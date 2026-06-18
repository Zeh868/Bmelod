set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(CMAKE_C_FLAGS "-mcpu=cortex-m0 -mthumb -Os -ffunction-sections -fdata-sections -Wall -Wextra -std=c99")
set(CMAKE_EXE_LINKER_FLAGS "-nostartfiles -Wl,--gc-sections")
