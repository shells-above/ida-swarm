cmake_minimum_required(VERSION 3.16)
project(ida_swarm_private)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# IDA SDK path
if(APPLE)
    set(IDASDK_PATH "/Users/user/Documents/idasdk90" CACHE PATH "Path to IDA SDK")
elseif(UNIX AND NOT APPLE)
    set(IDASDK_PATH "/home/one/Documents/idasdk90" CACHE PATH "Path to IDA SDK")
else()
    set(IDASDK_PATH "/Users/user/Documents/idasdk90" CACHE PATH "Path to IDA SDK")
endif()

# IDA Pro plugins directory path
if(APPLE)
    set(IDA_PLUGINS_PATH "/Applications/IDA Professional 9.0.app/Contents/MacOS/plugins" CACHE PATH "Path to IDA Pro plugins directory")
elseif(UNIX AND NOT APPLE)
    set(IDA_PLUGINS_PATH "/home/one/idapro-9.0/plugins" CACHE PATH "Path to IDA Pro plugins directory")
else()
    set(IDA_PLUGINS_PATH "/Applications/IDA Professional 9.0.app/Contents/MacOS/plugins" CACHE PATH "Path to IDA Pro plugins directory")
endif()

# IDA Qt build setup
if(APPLE)
    set(QTDIR "/Users/Shared/Qt/5.15.2-arm64" CACHE PATH "Path to IDA's custom Qt installation")
elseif(UNIX AND NOT APPLE)
    set(QTDIR "/home/one/Documents/qt-5.15.2-full-IDA84/5.15.2-amd64" CACHE PATH "Path to IDA's custom Qt installation")
else()
    set(QTDIR "/Users/Shared/Qt/5.15.2-arm64" CACHE PATH "Path to IDA's custom Qt installation")
endif()
add_definitions(-DQT_NAMESPACE=QT)  # SUPER important

# Set Qt tool paths explicitly - adjust for Linux build directory structure
if(APPLE)
    set(QT_MOC_EXECUTABLE "${QTDIR}/bin/moc")
    set(QT_UIC_EXECUTABLE "${QTDIR}/bin/uic")
    set(QT_RCC_EXECUTABLE "${QTDIR}/bin/rcc")
elseif(UNIX AND NOT APPLE)
    set(QT_MOC_EXECUTABLE "${QTDIR}/build/qtbase/bin/moc")
    set(QT_UIC_EXECUTABLE "${QTDIR}/build/qtbase/bin/uic")
    set(QT_RCC_EXECUTABLE "${QTDIR}/build/qtbase/bin/rcc")
else()
    set(QT_MOC_EXECUTABLE "${QTDIR}/bin/moc")
    set(QT_UIC_EXECUTABLE "${QTDIR}/bin/uic")
    set(QT_RCC_EXECUTABLE "${QTDIR}/bin/rcc")
endif()

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

