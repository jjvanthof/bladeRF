################################################################################
# Configure CMake and setup variables for the Cypress FX3 SDK v1.3.3
################################################################################
cmake_minimum_required(VERSION 2.8)

################################################################################
# Settings and Configuration
################################################################################

set(FX3_SDK_VERSION "1.3.3" CACHE STRING "Cypress FX3 SDK version")
set(GCC_VERSION     "4.8.1" CACHE STRING "GCC version included with SDK")

if(WIN32)
    set(EXE ".exe")
else()
    unset(EXE)
endif()

################################################################################
# FX3 SDK paths and files
################################################################################

# Paths to a specfic version use underscores instead of paths
string(REGEX REPLACE "\." "_" FX3_SDK_VERSION_PATH FX3_SDK_VERSION)

if(WIN32)
    set(FX3_SDK_PATH "C:/Program Files (x86)/Cypress/EZ-USB FX3 SDK/1.3"
        CACHE PATH "Path to FX3 SDK")

    set(ARM_NONE_EABI_PATH "${FX3_SDK_PATH}/ARM GCC"
        CACHE PATH "Path to arm-none-eabi toolchain")

    set(FX3_FW_DIR "${FX3_SDK_PATH}/fw_lib/${FX3_SDK_VERSION_PATH}"
        CACHE PATH "Path to FX3 (device) libraries")

    set(FX3_UTIL_DIR "${FX3_SDK_PATH}/util"
        CACHE PATH "Path to host utilities")

else()
    set(FX3_SDK_PATH "/opt/cypress/fx3_sdk"
        CACHE PATH "Path to FX3 SDK")

    set(ARM_NONE_EABI_PATH  "${FX3_SDK_PATH}/arm-2011.03"
        CACHE PATH "Path to arm-none-eabi toolchain")

    set(FX3_FW_DIR "${FX3_SDK_PATH}/cyfx3sdk/fw_lib/${FX3_SDK_VERSION_PATH}"
        CACHE PATH "Path to FX3 (device) libraries")

endif(WIN32)


if(${CMAKE_BUILD_TYPE} EQUAL "Debug")
    set(FX3_LIBRARY_DIR "${FX3_FW_DIR}/fx3_debug" 
        CACHE PATH "Path to FX3 device libraries for a debug build")
else()
    set(FX3_LIBRARY_DIR "${FX3_FW_DIR}/fx3_release"
        CACHE PATH "Path to FX3 device libraries for a release build")
endif()

# Perform some sanity checks on the above paths
if(NOT EXISTS "${FX3_FW_DIR}")
    message(FATAL_ERROR "FX3 SDK: Could not find ${FX3_FW_DIR}")
endif()

if(NOT EXISTS "${FX3_INCLUDE_DIR}/cyu3os.h")
    message(FATAL_ERROR "cyu3os.h is missing. Check FX3_INCLUDE_DIR definition.")
endif()

if(NOT_EXISTS "${FX3_LIBRARY_DIR}/libcyfxapi.a")
    message(FATAL_ERROR "libcyfxapi.a is missing. Check FX3_LIBRARY_DIR definition.")
endif()

set(FX3_INCLUDE_DIR "${FX3_FW_DIR}/inc"
    CACHE PATH "Path to FX3 device library header files")

################################################################################
# ARM Toolchain paths and sanity checks
################################################################################

set(CMAKE_C_COMPILER "${ARM_NONE_EABI_PATH}/bin/arm-none-eabi-gcc${EXE}")

set(ARM_NONE_EABI_LIBGCC
    "${ARM_NONE_EABI_PATH}/lib/gcc/arm-none-eabi/${ARM_NONE_EABI_GCC_VERSION}/libgcc.a")

set(ARM_NONE_EABI_LIBC "${ARM_NONE_EABI_PATH}/arm-none-eabi/lib/libc.a")

if(NOT EXISTS ${CMAKE_C_COMPILER})
    message(FATAL_ERROR "Could not find ${CMAKE_C_COMPILER}")
endif()

if(NOT EXISTS ${ARM_NONE_EABI_LIBGCC})
    message(FATAL_ERROR "Could not find libgcc.a")
endif()

if(NOT EXISTS ${ARM_NONE_EABI_LIBC})
    message(FATAL_ERROR "Could not find libc.a")
endif()

################################################################################
# Additional build configuration helper definitions
################################################################################

# Allow users to override this by defining this variable
if(NOT FX3_LINKER_FILE)
    set(FX3_LINKER_FILE ${FX3_
endif()

set(FX3_LIBRARIES
    ${ARM_NONE_EABI_LIBGCC}
    ${ARM_NONE_EABI_LIBC}
    ${FX3_LIB_DIR}/libcyfxapi.a
    ${FX3_LIB_DIR}/libcyu3lpp.a
    ${FX3_LIB_DIR}/libcyu3threadx.a
)

set(FX3_CFLAGS "-DCYU3P_FX3=1 -D__CYU3P_TX__=1 -mcpu=arm926ej-s -mthumb-interwork")
set(FX3_LDFLAGS
    "-Wl,--entry,CyU3PFirmwareEntry -Wl,-T,${FX3_LINKER_FILE} -Wl,-d -Wl,--gc-sections -Wl,--no-wchar-size-warning")


################################################################################
# CMake toolchain file configuration
################################################################################

include(CMakeForceCompiler)
set(CMAKE_SYSTEM_NAME fx3)

CMAKE_FORCE_C_COMPILER(arm-none-eabi-gcc GNU)
CMAKE_FORCE_CXX_COMPILER(arm-none-eabi-g++ GNU)

# Search for programms in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH ${ARM_NONE_EABI_PATH})
