cmake_minimum_required(VERSION 3.22)
project(Mindbass VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(MB_FORMATS AU VST3 Standalone)
else()
    set(MB_FORMATS VST3 Standalone)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Instrumento de bajos: toca su propio patrón de 4 compases (sub propio) y acepta notas MIDI.
juce_add_plugin(Mindbass
    COMPANY_NAME "Dj Nane"
    BUNDLE_ID com.djnane.mindbass
    PLUGIN_MANUFACTURER_CODE DjNa
    PLUGIN_CODE MnBs
    FORMATS ${MB_FORMATS}
    NEEDS_WEB_BROWSER TRUE
    PRODUCT_NAME "MIND BASS"
    PLUGIN_NAME "MIND BASS"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth)

# ui/index.html se genera con tools/build_ui.py (skin común + fuentes + marco embebidos)
juce_add_binary_data(MbData SOURCES ui/index.html ui/frame.jpg common/Nunito-ExtraBold.ttf)

target_sources(Mindbass PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BassEngine.cpp
    common/DragBar.cpp)

target_include_directories(Mindbass PRIVATE common)

target_compile_definitions(Mindbass PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Mindbass
    PRIVATE
        MbData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Pruebas (solo Linux/CI): cmake -DMB_TESTS=ON
option(MB_TESTS "Build engine tests" OFF)
if(MB_TESTS)
    juce_add_console_app(MbEngineTest PRODUCT_NAME "MbEngineTest")
    target_sources(MbEngineTest PRIVATE tests/engine_test.cpp Source/BassEngine.cpp)
    target_compile_definitions(MbEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MbEngineTest PRIVATE juce::juce_audio_basics juce::juce_events juce::juce_recommended_config_flags)

    add_executable(MbProcessorTest tests/processor_test.cpp)
    target_link_libraries(MbProcessorTest PRIVATE Mindbass)
    target_include_directories(MbProcessorTest PRIVATE $<TARGET_PROPERTY:Mindbass,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MbProcessorTest PRIVATE $<TARGET_PROPERTY:Mindbass,COMPILE_DEFINITIONS>)
endif()
