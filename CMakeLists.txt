cmake_minimum_required(VERSION 3.16)
project(ida_llm_agent)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# IDA SDK path - platform specific
if(APPLE)
    set(IDASDK_PATH "/Users/user/Documents/idasdk90" CACHE PATH "Path to IDA SDK")
elseif(UNIX AND NOT APPLE)
    set(IDASDK_PATH "/home/one/Documents/idasdk90" CACHE PATH "Path to IDA SDK")
else()
    set(IDASDK_PATH "/Users/user/Documents/idasdk90" CACHE PATH "Path to IDA SDK")
endif()

# IDA Pro plugins directory path - platform specific
if(APPLE)
    set(IDA_PLUGINS_PATH "/Applications/IDA Professional 9.0.app/Contents/MacOS/plugins" CACHE PATH "Path to IDA Pro plugins directory")
elseif(UNIX AND NOT APPLE)
    set(IDA_PLUGINS_PATH "/home/one/idapro-9.0/plugins" CACHE PATH "Path to IDA Pro plugins directory")
else()
    set(IDA_PLUGINS_PATH "/Applications/IDA Professional 9.0.app/Contents/MacOS/plugins" CACHE PATH "Path to IDA Pro plugins directory")
endif()

# IDA Qt build setup - platform specific
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
    find_library(QT_NETWORK_FRAMEWORK QtNetwork PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
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
    find_library(QT_NETWORK_FRAMEWORK Qt5Network PATHS "${QTDIR}/build/qtbase/lib" NO_DEFAULT_PATH)
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
    find_library(QT_NETWORK_FRAMEWORK QtNetwork PATHS "${QTDIR}/lib" NO_DEFAULT_PATH)
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
            "${QTDIR}/lib/QtNetwork.framework/Headers"
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
            "${QTDIR}/build/qtbase/include/QtNetwork"
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
            "${QTDIR}/lib/QtNetwork.framework/Headers"
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
        irc/irc_server.cpp
        irc/irc_client.cpp
        orchestrator/orchestrator_logger.cpp
)

# Orchestrator plugin sources
set(ORCHESTRATOR_SOURCES
        orchestrator/orchestrator_plugin.cpp
        orchestrator/orchestrator.cpp
        orchestrator/database_manager.cpp
        orchestrator/agent_spawner.cpp
        orchestrator/tool_call_tracker.cpp
        orchestrator/merge_manager.cpp
        ${COMMON_SOURCES}
)

# Agent plugin sources
set(AGENT_SOURCES
        agent/agent_plugin.cpp
        agent/swarm_agent.cpp
        agent/conflict_detector.cpp
        orchestrator/tool_call_tracker.cpp  # Shared for conflict detection
        ${COMMON_SOURCES}
)

