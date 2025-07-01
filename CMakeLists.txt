cmake_minimum_required(VERSION 3.10)
project(ida_llm_agent)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# IDA SDK path
set(IDASDK_PATH "/Users/user/Documents/idasdk90" CACHE PATH "Path to IDA SDK")

# IDA Pro plugins directory path
set(IDA_PLUGINS_PATH "/Applications/IDA Professional 9.0.app/Contents/MacOS/plugins" CACHE PATH "Path to IDA Pro plugins directory")

# IDA Qt build setup
set(QTDIR "/Users/Shared/Qt/5.15.2-arm64" CACHE PATH "Path to IDA's custom Qt installation")

# Set Qt tool paths explicitly
set(QT_MOC_EXECUTABLE "${QTDIR}/bin/moc")
set(QT_UIC_EXECUTABLE "${QTDIR}/bin/uic")
set(QT_RCC_EXECUTABLE "${QTDIR}/bin/rcc")

# Verify Qt tools exist
if(NOT EXISTS ${QT_MOC_EXECUTABLE})
    message(FATAL_ERROR "MOC tool not found at: ${QT_MOC_EXECUTABLE}")
endif()
if(NOT EXISTS ${QT_UIC_EXECUTABLE})
    message(FATAL_ERROR "UIC tool not found at: ${QT_UIC_EXECUTABLE}")
endif()
if(NOT EXISTS ${QT_RCC_EXECUTABLE})
    message(FATAL_ERROR "RCC tool not found at: ${QT_RCC_EXECUTABLE}")
endif()

# Set Qt version variables that AUTOMOC expects (Qt 5.15.2)
set(Qt5Core_VERSION_MAJOR 5)
set(Qt5Core_VERSION_MINOR 15)
set(Qt5Core_VERSION_PATCH 2)
set(Qt5Core_VERSION "${Qt5Core_VERSION_MAJOR}.${Qt5Core_VERSION_MINOR}.${Qt5Core_VERSION_PATCH}")

# Set directory properties for Qt version (required by AUTOMOC)
set_property(DIRECTORY PROPERTY Qt5Core_VERSION_MAJOR ${Qt5Core_VERSION_MAJOR})
set_property(DIRECTORY PROPERTY Qt5Core_VERSION_MINOR ${Qt5Core_VERSION_MINOR})

# Create IMPORTED targets for Qt tools so AUTOMOC/AUTOUIC/AUTORCC can find them
add_executable(Qt5::moc IMPORTED)
set_property(TARGET Qt5::moc PROPERTY IMPORTED_LOCATION ${QT_MOC_EXECUTABLE})

add_executable(Qt5::uic IMPORTED)
set_property(TARGET Qt5::uic PROPERTY IMPORTED_LOCATION ${QT_UIC_EXECUTABLE})

add_executable(Qt5::rcc IMPORTED)
set_property(TARGET Qt5::rcc PROPERTY IMPORTED_LOCATION ${QT_RCC_EXECUTABLE})

# Create Qt5::Core IMPORTED target with proper version info for AUTOMOC
add_library(Qt5::Core SHARED IMPORTED)
set_property(TARGET Qt5::Core PROPERTY IMPORTED_LOCATION ${QT_CORE_FRAMEWORK})
set_property(TARGET Qt5::Core PROPERTY INTERFACE_QT_MAJOR_VERSION 5)

# Enable Qt AUTO features - they will now use our custom Qt tools
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Add Qt framework search path
if(APPLE)
    set(CMAKE_FRAMEWORK_PATH ${CMAKE_FRAMEWORK_PATH} "${QTDIR}/lib")
endif()

# Qt include directories
include_directories(
        "${QTDIR}/lib/QtCore.framework/Headers"
        "${QTDIR}/lib/QtGui.framework/Headers"
        "${QTDIR}/lib/QtWidgets.framework/Headers"
        "${QTDIR}/lib/QtPrintSupport.framework/Headers"
        "${QTDIR}/lib/QtNetwork.framework/Headers"
        "${QTDIR}/lib/QtCLucene.framework/Headers"
        "${QTDIR}/lib/QtConcurrent.framework/Headers"
        "${QTDIR}/lib/QtDBus.framework/Headers"
        "${QTDIR}/lib/QtDesigner.framework/Headers"
        "${QTDIR}/lib/QtDesignerComponents.framework/Headers"
        "${QTDIR}/lib/QtHelp.framework/Headers"
        "${QTDIR}/lib/QtOpenGL.framework/Headers"
        "${QTDIR}/lib/QtSql.framework/Headers"
        "${QTDIR}/lib/QtTest.framework/Headers"
        "${QTDIR}/lib/QtUiPlugin.framework/Headers"
        "${QTDIR}/lib/QtXml.framework/Headers"
)

# Find Qt frameworks manually
find_library(QT_CORE_FRAMEWORK QtCore PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_GUI_FRAMEWORK QtGui PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_WIDGETS_FRAMEWORK QtWidgets PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_PRINTSUPPORT_FRAMEWORK QtPrintSupport PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_NETWORK_FRAMEWORK QtNetwork PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_CONCURRENT_FRAMEWORK QtConcurrent PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_SQL_FRAMEWORK QtSql PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_XML_FRAMEWORK QtXml PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_OPENGL_FRAMEWORK QtOpenGL PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
find_library(QT_TEST_FRAMEWORK QtTest PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)

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

# while 9.0 says that they moved hexrays.hpp (decompiler support) into the SDK, beta 2 that was leaked did not have this changed yet (https://docs.hex-rays.com/release-notes/9_0)
# you will find the hexrays sdk inside plugins/hexrays_sdk/
include_directories(${IDA_PLUGINS_PATH}/hexrays_sdk/include)

# Find IDA libraries
find_library(IDA64
        NAMES ida64
        PATHS ${IDA_LIBDIR}
        NO_DEFAULT_PATH
)

if(NOT IDA64)
    message(FATAL_ERROR "Could not find IDA libraries in ${IDA_LIBDIR}")
endif()

# end IDA SDK setup

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Source files
set(SOURCES
        src/actions.cpp
        src/ida_utils.cpp
        src/plugin.cpp
        src/memory.cpp
)

# Create shared library with correct naming convention (end in "64", wont be loaded if it doesn't)
add_library(ida_llm_agent64 SHARED ${SOURCES})

add_executable(sample_file sample_file.cpp)
set_target_properties(sample_file PROPERTIES
        CMAKE_BUILD_TYPE Release
)

target_compile_options(sample_file PRIVATE
        -O3          # Optimize
        -g0          # No debug info
        -DNDEBUG     # Release mode
)

# Link libraries including custom Qt frameworks
target_link_libraries(ida_llm_agent64
        ${IDA64}
        CURL::libcurl
        nlohmann_json::nlohmann_json
        Qt5::Core
        ${QT_GUI_FRAMEWORK}
        ${QT_WIDGETS_FRAMEWORK}
        ${QT_PRINTSUPPORT_FRAMEWORK}
        ${QT_NETWORK_FRAMEWORK}
        ${QT_CONCURRENT_FRAMEWORK}
        ${QT_SQL_FRAMEWORK}
        ${QT_XML_FRAMEWORK}
        ${QT_OPENGL_FRAMEWORK}
        ${QT_TEST_FRAMEWORK}
)