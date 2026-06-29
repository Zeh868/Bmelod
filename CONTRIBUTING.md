# 贡献指南（Contributing）

感谢你对 Bmelod Baremetal Framework 的关注与贡献。

## 授权模式与为何需要 CLA

本项目采用**双授权（GPL-3.0-or-later 或 商业授权）**模式（见 `LICENSE`、
`COMMERCIAL-LICENSE.md`、`NOTICE`）。为了能够持续对外提供商业授权，版权持有人
必须对项目代码拥有**可再许可（relicense）**的权利。因此，所有贡献者在提交贡献前
需签署贡献者许可协议（CLA），授予版权持有人将其贡献以 GPLv3 与商业授权两种方式
分发的权利。

若不签署 CLA，贡献将无法被合并。

## 提交规范

- C/H 文件须遵循项目既有的 **Doxygen 中文注释**约定（文件头、公共 API、static
  辅助函数均需注释）。
- 每个新增的自有源码文件，文件头需带 SPDX 标识：
  `SPDX-License-Identifier: GPL-3.0-or-later`
- 保持最小改动，匹配既有代码风格与命名。
- 不要修改 `third_party/` 与 `tests/unit/unity/` 中的第三方文件，也不要为其添加
  本项目 SPDX 标识（它们各自保留原始许可证）。
- 提交信息使用中文，清晰说明动机与改动范围。

## 验证

- 单核默认构建（`BM_CONFIG_SMP=OFF`）单测须全部通过。
- 若改动涉及多核，`BM_CONFIG_SMP=ON` 的 native 与 qemu SMP 测试须全部通过。

## 流程

1. Fork / 新建分支进行修改。
2. 确保本地构建与相关测试通过。
3. 在 PR 中确认你已阅读并同意本指南与 CLA（见 `CLA.md`）。
4. 等待维护者审查与合并。
