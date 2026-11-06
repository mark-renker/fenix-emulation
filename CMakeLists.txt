# Synton Fénix I — circuit-level software emulation (AU / VST3 / CLAP + headless harness)
cmake_minimum_required(VERSION 3.22)
project(FenixI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # universal by default (release installers); -DFENIX_ARCHS=arm64 for a faster native-only dev build
  set(FENIX_ARCHS "arm64;x86_64" CACHE STRING "macOS architectures to build")
  set(CMAKE_OSX_ARCHITECTURES "${FENIX_ARCHS}" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

option(FENIX_BUILD_PLUGINS "Build the JUCE plugin targets" ON)
option(FENIX_BUILD_CLAP "Build the CLAP format via clap-juce-extensions" ON)

# ---- headless harness (no JUCE) --------------------------------------------
add_executable(fenix_harness tests/Harness.cpp)
if(NOT MSVC)
  target_compile_options(fenix_harness PRIVATE -Wall -Wextra)
endif()
enable_testing()
add_test(NAME harness COMMAND fenix_harness --quick --report ${CMAKE_BINARY_DIR}/fenix_report.json)

if(NOT FENIX_BUILD_PLUGINS)
  return()
endif()

# ---- JUCE: local checkout when present, otherwise fetch 8.0.6 --------------
include(FetchContent)
if(DEFINED FENIX_JUCE_PATH AND EXISTS "${FENIX_JUCE_PATH}/CMakeLists.txt")
  add_subdirectory("${FENIX_JUCE_PATH}" JUCE)
elseif(EXISTS "$ENV{HOME}/JUCE/CMakeLists.txt")
  message(STATUS "Using local JUCE at $ENV{HOME}/JUCE")
  add_subdirectory("$ENV{HOME}/JUCE" JUCE)
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

set(FENIX_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FENIX_FORMATS AU)
endif()

if(FENIX_BUILD_CLAP)
  FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG main          # the tagged releases predate JUCE 8; main tracks it
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(clap-juce-extensions)
endif()

function(fenix_common target)
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(${target} PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endfunction()

# ---- Instrument: Fénix I (aumu) --------------------------------------------
juce_add_plugin(FenixI
  PRODUCT_NAME "Fenix I"
  COMPANY_NAME "Renky Denk"
  BUNDLE_ID com.renkydenk.fenixi
  PLUGIN_MANUFACTURER_CODE Rnky
  PLUGIN_CODE Fnx1
  FORMATS ${FENIX_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(FenixI PRIVATE src/plugin/PluginProcessor.cpp)
target_compile_definitions(FenixI PRIVATE FENIX_FX=0)
fenix_common(FenixI)

# ---- Effect variant: Fénix I FX (aumf) — the two 1/4" audio inputs + rear CV bus
juce_add_plugin(FenixIFX
  PRODUCT_NAME "Fenix I FX"
  COMPANY_NAME "Renky Denk"
  BUNDLE_ID com.renkydenk.fenixifx
  PLUGIN_MANUFACTURER_CODE Rnky
  PLUGIN_CODE FnxF
  FORMATS ${FENIX_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(FenixIFX PRIVATE src/plugin/PluginProcessor.cpp)
target_compile_definitions(FenixIFX PRIVATE FENIX_FX=1)
fenix_common(FenixIFX)

if(FENIX_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET FenixI
    CLAP_ID "com.renkydenk.fenixi"
    CLAP_FEATURES instrument synthesizer modular)
  clap_juce_extensions_plugin(TARGET FenixIFX
    CLAP_ID "com.renkydenk.fenixifx"
    CLAP_FEATURES audio-effect modular)
endif()

# ---- UI snapshot tool (renders the editor to PNG, no host needed) -----------
juce_add_console_app(fenix_snapshot PRODUCT_NAME "fenix_snapshot")
target_sources(fenix_snapshot PRIVATE tools/Snapshot.cpp src/plugin/PluginProcessor.cpp)
target_compile_definitions(fenix_snapshot PRIVATE FENIX_FX=0 JUCE_MODAL_LOOPS_PERMITTED=1)
fenix_common(fenix_snapshot)
target_link_libraries(fenix_snapshot PRIVATE juce::juce_gui_extra)