# Original plugin sources
set(SOURCES
        core/plugin.cpp
        ${COMMON_SOURCES}
        # ui_v2 core
        ui_v2/core/base_styled_widget.cpp
        ui_v2/core/theme_manager.cpp
        ui_v2/core/theme_templates.cpp
        ui_v2/core/theme_transition_manager.cpp
        ui_v2/core/theme_undo_manager.cpp
        ui_v2/core/effects_manager.cpp
        ui_v2/core/animation_manager.cpp
        ui_v2/core/focus_manager.cpp
        ui_v2/core/ui_utils.cpp
        ui_v2/core/agent_controller.cpp
        ui_v2/core/settings_manager.cpp
        ui_v2/core/color_constants.cpp
        # ui_v2 models
        ui_v2/models/conversation_model.cpp
        # ui_v2 views
        ui_v2/views/main_window.cpp
        ui_v2/views/conversation_view.cpp
        ui_v2/views/memory_dock.cpp
        ui_v2/views/tool_execution_dock.cpp
        ui_v2/views/console_dock.cpp
        ui_v2/views/settings_dialog.cpp
        # ui_v2 theme editor
        ui_v2/views/theme_editor/theme_editor_dialog.cpp
        ui_v2/views/theme_editor/widgets/accessibility_tools.cpp
        ui_v2/views/theme_editor/widgets/animation_config_widget.cpp
        ui_v2/views/theme_editor/widgets/chart_theme_widget.cpp
        ui_v2/views/theme_editor/widgets/color_picker_widget.cpp
        ui_v2/views/theme_editor/widgets/effects_config_widget.cpp
        ui_v2/views/theme_editor/widgets/theme_preview_widget.cpp
        ui_v2/views/theme_editor/widgets/theme_template_selector.cpp
        ui_v2/views/theme_editor/widgets/theme_save_as_dialog.cpp
        # ui_v2 widgets
        ui_v2/widgets/message_bubble.cpp
        ui_v2/widgets/message_group.cpp
        ui_v2/widgets/markdown_viewer.cpp
        # ui_v2 custom charts
        ui_v2/widgets/charts/chart_types.cpp
        ui_v2/widgets/charts/custom_chart_base.cpp
        ui_v2/widgets/charts/chart_theme.cpp
        ui_v2/widgets/charts/line_chart.cpp
        ui_v2/widgets/charts/circular_chart.cpp
        ui_v2/widgets/charts/bar_chart.cpp
        ui_v2/widgets/charts/heatmap_widget.cpp
        ui_v2/widgets/charts/sparkline_widget.cpp
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
        agent/agent.h
        agent/tool_system.h
        agent/grader.h
        # IRC headers
        irc/irc_server.h
        irc/irc_client.h
        # ui_v2 core
        ui_v2/core/base_styled_widget.h
        ui_v2/core/theme_manager.h
        ui_v2/core/theme_constants.h
        ui_v2/core/theme_transition_manager.h
        ui_v2/core/theme_undo_manager.h
        ui_v2/core/effects_manager.h
        ui_v2/core/animation_manager.h
        ui_v2/core/focus_manager.h
        ui_v2/core/ui_constants.h
        ui_v2/core/ui_utils.h
        ui_v2/core/agent_controller.h
        ui_v2/core/settings_manager.h
        ui_v2/core/ui_v2_common.h
        ui_v2/core/color_constants.h
        # ui_v2 models
        ui_v2/models/conversation_model.h
        ui_v2/models/tool_execution.h
        # ui_v2 views
        ui_v2/views/main_window.h
        ui_v2/views/conversation_view.h
        ui_v2/views/memory_dock.h
        ui_v2/views/tool_execution_dock.h
        ui_v2/views/settings_dialog.h
        # ui_v2 theme editor
        ui_v2/views/theme_editor/theme_editor_dialog.h
        ui_v2/views/theme_editor/widgets/accessibility_tools.h
        ui_v2/views/theme_editor/widgets/animation_config_widget.h
        ui_v2/views/theme_editor/widgets/chart_theme_widget.h
        ui_v2/views/theme_editor/widgets/color_picker_widget.h
        ui_v2/views/theme_editor/widgets/effects_config_widget.h
        ui_v2/views/theme_editor/widgets/theme_preview_widget.h
        ui_v2/views/theme_editor/widgets/theme_template_selector.h
        ui_v2/views/theme_editor/widgets/theme_save_as_dialog.h
        # ui_v2 widgets
        ui_v2/widgets/message_bubble.h
        ui_v2/widgets/message_group.h
        ui_v2/widgets/markdown_viewer.h
        # ui_v2 custom charts
        ui_v2/widgets/charts/chart_types.h
        ui_v2/widgets/charts/custom_chart_base.h
        ui_v2/widgets/charts/chart_theme.h
        ui_v2/widgets/charts/line_chart.h
        ui_v2/widgets/charts/circular_chart.h
        ui_v2/widgets/charts/bar_chart.h
        ui_v2/widgets/charts/heatmap_widget.h
        ui_v2/widgets/charts/sparkline_widget.h
)

