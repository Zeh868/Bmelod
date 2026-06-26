# 01 固件升级方案设计（独立 Bootloader + A/B swap）

> **状态：设计稿（未实现，待审查）**。本文描述 Bmelod 框架的平台无关固件升级能力设计，
> 参考 Zephyr/MCUboot 思路裁剪，目标是把「镜像格式 / 校验 / 分区抽象 / A/B swap 状态机与
> 掉电续传」沉淀为框架**共享库**，由 **独立 bootloader ELF** 与 **application ELF** 共同链接；
> 具体 flash 驱动、传输通道、分区物理地址、bootloader 镜像本身留给产品工程。
>
> 升级策略首期定为 **A/B swap（可回滚）**。本文尚未落地代码，供审查后再转入实现。

## 1. 目标与边界

**进框架（平台无关、可复用、native 可测）：**

- 镜像头/TLV 格式与校验（复用 `bm_crc32`，签名留扩展位）。
- 分区抽象 `bm_flash_area`（MCUboot `flash_area` 思路，静态 C 分区表，不引 DTS）。
- A/B swap 状态机与执行（swap_type 决策 + 逐扇区交换 + 掉电续传）。
- app 侧升级会话 `bm_update`（写 secondary、置 pending、自我 confirm）。
- flash 后端骨架 `bm_hal_flash`（native 文件后端可测，嵌入式留桩）。

**不进框架（平台 / 产品相关）：**

- 具体 flash 擦写驱动实现（走 `bm_hal_flash` 后端，vendor/产品填）。
- 传输通道（UART / CAN / OTA 收固件）。
- 分区物理地址、linker 脚本、bootloader 镜像本身。

## 2. 适用平台与重要分叉（务必先读）

本方案面向 **STM32 / GD32 等裸机 MCU**（无原生 OTA 基础设施，需自造 bootloader + A/B）。

> ⚠️ **ESP32（含灯哥 V4 / Hoverboard）不适用本方案的 swap 引擎**：ESP-IDF 自带二级
> bootloader、`otadata` 分区、`esp_ota_*` API 与回滚机制，工业级且免费。ESP32 目标应
> **直接用 IDF 原生 OTA**，框架侧只需对 `esp_ota_*` 做**薄封装**统一接口，**不要**自造本文的
> trailer / swap / scratch 逻辑。本设计的价值在裸机 MCU 平台。

因此本库的升级核心需保持**抽象分层**：上层接口（begin/write/confirm/版本查询）平台无关，
裸机平台走本文 A/B 引擎，ESP32 平台走 IDF OTA 后端——二者在 `bm_update` 接口下并存。

## 3. 现状基线

| 项 | 现状 |
|---|---|
| `include/bm/common/bm_crc32.h` | 已有，校验原语可直接复用 |
| `portable/boot/<target>/`（crt0/startup/linker） | 已有，是 **app 自身**的启动入口（非 bootloader） |
| bootloader / 升级 / 分区 / OTA / 镜像格式 | **零基础**，本方案新增 |
| `bm_persist`（#10）/ `bm_crc32` / `bm_shell`（#11） | 可作为升级层的复用积木 |

## 4. 总体架构

三个产物共享同一个核心库，单一事实源，格式逻辑不漂移：

```
                  共享库 libbm_boot.a (= MCUboot bootutil 等价物)
        bm_image(校验) / bm_flash_area(分区) / bm_boot(swap 状态机+执行)
              /                                              \
   Bootloader ELF (独立)                              Application ELF
   开机第一个运行:                                     正常业务
   读 trailer→定 swap_type→(必要时)逐扇区 swap         + bm_update 会话:写 secondary、
   →校验 primary→跳转                                  set_pending、confirm、收固件
   portable/bootloader/<target>/                       产品工程
              \                                              /
               两个 ELF 链接同一个 libbm_boot
```

**为什么 bootloader 必须是独立 ELF（不能只是 app 里的模块）：**

- 它要能擦掉并重写 app 所在 flash，自己绝不能运行在那块区域；
- 独立复位向量 / 链接脚本 / flash 区，开机第一个运行，**不依赖 app**（app 可能正损坏或正被覆盖）；
- 体积小、极稳定、几乎不更新。

