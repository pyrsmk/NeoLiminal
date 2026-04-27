cmake_minimum_required(VERSION 3.22)
project(LiminalShit VERSION 0.1)

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

juce_add_plugin(LiminalShit
    COMPANY_NAME                "TheOrigamist"
    COMPANY_WEBSITE             ""
    PLUGIN_MANUFACTURER_CODE    Orgm
    PLUGIN_CODE                 Lmnl
    BUNDLE_ID                   "com.theorigamist.liminal_shit"
    PRODUCT_NAME                "LiminalShit"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS                     VST3 AU
)

juce_generate_juce_header(LiminalShit)

target_sources(LiminalShit
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

target_compile_definitions(LiminalShit
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LiminalShit
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
