set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    set(_BM_HOST_EXE ".exe")
else()
    set(_BM_HOST_EXE "")
endif()

set(BM_ARM_NONE_EABI_G4_FLOAT_ABI "hard" CACHE STRING
    "STM32G4 FPU ABI: hard / softfp / soft (must match App/Cube project)")
set_property(CACHE BM_ARM_NONE_EABI_G4_FLOAT_ABI PROPERTY STRINGS hard softfp soft)

set(_BM_G4_ARCH_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=${BM_ARM_NONE_EABI_G4_FLOAT_ABI}")

set(CMAKE_C_FLAGS "${_BM_G4_ARCH_FLAGS} -Os -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_ASM_FLAGS "${_BM_G4_ARCH_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "-nostartfiles -Wl,--gc-sections")
