cmake_minimum_required(VERSION 3.22)
project(NeoLiminal VERSION 0.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

set_source_files_properties(
    "${juce_SOURCE_DIR}/modules/juce_gui_basics/juce_gui_basics.mm"
    PROPERTIES COMPILE_FLAGS "-Wno-unused-but-set-variable -Wno-switch-enum"
)

juce_add_plugin(NeoLiminal
    COMPANY_NAME                "TheOrigamist"
    COMPANY_WEBSITE             ""
    PLUGIN_MANUFACTURER_CODE    Orgm
    PLUGIN_CODE                 Lmnl
    BUNDLE_ID                   "com.theorigamist.neoliminal"
    PRODUCT_NAME                "NeoLiminal"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS                     VST3 AUv3 Standalone
)

juce_generate_juce_header(NeoLiminal)

target_sources(NeoLiminal
    PRIVATE
        Source/AppVersion.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DSP/FeedbackProcessor.cpp
        Source/DSP/ReverbProcessor.cpp
        Source/DSP/DatalossProcessor.cpp
        Source/DSP/TapeProcessor.cpp
)

set_source_files_properties(Source/AppVersion.cpp PROPERTIES
    COMPILE_DEFINITIONS "VERSION=\"${PROJECT_VERSION}\""
)

target_compile_definitions(NeoLiminal
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(NeoLiminal
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
