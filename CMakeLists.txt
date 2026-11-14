cmake_minimum_required(VERSION 3.22)

project(MeteoraNamCompressor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.11
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(METEORA_FORMATS VST3 AU)
else()
    set(METEORA_FORMATS VST3)
endif()

juce_add_plugin(MeteoraNamCompressor
    COMPANY_NAME                "Meteora Plugins"
    COMPANY_WEBSITE             "https://github.com/Meteorastudio85"
    BUNDLE_ID                   com.meteoraplugins.namcompressor
    PLUGIN_MANUFACTURER_CODE    Mtra
    PLUGIN_CODE                 MnBc
    FORMATS                     ${METEORA_FORMATS}
    PRODUCT_NAME                "Meteora NAM Bus Compressor"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

juce_add_binary_data(MeteoraNamCompressorData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES
        Resources/panel.png
        Resources/knob_threshold.png
        Resources/knob_ratio.png
        Resources/knob_attack.png
        Resources/knob_release.png
        Resources/knob_makeup.png
        Resources/knob_mix.png
        Resources/power_off.png
        Resources/led_on.png
        Resources/glass.png
        Resources/models/nocomp_L.nam
        Resources/models/nocomp_R.nam
        Resources/models/fast_r2_L.nam
        Resources/models/fast_r2_R.nam
        Resources/models/fast_r4_L.nam
        Resources/models/fast_r4_R.nam
        Resources/models/fast_r10_L.nam
        Resources/models/fast_r10_R.nam
        Resources/models/slow_r2_L.nam
        Resources/models/slow_r2_R.nam
        Resources/models/slow_r4_L.nam
        Resources/models/slow_r4_R.nam
        Resources/models/slow_r10_L.nam
        Resources/models/slow_r10_R.nam)

target_sources(MeteoraNamCompressor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ModelLibrary.cpp)

target_include_directories(MeteoraNamCompressor PRIVATE Source)

target_compile_definitions(MeteoraNamCompressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# The neural network is the heavy part: always optimise it fully.
if(MSVC)
    target_compile_options(MeteoraNamCompressor PRIVATE $<$<CONFIG:Release>:/O2 /fp:fast>)
else()
    target_compile_options(MeteoraNamCompressor PRIVATE $<$<CONFIG:Release>:-O3>)
endif()

target_link_libraries(MeteoraNamCompressor
    PRIVATE
        MeteoraNamCompressorData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
