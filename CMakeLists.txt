cmake_minimum_required(VERSION 3.16)
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
add_definitions(-DQT_NAMESPACE=QT)  # SUPER important

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
    add_definitions(-D__LINUX__ -D__EA64__)
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
        ${IDASDK_PATH}/include
        ${IDA_PLUGINS_PATH}/hexrays_sdk/include
        ${CMAKE_CURRENT_BINARY_DIR}  # For MOC generated files
)

# Qt include directories
include_directories(
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

# Find IDA libraries
find_library(IDA64
        NAMES ida64
        PATHS ${IDA_LIBDIR}
        NO_DEFAULT_PATH
)

if(NOT IDA64)
    message(FATAL_ERROR "Could not find IDA libraries in ${IDA_LIBDIR}")
endif()

# Find required packages
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Include Keystone build configuration
include(cmake/BuildKeystone.cmake)

# Source files
set(SOURCES
        core/plugin.cpp
        core/ida_utils.cpp
        core/config.cpp
        api/anthropic_api.cpp
        analysis/actions.cpp
        analysis/deep_analysis.cpp
        analysis/memory.cpp
        patching/patch_manager.cpp
        # ui_v2 core
        ui_v2/core/base_styled_widget.cpp
        ui_v2/core/theme_manager.cpp
        ui_v2/core/focus_manager.cpp
        ui_v2/core/ui_utils.cpp
        ui_v2/core/agent_controller.cpp
        ui_v2/core/settings_manager.cpp
        # ui_v2 models
        ui_v2/models/conversation_model.cpp
        # ui_v2 views
        ui_v2/views/main_window.cpp
        ui_v2/views/conversation_view.cpp
        ui_v2/views/memory_dock.cpp
        ui_v2/views/tool_execution_dock.cpp
        ui_v2/views/statistics_dock.cpp
        ui_v2/views/floating_inspector.cpp
        ui_v2/views/settings_dialog.cpp
        # ui_v2 widgets
        ui_v2/widgets/message_bubble.cpp
        ui_v2/widgets/message_group.cpp
        ui_v2/widgets/markdown_viewer.cpp
        ui_v2/widgets/command_palette.cpp
        ui_v2/widgets/tool_execution_visualizer.cpp
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
        api/anthropic_api.h
        api/message_types.h
        analysis/actions.h
        analysis/deep_analysis.h
        analysis/memory.h
        patching/patch_manager.h
        agent/agent.h
        agent/tool_system.h
        # ui_v2 core
        ui_v2/core/base_styled_widget.h
        ui_v2/core/theme_manager.h
        ui_v2/core/focus_manager.h
        ui_v2/core/ui_constants.h
        ui_v2/core/ui_utils.h
        ui_v2/core/agent_controller.h
        ui_v2/core/settings_manager.h
        ui_v2/core/ui_v2_common.h
        # ui_v2 models
        ui_v2/models/conversation_model.h
        ui_v2/models/tool_execution.h
        # ui_v2 views
        ui_v2/views/main_window.h
        ui_v2/views/conversation_view.h
        ui_v2/views/memory_dock.h
        ui_v2/views/tool_execution_dock.h
        ui_v2/views/statistics_dock.h
        ui_v2/views/floating_inspector.h
        ui_v2/views/settings_dialog.h
        # ui_v2 widgets
        ui_v2/widgets/message_bubble.h
        ui_v2/widgets/message_group.h
        ui_v2/widgets/markdown_viewer.h
        ui_v2/widgets/command_palette.h
        ui_v2/widgets/tool_execution_visualizer.h
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

# Create shared library (end in "64", won't be loaded if it doesn't)
add_library(ida_llm_agent64 SHARED
        ${SOURCES}
        ${HEADERS}
)

# Set library properties
set_target_properties(ida_llm_agent64 PROPERTIES
        OUTPUT_NAME "ida_llm_agent64"
        PREFIX ""
        SUFFIX ${PLUGIN_EXT}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Link libraries including custom Qt frameworks
target_link_libraries(ida_llm_agent64
        ${IDA64}
        CURL::libcurl
        nlohmann_json::nlohmann_json
        keystone
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

# Post-build: Copy plugin to IDA plugins directory
add_custom_command(TARGET ida_llm_agent64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:ida_llm_agent64>
        "${IDA_PLUGINS_PATH}/"
        COMMENT "Copying plugin to IDA plugins directory"
)
# put llm_re_config.json into IDA_PLUGINS_PATH dir

# Installation
install(TARGETS ida_llm_agent64
        RUNTIME DESTINATION ${IDA_PLUGINS_PATH}
        LIBRARY DESTINATION ${IDA_PLUGINS_PATH}
)

# Debug information
message(STATUS "IDA SDK Path: ${IDASDK_PATH}")
message(STATUS "IDA Plugins Path: ${IDA_PLUGINS_PATH}")
message(STATUS "Qt Directory: ${QTDIR}")
message(STATUS "IDA Library Directory: ${IDA_LIBDIR}")
message(STATUS "Plugin Extension: ${PLUGIN_EXT}")