# Find Qt frameworks/libraries manually - adjust for platform
if(APPLE)
    find_library(QT_CORE_FRAMEWORK QtCore PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_GUI_FRAMEWORK QtGui PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_WIDGETS_FRAMEWORK QtWidgets PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_PRINTSUPPORT_FRAMEWORK QtPrintSupport PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_CONCURRENT_FRAMEWORK QtConcurrent PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_SQL_FRAMEWORK QtSql PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_XML_FRAMEWORK QtXml PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_OPENGL_FRAMEWORK QtOpenGL PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_TEST_FRAMEWORK QtTest PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
elseif(UNIX AND NOT APPLE)
    # On Linux, find the .so files
    find_library(QT_CORE_FRAMEWORK Qt5Core PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_GUI_FRAMEWORK Qt5Gui PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_WIDGETS_FRAMEWORK Qt5Widgets PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_PRINTSUPPORT_FRAMEWORK Qt5PrintSupport PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_CONCURRENT_FRAMEWORK Qt5Concurrent PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_SQL_FRAMEWORK Qt5Sql PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_XML_FRAMEWORK Qt5Xml PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_OPENGL_FRAMEWORK Qt5OpenGL PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
    find_library(QT_TEST_FRAMEWORK Qt5Test PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
else()
    find_library(QT_CORE_FRAMEWORK QtCore PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_GUI_FRAMEWORK QtGui PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_WIDGETS_FRAMEWORK QtWidgets PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_PRINTSUPPORT_FRAMEWORK QtPrintSupport PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_CONCURRENT_FRAMEWORK QtConcurrent PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_SQL_FRAMEWORK QtSql PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_XML_FRAMEWORK QtXml PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_OPENGL_FRAMEWORK QtOpenGL PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
    find_library(QT_TEST_FRAMEWORK QtTest PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
endif()

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
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

# Add Qt framework search path
if(APPLE)
    set(CMAKE_FRAMEWORK_PATH ${CMAKE_FRAMEWORK_PATH} "${QTDIR}/lib")
endif()

# Detect system and architecture
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/arm64_mac_clang_64")
        add_definitions(-D__MAC__ -D__EA64__ -D__ARM__ -D__QT__)
    else()
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_mac_clang_64")
        add_definitions(-D__MAC__ -D__EA64__ -D__QT__)
    endif()
    set(PLUGIN_EXT ".dylib")

elseif(UNIX AND NOT APPLE)
    set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_linux_gcc_64")
    add_definitions(-D__LINUX__ -D__EA64__ -D__QT__)
    set(PLUGIN_EXT ".so")

elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_win_vc_64")
    else()
        set(IDA_LIBDIR "${IDASDK_PATH}/lib/x64_win_vc_32")
    endif()
    add_definitions(-D__NT__ -D__EA64__)
    set(PLUGIN_EXT ".dll")
endif()

# Include directories
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}  # For MOC generated files
)

# Include IDA SDK headers as SYSTEM to suppress warnings
include_directories(SYSTEM
        ${IDASDK_PATH}/include
        ${IDA_PLUGINS_PATH}/hexrays_sdk/include
)

# Qt include directories (as SYSTEM to suppress warnings) - platform specific
if(APPLE)
    include_directories(SYSTEM
            "${QTDIR}/lib/QtCore.framework/Headers"
            "${QTDIR}/lib/QtGui.framework/Headers"
            "${QTDIR}/lib/QtWidgets.framework/Headers"
            "${QTDIR}/lib/QtPrintSupport.framework/Headers"
            "${QTDIR}/lib/QtConcurrent.framework/Headers"
            "${QTDIR}/lib/QtSql.framework/Headers"
            "${QTDIR}/lib/QtXml.framework/Headers"
            "${QTDIR}/lib/QtOpenGL.framework/Headers"
    )
elseif(UNIX AND NOT APPLE)
    include_directories(SYSTEM
            "${QTDIR}/build/qtbase/include"
            "${QTDIR}/build/qtbase/include/QtCore"
            "${QTDIR}/build/qtbase/include/QtGui"
            "${QTDIR}/build/qtbase/include/QtWidgets"
            "${QTDIR}/build/qtbase/include/QtPrintSupport"
            "${QTDIR}/build/qtbase/include/QtConcurrent"
            "${QTDIR}/build/qtbase/include/QtSql"
            "${QTDIR}/build/qtbase/include/QtXml"
            "${QTDIR}/build/qtbase/include/QtOpenGL"
    )
else()
    include_directories(SYSTEM
            "${QTDIR}/lib/QtCore.framework/Headers"
            "${QTDIR}/lib/QtGui.framework/Headers"
            "${QTDIR}/lib/QtWidgets.framework/Headers"
            "${QTDIR}/lib/QtPrintSupport.framework/Headers"
            "${QTDIR}/lib/QtConcurrent.framework/Headers"
            "${QTDIR}/lib/QtSql.framework/Headers"
            "${QTDIR}/lib/QtXml.framework/Headers"
            "${QTDIR}/lib/QtOpenGL.framework/Headers"
    )
endif()

# Find IDA libraries - platform specific names
if(APPLE)
    find_library(IDA64
            NAMES ida64
            PATHS ${IDA_LIBDIR}
            NO_DEFAULT_PATH
    )
elseif(UNIX AND NOT APPLE)
    find_library(IDA64
            NAMES ida64 libida64
            PATHS ${IDA_LIBDIR}
            NO_DEFAULT_PATH
    )
else()
    find_library(IDA64
            NAMES ida64
            PATHS ${IDA_LIBDIR}
            NO_DEFAULT_PATH
    )
endif()

if(NOT IDA64)
    message(FATAL_ERROR "Could not find IDA libraries in ${IDA_LIBDIR}")
endif()

# Find required packages
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# Find SQLite3 for IRC server
find_package(SQLite3 REQUIRED)

# LIEF for binary modification (used in code injection)
include(FetchContent)
FetchContent_Declare(
    LIEF
    GIT_REPOSITORY https://github.com/lief-project/LIEF
    GIT_TAG 0.14.1  # Use a stable release
)

# Configure LIEF build options
set(LIEF_DOC OFF)
set(LIEF_PYTHON_API OFF)
set(LIEF_EXAMPLES OFF)
set(LIEF_TESTS OFF)
set(LIEF_PE ON)      # Enable PE support for Windows binaries
set(LIEF_ELF ON)     # Enable ELF support for Linux binaries
set(LIEF_MACHO ON)   # Enable Mach-O support for macOS binaries

FetchContent_MakeAvailable(LIEF)

# Include Keystone build configuration
include(cmake/BuildKeystone.cmake)

# Common source files (shared between plugins)
set(COMMON_SOURCES
        core/ida_utils.cpp
        core/config.cpp
        sdk/client/client.cpp
        sdk/auth/oauth_manager.cpp
        sdk/auth/oauth_flow.cpp
        sdk/auth/oauth_authorizer.cpp
        analysis/actions.cpp
        analysis/deep_analysis.cpp
        analysis/memory.cpp
        agent/grader.cpp
        patching/patch_manager.cpp
        patching/code_injection_manager.cpp
        irc/irc_server.cpp
        irc/irc_client.cpp
        orchestrator/orchestrator_logger.cpp
)

# Unified plugin sources - combines all functionality
set(PLUGIN_SOURCES
        plugin.cpp  # New unified plugin entry point
        # Orchestrator components
        orchestrator/orchestrator.cpp
        orchestrator/database_manager.cpp
        orchestrator/agent_spawner.cpp
        orchestrator/tool_call_tracker.cpp
        orchestrator/merge_manager.cpp
        orchestrator/nogo_zone_manager.cpp
        # Agent components
        agent/swarm_agent.cpp
        agent/swarm_logger.cpp
        agent/conflict_detector.cpp
        agent/consensus_executor.cpp
        # UI components
        ui/orchestrator_ui.cpp
        ui/preferences_dialog.cpp
        ui/orchestrator_worker.cpp
        ui/ui_orchestrator_bridge.cpp
        ui/log_window.cpp
        ui/activity_feed_panel.cpp
        ui/patch_tracker.cpp
        # Common sources
        ${COMMON_SOURCES}
)

# Header files
set(HEADERS
        core/common_base.h
        core/common.h
        core/ida_utils.h
        core/ida_validators.h
        core/config.h
        core/types.h
        sdk/common.h
        sdk/claude_sdk.h
        sdk/client/client.h
        sdk/messages/types.h
        sdk/tools/registry.h
        sdk/auth/oauth_manager.h
        sdk/auth/oauth_flow.h
        sdk/auth/oauth_authorizer.h
        sdk/usage/pricing.h
        sdk/usage/stats.h
        analysis/actions.h
        analysis/deep_analysis.h
        analysis/memory.h
        patching/patch_manager.h
        patching/code_injection_manager.h
        agent/agent.h
        agent/tool_system.h
        agent/grader.h
        agent/event_bus.h
        # IRC headers
        irc/irc_server.h
        irc/irc_client.h
        # UI headers
        ui/orchestrator_ui.h
        ui/preferences_dialog.h
        ui/orchestrator_worker.h
        ui/ui_orchestrator_bridge.h
        ui/ui_integration.h
        ui/log_window.h
        # Orchestrator headers
        orchestrator/orchestrator.h
)

# Create unified plugin
add_library(ida_swarm64 SHARED
        ${PLUGIN_SOURCES}
        ${HEADERS}
)

# Set unified plugin properties
set_target_properties(ida_swarm64 PROPERTIES
        OUTPUT_NAME "ida_swarm64"
        PREFIX ""
        SUFFIX ${PLUGIN_EXT}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Common libraries
set(COMMON_LIBS
        ${IDA64}
        CURL::libcurl
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
        keystone
        SQLite::SQLite3
        LIEF::LIEF
)

# Link libraries for unified plugin
target_link_libraries(ida_swarm64
        ${COMMON_LIBS}
        ${QT_CORE_FRAMEWORK}
        ${QT_GUI_FRAMEWORK}
        ${QT_WIDGETS_FRAMEWORK}
)

# Post-build: Copy unified plugin to IDA plugins directory
add_custom_command(TARGET ida_swarm64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:ida_swarm64>
        "${IDA_PLUGINS_PATH}/"
        COMMENT "Copying unified plugin to IDA plugins directory"
)

# MCP Server executable
set(MCP_SERVER_SOURCES
        mcp_server/src/mcp_server.cpp
        mcp_server/src/session_manager.cpp
        mcp_server/src/stdio_mcp_server.cpp
        # Reuse orchestrator components
        orchestrator/orchestrator.cpp
        orchestrator/database_manager.cpp
        orchestrator/agent_spawner.cpp
        orchestrator/tool_call_tracker.cpp
        orchestrator/merge_manager.cpp
        orchestrator/nogo_zone_manager.cpp
        orchestrator/orchestrator_logger.cpp
        # Common sources needed
        core/ida_utils.cpp
        core/config.cpp
        sdk/client/client.cpp
        sdk/auth/oauth_manager.cpp
        sdk/auth/oauth_flow.cpp
        sdk/auth/oauth_authorizer.cpp
        analysis/actions.cpp
        analysis/deep_analysis.cpp
        analysis/memory.cpp
        agent/grader.cpp
        agent/consensus_executor.cpp
        patching/patch_manager.cpp
        patching/code_injection_manager.cpp
        irc/irc_server.cpp
        irc/irc_client.cpp
)

add_executable(ida_swarm_mcp ${MCP_SERVER_SOURCES})

# Include directories for MCP server
target_include_directories(ida_swarm_mcp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mcp_server/include
)

# Include IDA SDK headers as SYSTEM for MCP server
target_include_directories(ida_swarm_mcp SYSTEM PRIVATE
        ${IDASDK_PATH}/include
        ${IDA_PLUGINS_PATH}/hexrays_sdk/include
)

# Link libraries for MCP server
target_link_libraries(ida_swarm_mcp
        ${IDA64}
        CURL::libcurl
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
        keystone
        SQLite::SQLite3
        LIEF::LIEF
)

# Installation
install(TARGETS ida_swarm64
        RUNTIME DESTINATION ${IDA_PLUGINS_PATH}
        LIBRARY DESTINATION ${IDA_PLUGINS_PATH}
)

# Debug information
message(STATUS "IDA SDK Path: ${IDASDK_PATH}")
message(STATUS "IDA Plugins Path: ${IDA_PLUGINS_PATH}")
message(STATUS "Qt Directory: ${QTDIR}")
message(STATUS "IDA Library Directory: ${IDA_LIBDIR}")
message(STATUS "Plugin Extension: ${PLUGIN_EXT}")