**为什么核心要做成共享库（而非塞进 bootloader）：**

- app 侧也要用同一套格式写 secondary、置 pending、自我 confirm；共享 = 单一事实源；
- 脱离任何 ELF 即可在 native ctest 单独测（MCUboot 抽出 `bootutil` 同理）。

**Flash 布局（A/B swap-using-scratch 示意，地址为示例）：**

```
 0x0800_0000  Bootloader        独立向量表，几乎不更新
 0x0800_8000  Primary slot      当前运行的 app + 尾部 trailer
 0x0804_0000  Secondary slot    新固件暂存 + 尾部 trailer   (与 primary 等大)
 0x0807_E000  Scratch           swap 中转区 (>=1 扇区)
 0x0807_F000  (可选) 元数据       非关键信息走 bm_persist
```

## 5. A/B swap 核心机制（借鉴 MCUboot）

### 5.1 slot trailer（每个 slot 尾部，从尾向前）

```
... [ swap_status 逐扇区进度 ] [ swap_size ] [ swap_info ] [ copy_done ] [ image_ok ] [ BOOT_MAGIC 16B ]
```

- `BOOT_MAGIC`：secondary 写好新固件后置上，标记「请求升级」。
- `image_ok`：app 启动后自我确认写入；**缺失 = 未确认 → 下次回滚**。
- `copy_done` / `swap_status`：swap 执行进度；**掉电后 bootloader 据此续传**（A/B 最关键的原子性保证）。

### 5.2 swap_type 决策

| swap_type | 触发条件 | 动作 |
|---|---|---|
| `NONE` | secondary 无 magic | 直接跳 primary |
| `TEST` | secondary 有 magic、image_ok 空 | swap，**试运行一次**；app 不 confirm 则下次 REVERT |
| `PERM` | secondary 有 magic、image_ok=确认 | swap 并永久生效 |
| `REVERT` | 上次 TEST 后未确认 | 把旧固件 swap 回来 |
| `FAIL` | 新镜像校验失败 | 拒绝，保持 primary |

### 5.3 升级全时序

```
app 收固件 → bm_update 写满 secondary + 校验 → set_pending(置 secondary magic) → 复位
  → bootloader: 读 trailer=TEST → 校验 secondary → 逐扇区 swap(scratch 中转，记 swap_status)
  → 跳 primary(=新固件，试运行)
  → 新 app 自检 OK → bm_boot_confirm()(写 image_ok) → 永久生效
  → 若新 app 崩溃/不 confirm → 复位 → bootloader: REVERT → swap 回旧固件
```

### 5.4 scratch vs move

本方案主线用 **swap-using-scratch**（经典易懂，需独立 scratch 区）。若 flash 紧张，
`bm_boot` 内部可改 **swap-using-move**（secondary 多一个扇区、primary 上移，省 scratch 区）——
这是实现选择，**不影响对外 API**。

## 6. 模块分解与 API 草图

### 6.1 `include/hal/bm_hal_flash.h` — 后端原语（沿用 #10 `BM_DRV_HAS_BACKEND` 模式）

```c
int      bm_hal_flash_read (uint32_t addr, void *buf, uint32_t len);
int      bm_hal_flash_write(uint32_t addr, const void *buf, uint32_t len);
int      bm_hal_flash_erase(uint32_t addr, uint32_t len);   /* 按扇区对齐 */
uint32_t bm_hal_flash_sector_size(uint32_t addr);
```

native 后端 `portable/sim/native/bm_drv_flash_native.c`：文件模拟，带 `set_path`/`reset`
钩子（复用 #10 套路模拟掉电）。

### 6.2 `include/bm/component/bm_flash_area.h` — 分区抽象

```c
typedef enum { BM_FA_PRIMARY, BM_FA_SECONDARY, BM_FA_SCRATCH } bm_fa_id_t;
const bm_flash_area_t *bm_flash_area_open(bm_fa_id_t id);   /* 静态 layout 表 */
int bm_flash_area_read (const bm_flash_area_t*, uint32_t off, void *buf, uint32_t len);
int bm_flash_area_write(const bm_flash_area_t*, uint32_t off, const void *buf, uint32_t len);
int bm_flash_area_erase(const bm_flash_area_t*, uint32_t off, uint32_t len);
```

