################################################################################
# Build bladeRF FX3 Firmware
################################################################################
cmake_minimum_required(VERSION 2.8.3)
project(bladeRF_fw C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../host/cmake/modules)

# Update these definitions when updating the firmware version
set(VERSION_INFO_MAJOR 1)
set(VERSION_INFO_MINOR 8)
set(VERSION_INFO_PATCH 1)

if(NOT DEFINED VERSION_INFO_EXTRA)
    set(VERSION_INFO_EXTRA "git")
endif()
include(Version)

set(VERSION "${VERSION_INFO}")

# Generate firmware version header file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY
)

################################################################################
# Drop our .gdbinit into our build output directory for convenience
################################################################################
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.gdbinit
               ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/.gdbinit
               @ONLY)
