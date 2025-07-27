# Custom build for Keystone assembler library
# This avoids CMake version compatibility issues with the upstream keystone

set(KEYSTONE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/keystone)
set(KEYSTONE_LLVM_DIR ${KEYSTONE_SOURCE_DIR}/llvm)

# Keystone version
set(KEYSTONE_VERSION_MAJOR 0)
set(KEYSTONE_VERSION_MINOR 9)

# Core source files
file(GLOB_RECURSE KEYSTONE_MC_SOURCES "${KEYSTONE_LLVM_DIR}/lib/MC/*.cpp")
file(GLOB KEYSTONE_SUPPORT_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Support/*.c*")
file(GLOB KEYSTONE_TARGET_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/*.cpp")

# Architecture-specific sources
file(GLOB_RECURSE KEYSTONE_AARCH64_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/AArch64/*.cpp")
file(GLOB_RECURSE KEYSTONE_ARM_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/ARM/*.cpp")
file(GLOB_RECURSE KEYSTONE_X86_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/X86/*.cpp")
file(GLOB_RECURSE KEYSTONE_MIPS_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/Mips/*.cpp")
file(GLOB_RECURSE KEYSTONE_PPC_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/PowerPC/*.cpp")
file(GLOB_RECURSE KEYSTONE_SPARC_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/Sparc/*.cpp")
file(GLOB_RECURSE KEYSTONE_SYSTEMZ_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/SystemZ/*.cpp")
file(GLOB_RECURSE KEYSTONE_HEXAGON_SOURCES "${KEYSTONE_LLVM_DIR}/lib/Target/Hexagon/*.cpp")

# Keystone engine sources
set(KEYSTONE_ENGINE_SOURCES
    ${KEYSTONE_LLVM_DIR}/keystone/ks.cpp
    ${KEYSTONE_LLVM_DIR}/keystone/EVMMapping.cpp
)

# Combine all sources
set(KEYSTONE_ALL_SOURCES
    ${KEYSTONE_MC_SOURCES}
    ${KEYSTONE_SUPPORT_SOURCES}
    ${KEYSTONE_TARGET_SOURCES}
    ${KEYSTONE_ENGINE_SOURCES}
    # Add architecture sources
    ${KEYSTONE_AARCH64_SOURCES}
    ${KEYSTONE_ARM_SOURCES}
    ${KEYSTONE_X86_SOURCES}
    ${KEYSTONE_MIPS_SOURCES}
    ${KEYSTONE_PPC_SOURCES}
    ${KEYSTONE_SPARC_SOURCES}
    ${KEYSTONE_SYSTEMZ_SOURCES}
    ${KEYSTONE_HEXAGON_SOURCES}
)

# Create the library
add_library(keystone STATIC ${KEYSTONE_ALL_SOURCES})

# Disable Qt processing for this library
set_target_properties(keystone PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# Set include directories
target_include_directories(keystone PUBLIC
    ${KEYSTONE_SOURCE_DIR}/include
    ${KEYSTONE_LLVM_DIR}/include
    ${KEYSTONE_LLVM_DIR}/keystone
    ${CMAKE_CURRENT_BINARY_DIR}/keystone-config
)

# Architecture-specific includes
target_include_directories(keystone PRIVATE
    ${KEYSTONE_LLVM_DIR}/lib/Target/AArch64
    ${KEYSTONE_LLVM_DIR}/lib/Target/ARM
    ${KEYSTONE_LLVM_DIR}/lib/Target/X86
    ${KEYSTONE_LLVM_DIR}/lib/Target/Mips
    ${KEYSTONE_LLVM_DIR}/lib/Target/PowerPC
    ${KEYSTONE_LLVM_DIR}/lib/Target/Sparc
    ${KEYSTONE_LLVM_DIR}/lib/Target/SystemZ
    ${KEYSTONE_LLVM_DIR}/lib/Target/Hexagon
)

# Generate config headers
set(KEYSTONE_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/keystone-config)
file(MAKE_DIRECTORY ${KEYSTONE_CONFIG_DIR}/llvm/Config)
file(MAKE_DIRECTORY ${KEYSTONE_CONFIG_DIR}/llvm/Support)

# Create config.h
file(WRITE ${KEYSTONE_CONFIG_DIR}/llvm/Config/config.h "
#ifndef LLVM_CONFIG_H
#define LLVM_CONFIG_H

#define LLVM_VERSION_MAJOR 3
#define LLVM_VERSION_MINOR 9
#define LLVM_VERSION_PATCH 0

#define LLVM_HOST_TRIPLE \"${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}\"
#define LLVM_DEFAULT_TARGET_TRIPLE \"${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}\"

#define LLVM_ENABLE_THREADS 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOULL 1
#define HAVE_MALLOC_H 0
#define HAVE_MALLOC_MALLOC_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_PTHREAD_MUTEX_LOCK 1

#if defined(__APPLE__)
#define HAVE_MACH_MACH_H 1
#endif

#endif
")

# Create llvm-config.h
file(WRITE ${KEYSTONE_CONFIG_DIR}/llvm/Config/llvm-config.h "
#ifndef LLVM_CONFIG_LLVM_CONFIG_H
#define LLVM_CONFIG_LLVM_CONFIG_H

#define LLVM_VERSION_MAJOR 3
#define LLVM_VERSION_MINOR 9
#define LLVM_VERSION_PATCH 0
#define LLVM_VERSION_STRING \"3.9.0\"

#define LLVM_HOST_TRIPLE \"${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}\"
#define LLVM_DEFAULT_TARGET_TRIPLE LLVM_HOST_TRIPLE

#define LLVM_ENABLE_THREADS 1

#endif
")

# Create AsmParsers.def
file(WRITE ${KEYSTONE_CONFIG_DIR}/llvm/Config/AsmParsers.def "
LLVM_ASM_PARSER(AArch64)
LLVM_ASM_PARSER(ARM)
LLVM_ASM_PARSER(Hexagon)
LLVM_ASM_PARSER(Mips)
LLVM_ASM_PARSER(PowerPC)
LLVM_ASM_PARSER(Sparc)
LLVM_ASM_PARSER(SystemZ)
LLVM_ASM_PARSER(X86)
")

# Create Targets.def
file(WRITE ${KEYSTONE_CONFIG_DIR}/llvm/Config/Targets.def "
LLVM_TARGET(AArch64)
LLVM_TARGET(ARM)
LLVM_TARGET(Hexagon)
LLVM_TARGET(Mips)
LLVM_TARGET(PowerPC)
LLVM_TARGET(Sparc)
LLVM_TARGET(SystemZ)
LLVM_TARGET(X86)
")

# Create DataTypes.h
file(WRITE ${KEYSTONE_CONFIG_DIR}/llvm/Support/DataTypes.h "
#ifndef LLVM_SUPPORT_DATATYPES_H
#define LLVM_SUPPORT_DATATYPES_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef INT8_MAX
#define INT8_MAX 127
#endif
#ifndef INT8_MIN
#define INT8_MIN -128
#endif
#ifndef UINT8_MAX
#define UINT8_MAX 255
#endif
#ifndef INT16_MAX
#define INT16_MAX 32767
#endif
#ifndef INT16_MIN
#define INT16_MIN -32768
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-INT32_MAX-1)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif
#ifndef INT64_MAX
#define INT64_MAX 9223372036854775807LL
#endif
#ifndef INT64_MIN
#define INT64_MIN (-INT64_MAX-1)
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 18446744073709551615ULL
#endif

#endif
")

# Set compile definitions
target_compile_definitions(keystone PRIVATE
    _GNU_SOURCE
    __STDC_CONSTANT_MACROS
    __STDC_FORMAT_MACROS
    __STDC_LIMIT_MACROS
)

# Platform-specific settings
if(APPLE)
    target_compile_definitions(keystone PRIVATE
        HAVE_MACH_MACH_H
        HAVE_MALLOC_MALLOC_H
    )
elseif(UNIX)
    target_compile_definitions(keystone PRIVATE
        HAVE_MALLOC_H
    )
endif()

# Set C++ standard
set_target_properties(keystone PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Disable warnings for third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(keystone PRIVATE
        -w  # Disable all warnings
    )
elseif(MSVC)
    target_compile_options(keystone PRIVATE
        /W0  # Disable all warnings
    )
endif()

message(STATUS "Configured Keystone assembler library")