分区表 = 编译期静态 C 结构（不引 DTS），产品在 board 配置里给地址。

### 6.3 `include/bm/component/bm_image.h` — 镜像格式/校验

```c
typedef struct {
    uint32_t magic; uint16_t hdr_size; uint16_t tlv_off;
    uint32_t img_size; uint32_t flags;
    struct { uint8_t major, minor; uint16_t rev; uint32_t build; } ver;
} bm_image_header_t;

int bm_image_verify(const bm_flash_area_t *fa);  /* 读 header→遍历 TLV→CRC32(复用 bm_crc32) */
```

### 6.4 `include/bm/component/bm_boot.h` — swap 状态机 + 执行（共享库核心）

```c
bm_swap_type_t bm_boot_decide(void);          /* 读两 slot trailer 定 swap_type */
int            bm_boot_perform_swap(void);    /* 逐扇区 swap，scratch 中转，可断电续传 */
int            bm_boot_go(void);              /* 校验 primary→跳转(bootloader 调用) */
```

### 6.5 `include/bm/component/bm_update.h` — app 侧会话

```c
int bm_update_begin(uint32_t total_size);
int bm_update_write(const void *chunk, uint32_t len);   /* 顺序写 secondary */
int bm_update_finalize(void);                           /* 校验 + set_pending(置 magic) */
int bm_boot_confirm(void);                              /* app 自检通过后写 image_ok */
```

### 6.6 `portable/bootloader/<target>/` — 独立 ELF

极简 main = `bm_boot_decide → bm_boot_perform_swap → bm_boot_go`；独立 linker.ld / 向量表；
只链 `libbm_boot`，不依赖 app。

## 7. 镜像产物与烧录流程

### 7.1 镜像打包/签名工具（关键、易漏）

送进 bootloader 的 app **不是裸 .bin**，而是套了 `bm_image_header_t` + 尾部 CRC TLV 的
**signed image**（否则 bootloader 校验不过）。需一个构建后工具，等价于 MCUboot `imgtool.py`：

```
app.elf --objcopy--> app_raw.bin --[ tools/bm_imgtool.py ]--> signed_app.bin
                                    加 header(magic/版本/img_size) + 末尾 CRC32 TLV
```

`bm_imgtool` 是镜像格式的**生产端**，与 `bm_image_verify`（消费端）共用同一份头定义——
单一事实源。

### 7.2 构建产物（三类）

| 产物 | 说明 |
|---|---|
| `bootloader.bin` | 裸 bootloader 镜像 |
| `signed_app.bin` | 套了 header+TLV 的 app；**首烧入 primary / OTA 入 secondary，同一文件两用** |
| `merged.hex`（可选） | 首次烧录把 bootloader + signed_app 按地址拼成单文件 |

### 7.3 首次烧录（产线/开发）：分开烧 vs 合并烧

| 方式 | 做法 | 适用 |
|---|---|---|
| **分开烧** | 调试器分别烧 `bootloader.bin @BL_ADDR`、`signed_app.bin @PRIMARY_ADDR` | 开发期（bootloader 烧一次基本不动） |
| **合并烧** | 脚本（`srec_cat`/`objcopy`/自写）按地址拼成 `merged.hex` 一次烧 | 产线（单文件、单次、不烧错地址） |

> 这只是「烧录介质上的打包」，**不改运行时架构**——flash 里 bootloader 与 app 永远是两个
> 独立镜像、独立 slot。地址一致性由 linker.ld + 分区表保证，合并脚本读同一份地址定义。
> 建议：开发期分开烧，发布/产线提供合并脚本一次烧。

### 7.4 升级烧录（量产后）

传输通道只送 **signed_app** → `bm_update` 写 secondary → 重启走 A/B swap。
**bootloader 自身默认出厂固化、不参与 OTA**（自更新会把最后防线置于风险中，易变砖）。

## 8. 复用已有积木

