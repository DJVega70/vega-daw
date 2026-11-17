cmake_minimum_required(VERSION 3.22)
project(VegaDAW VERSION 0.2.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Tracktion Engine (GPLv3) + its bundled JUCE. Pass -DTE_DIR=<path to a tracktion_engine checkout>.
if (NOT DEFINED TE_DIR)
    message(FATAL_ERROR "Set TE_DIR to a tracktion_engine checkout (with its modules/juce submodule)")
endif()
add_subdirectory(${TE_DIR}/modules/juce juce_build)
add_subdirectory(${TE_DIR}/modules tracktion_build)

option(VEGA_ASIO "Build with ASIO support (needs ASIO_SDK_DIR)" OFF)

juce_add_gui_app(VegaDAW
    PRODUCT_NAME "Vega Productions DAW"
    COMPANY_NAME "Vega Productions"
    BUNDLE_ID com.vegaproductions.daw
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Recording audio"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE
    DOCUMENT_EXTENSIONS vegaproj)

target_sources(VegaDAW PRIVATE
    Source/Main.cpp
    Source/DawSession.cpp
    Source/ArrangeView.cpp
    Source/PianoRoll.cpp
    Source/MixerView.cpp
    Source/HuiSurface.cpp
    Source/Controllers.cpp)

target_compile_definitions(VegaDAW PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="Vega Productions DAW"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_ENABLE_CONTROL_SURFACES=1)

if (VEGA_ASIO AND WIN32)
    target_include_directories(VegaDAW PRIVATE ${ASIO_SDK_DIR}/common)
    target_compile_definitions(VegaDAW PRIVATE JUCE_ASIO=1)
endif()

target_link_libraries(VegaDAW PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(VegaDAW PRIVATE "-fno-aligned-allocation")
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(VegaDAW PRIVATE "-latomic")
endif()

# headless self-test (engine round trips: tracks, clips, routing, render)
option(VEGA_TESTS "Build the headless test app" OFF)
if (VEGA_TESTS)
    juce_add_console_app(VegaDAWTest PRODUCT_NAME "VegaDAWTest")
    target_sources(VegaDAWTest PRIVATE tests/DawTest.cpp Source/DawSession.cpp Source/HuiSurface.cpp)
    target_compile_definitions(VegaDAWTest PRIVATE
        JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1 TRACKTION_ENABLE_CONTROL_SURFACES=1 VEGA_HEADLESS=1)
    target_link_libraries(VegaDAWTest PRIVATE tracktion::tracktion_core tracktion::tracktion_engine tracktion::tracktion_graph
        juce::juce_audio_devices juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_extra juce::juce_recommended_config_flags)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(VegaDAWTest PRIVATE "-latomic")
    endif()
endif()
