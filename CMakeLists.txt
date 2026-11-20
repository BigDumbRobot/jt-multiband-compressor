cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(JTMultibandCompressor VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is downloaded at configure time. To reuse a local copy instead, pass
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(JUCE)

# AAX is built from the SDK bundled with JUCE; unsigned, it loads in Pro Tools Developer only.
option(BDR_AAX "Build the AAX plugin" ON)
set(BDR_FORMATS VST3)
if(BDR_AAX)
    list(APPEND BDR_FORMATS AAX)
endif()
if(APPLE)
    list(APPEND BDR_FORMATS AU)
endif()

# The title font (Russo One, SIL Open Font License: assets/fonts/RussoOne-OFL.txt).
juce_add_binary_data(JTMultibandAssets SOURCES assets/fonts/RussoOne-Regular.ttf)

juce_add_plugin(JTMultibandCompressor
    PRODUCT_NAME "JT Multiband Compressor"
    COMPANY_NAME "Big Dumb Robot"
    DESCRIPTION "Three-band multiband compressor with soft clip, limiter and 32x oversampling"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/plugin-icon.png"
    BUNDLE_ID com.bigdumbrobot.jtmultibandcompressor
    PLUGIN_MANUFACTURER_CODE Bdrb
    PLUGIN_CODE Jtfm
    FORMATS ${BDR_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    AAX_CATEGORY Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(JTMultibandCompressor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(JTMultibandCompressor PRIVATE Source)
# The reference arithmetic multiplies and adds separately; clang would otherwise fuse a*b + c (arm64
# default), which changes rounding enough to flip the compressor's and limiter's decisions.
target_compile_options(JTMultibandCompressor PRIVATE -ffp-contract=off)
target_compile_definitions(JTMultibandCompressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(JTMultibandCompressor
    PRIVATE
        JTMultibandAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Renders the plugin icon (assets/icons/plugin-icon.png) and an editor screenshot:
#   JTRender <icon.png> <screenshot.png> [preset]
juce_add_console_app(JTRender PRODUCT_NAME "JTRender")
target_sources(JTRender PRIVATE Tools/Render.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(JTRender PRIVATE Source)
target_compile_definitions(JTRender PRIVATE
    JucePlugin_Name="JT Multiband Compressor" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(JTRender PRIVATE
    JTMultibandAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

# Crossover design printout, rate comparisons and CPU benchmarks.
juce_add_console_app(JTXoverCheck PRODUCT_NAME "JTXoverCheck")
target_sources(JTXoverCheck PRIVATE Tools/XoverCheck.cpp)
target_include_directories(JTXoverCheck PRIVATE Source)
target_compile_definitions(JTXoverCheck PRIVATE BDR_BENCH=1)
target_compile_options(JTXoverCheck PRIVATE -ffp-contract=off)
target_link_libraries(JTXoverCheck PRIVATE juce::juce_core juce::juce_recommended_config_flags juce::juce_recommended_lto_flags "-framework Accelerate")