# Create orchestrator plugin (no UI headers needed)
add_library(ida_orchestrator64 SHARED
        ${ORCHESTRATOR_SOURCES}
)

# Create agent plugin (no UI headers needed)
add_library(ida_agent64 SHARED
        ${AGENT_SOURCES}
)

# Original plugin (for backward compatibility)
add_library(ida_llm_agent64 SHARED
        ${SOURCES}
        ${HEADERS}
)

# Set orchestrator plugin properties
set_target_properties(ida_orchestrator64 PROPERTIES
        OUTPUT_NAME "ida_orchestrator64"
        PREFIX ""
        SUFFIX ${PLUGIN_EXT}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Set agent plugin properties
set_target_properties(ida_agent64 PROPERTIES
        OUTPUT_NAME "ida_agent64"
        PREFIX ""
        SUFFIX ${PLUGIN_EXT}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Set original plugin properties
set_target_properties(ida_llm_agent64 PROPERTIES
        OUTPUT_NAME "ida_llm_agent64"
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
)

# Link libraries for orchestrator plugin
target_link_libraries(ida_orchestrator64
        ${COMMON_LIBS}
        ${QT_CORE_FRAMEWORK}
        ${QT_GUI_FRAMEWORK}
        ${QT_WIDGETS_FRAMEWORK}
        ${QT_NETWORK_FRAMEWORK}
)

# Link libraries for agent plugin
target_link_libraries(ida_agent64
        ${COMMON_LIBS}
        ${QT_CORE_FRAMEWORK}
        ${QT_NETWORK_FRAMEWORK}
)

# Link libraries for original plugin
target_link_libraries(ida_llm_agent64
        ${COMMON_LIBS}
        ${QT_CORE_FRAMEWORK}
        ${QT_GUI_FRAMEWORK}
        ${QT_WIDGETS_FRAMEWORK}
        ${QT_PRINTSUPPORT_FRAMEWORK}
        ${QT_NETWORK_FRAMEWORK}
        ${QT_CONCURRENT_FRAMEWORK}
        ${QT_SQL_FRAMEWORK}
        ${QT_XML_FRAMEWORK}
        ${QT_OPENGL_FRAMEWORK}
)

# Post-build: Copy plugins to IDA plugins directory
add_custom_command(TARGET ida_orchestrator64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:ida_orchestrator64>
        "${IDA_PLUGINS_PATH}/"
        COMMENT "Copying orchestrator plugin to IDA plugins directory"
)

add_custom_command(TARGET ida_agent64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:ida_agent64>
        "${IDA_PLUGINS_PATH}/"
        COMMENT "Copying agent plugin to IDA plugins directory"
)

add_custom_command(TARGET ida_llm_agent64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:ida_llm_agent64>
        "${IDA_PLUGINS_PATH}/"
        COMMENT "Copying original plugin to IDA plugins directory"
)
# put llm_re_config.json into IDA_PLUGINS_PATH dir

# Custom target to build all plugins at once
add_custom_target(all_plugins
    DEPENDS ida_orchestrator64 ida_agent64 ida_llm_agent64
    COMMENT "Building all IDA plugins"
)

# Installation
install(TARGETS ida_orchestrator64 ida_agent64 ida_llm_agent64
        RUNTIME DESTINATION ${IDA_PLUGINS_PATH}
        LIBRARY DESTINATION ${IDA_PLUGINS_PATH}
)

# Debug information
message(STATUS "IDA SDK Path: ${IDASDK_PATH}")
message(STATUS "IDA Plugins Path: ${IDA_PLUGINS_PATH}")
message(STATUS "Qt Directory: ${QTDIR}")
message(STATUS "IDA Library Directory: ${IDA_LIBDIR}")
message(STATUS "Plugin Extension: ${PLUGIN_EXT}")
