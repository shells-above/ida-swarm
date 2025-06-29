cmake_minimum_required(VERSION 3.10)
project(ida_re_agent)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# IDA SDK path
set(IDASDK_PATH "/Users/user/Documents/idasdk90" CACHE PATH "Path to IDA SDK")

# IDA Pro plugins directory path
set(IDA_PLUGINS_PATH "/Applications/IDA Professional 9.0.app/Contents/MacOS/plugins" CACHE PATH "Path to IDA Pro plugins directory")

# Detect system and architecture
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/arm64_mac_clang_64")
        add_definitions(-D__MAC__ -D__EA64__ -D__ARM__)
    else()
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_mac_clang_64")
        add_definitions(-D__MAC__ -D__EA64__)
    endif()
    set(IDA_LIB_SUFFIX ".dylib")

elseif(UNIX AND NOT APPLE)
    set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_linux_gcc_64")
    add_definitions(-D__LINUX__ -D__EA64__)
    set(IDA_LIB_SUFFIX ".so")
elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_win_vc_64")
    else()
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_win_vc_32")
    endif()
    add_definitions(-D__NT__ -D__EA64__)
    set(IDA_LIB_SUFFIX ".lib")
endif()

# Include directories
include_directories(${IDASDK_PATH}/include)

# Find IDA libraries
find_library(IDA64
        NAMES ida64
        PATHS ${IDA_LIBDIR}
        NO_DEFAULT_PATH
)

if(NOT IDA64)
    message(FATAL_ERROR "Could not find IDA libraries in ${IDA_LIBDIR}")
endif()

# Source files
set(SOURCES plugin.cpp)

# Create shared library with correct naming convention (end in "64", wont be loaded if it doesn't)
add_library(ida_analyzer64 SHARED ${SOURCES})

# Link libraries
target_link_libraries(ida_analyzer64 ${IDA64})

# Set RPATH for finding libraries at runtime
if(APPLE)
    set_target_properties(ida_analyzer64 PROPERTIES
            BUILD_RPATH "@executable_path;${IDA_LIBDIR}"
            INSTALL_RPATH "@executable_path;${IDA_LIBDIR}"
    )
elseif(UNIX AND NOT APPLE)
    set_target_properties(ida_analyzer64 PROPERTIES
            BUILD_RPATH "$ORIGIN;${IDA_LIBDIR}"
            INSTALL_RPATH "$ORIGIN;${IDA_LIBDIR}"
    )
endif()

# Option to copy built plugin to IDA Pro plugins directory
option(INSTALL_TO_IDA "Copy built plugin to IDA Pro plugins directory" ON)
if(INSTALL_TO_IDA)
    add_custom_command(TARGET ida_analyzer64 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ida_analyzer64>
            "${IDA_PLUGINS_PATH}/ida_analyzer64.dylib"
            COMMENT "Installing plugin to IDA Pro plugins directory: ${IDA_PLUGINS_PATH}"
    )
endif()