| 积木 | 用途 |
|---|---|
| `bm_crc32` | `bm_image` 校验 |
| `bm_shell`（#11） | `fwinfo` / `fwconfirm` / `fwstatus` 调试命令 |
| `bm_persist`（#10） | 只存**非关键**元数据（版本号、升级计数）；**swap 关键状态走 slot trailer，不压在 KV 上** |

## 9. 测试策略（全 native 可测）

native 文件后端把整条 A/B 链路搬进 ctest：

- 写镜像到 secondary → `bm_image_verify` → `set_pending` → `bm_boot_decide`=TEST →
  `perform_swap` → 断言 primary=新固件；
- **掉电续传**：swap 中途 `reset` 后端 → 重启 bootloader → 断言据 swap_status 续完、结果一致；
- **回滚**：TEST 后不 confirm → 重启 → `decide`=REVERT → 断言换回旧固件；
- `confirm` 后 → 重启 → `decide`=NONE。

## 10. 文件落点

```
include/hal/bm_hal_flash.h                       portable/sim/native/bm_drv_flash_native.{c,h}
include/bm/component/bm_flash_area.h             Source/component/bm_flash_area.c
include/bm/component/bm_image.h                  Source/component/bm_image.c
include/bm/component/bm_boot.h                   Source/component/bm_boot.c
include/bm/component/bm_update.h                 Source/component/bm_update.c
tools/bm_imgtool.py                              (镜像打包/签名)
portable/bootloader/<target>/{main.c,linker.ld}  (S5，上板)
tests/unit/{test_flash_area,test_image,test_boot_swap,test_update}.c
CMake: 新 target libbm_boot(共享核心) + libbm_update(app 侧)，app/bl 各自 link
```

## 11. 实施切片

| 切片 | 内容 | 可测性 |
|---|---|---|
| **S0** | `tools/bm_imgtool.py` 打包工具 + 产物/烧录文档（与 S2 头格式同源，建议同批） | 生成镜像喂 `bm_image_verify` 闭环 |
| **S1** | `bm_hal_flash` + native 后端 + `bm_flash_area` | native ✓ |
| **S2** | `bm_image` header/TLV + CRC 校验 | native ✓ |
| **S3** | `bm_boot` trailer 编解码 + swap_type 决策状态机 | native ✓ |
| **S4** | `bm_boot_perform_swap` 逐扇区 swap + 掉电续传 + `bm_update` 会话 | native ✓（含断电/回滚用例） |
| **S5** | bootloader ELF + vendor flash 后端 + 传输 | 上板 |

S1–S4（+S0）= 框架共享库，本机 ctest 全验收；S5 上板归产品工程。

## 12. 风险与关键点

- **掉电原子性是 A/B 成败点**：`swap_status` 逐扇区记录 + 幂等续传必须在 S4 用 native
  「中途 reset」用例**充分覆盖**，否则升级期断电会变砖。
- **flash 预算**：A/B 需约 2× app 大小 + scratch；目标芯片 flash 容量需先确认放得下，
  否则退 overwrite-only（牺牲回滚）或 swap-using-move（省 scratch）。
- **flash 写粒度/对齐**：不同 flash 最小写单位不同（STM32 双字 / ESP 4B），`bm_hal_flash`
  需暴露 `sector_size` 与写对齐约束，trailer 布局按最坏对齐设计。
- **平台分叉**：ESP32 走 IDF OTA 而非本引擎（见 §2），勿在 ESP 目标自造 swap。

## 13. 待决策项（审查时确认）

1. **目标平台**：本方案面向裸机/STM32 类；若近期升级需求落在 ESP32（灯哥 V4），应改走 IDF OTA 薄封装路线（§2）。
2. **flash 预算**：目标芯片 flash 总量？决定 A/B（scratch）/ swap-using-move / overwrite-only 取舍。
3. **升级策略**：已定 **A/B swap 可回滚**；若 flash 不足再降级。
4. **推进方式**：S0–S4 一次性做完整个共享库，还是分批（S1 → S2+S0 → S3 → S4）逐批审查。
5. **boot 标志存储**：关键 swap 状态走 slot trailer（已定），`bm_persist` 仅存版本/统计。
