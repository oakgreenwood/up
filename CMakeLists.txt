cmake_minimum_required(VERSION 3.24)

# Change to your project name
project(ulichperc VERSION 1.0.5)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_XCODE_GENERATE_SCHEME OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (APPLE)
    # Avoid host-only arm64 builds and host OS minimums leaking into the bundle.
    # This keeps releases discoverable on Intel Macs.
    option(ULICHPERC_MAC_BUILD_UNIVERSAL "Build universal macOS binaries (arm64 + x86_64)." ON)
    set(ULICHPERC_MAC_LEGACY_INTEL_MIN_OS "10.13" CACHE STRING "Minimum macOS target for x86_64-only builds.")

    if ("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
        if (ULICHPERC_MAC_BUILD_UNIVERSAL)
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS target architectures" FORCE)
        endif()
    endif()

    if ("${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
        set(_ULICHPERC_HAS_ARM64 FALSE)
        set(_ULICHPERC_HAS_X86_64 FALSE)
        foreach(_ULICHPERC_ARCH ${CMAKE_OSX_ARCHITECTURES})
            if (_ULICHPERC_ARCH STREQUAL "arm64")
                set(_ULICHPERC_HAS_ARM64 TRUE)
            endif()
            if (_ULICHPERC_ARCH STREQUAL "x86_64")
                set(_ULICHPERC_HAS_X86_64 TRUE)
            endif()
        endforeach()

        if (_ULICHPERC_HAS_ARM64)
            # arm64 macOS binaries require macOS 11.0+.
            set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS target version" FORCE)
        elseif(_ULICHPERC_HAS_X86_64)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "${ULICHPERC_MAC_LEGACY_INTEL_MIN_OS}" CACHE STRING "Minimum macOS target version" FORCE)
        endif()
    endif()

    message(STATUS "ULICHPERC macOS architectures: ${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "ULICHPERC macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

if (MSVC)
    # Keep optimized Release builds while emitting external PDB files for crash symbolication.
    add_compile_options("$<$<CONFIG:Release>:/Zi>")
    add_link_options("$<$<CONFIG:Release>:/DEBUG:FULL>")
endif()

# We're going to use CPM as our package manager to bring in JUCE
# Check to see if we have CPM installed already.  Bring it in if we don't.
set(CPM_DOWNLOAD_VERSION 0.34.0)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if (NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
endif ()

include(${CPM_DOWNLOAD_LOCATION})

# Bring in JUCE locally
CPMAddPackage(
    NAME juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 501c07674e1ad693085a7e7c398f205c2677f5da
)

# Make sure you include any new source files here
set(SourceFiles
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/UI/BinaryImageLoader.h
        Source/UI/BinaryImageLoader.cpp
        Source/UI/RotarySliderRenderer.h
        Source/UI/RotarySliderRenderer.cpp
        Source/UI/TempoSyncButtonRenderer.h
        Source/UI/TempoSyncButtonRenderer.cpp
        Source/UI/CustomLookAndFeel.h
        Source/UI/CustomLookAndFeel.cpp
        Source/UI/SampleGroupSelector.h
        Source/UI/SampleGroupSelector.cpp
        Source/Parameters/PluginParameters.h
        Source/Parameters/PluginParameters.cpp
        Source/Parameters/SampleSpecificParameterState.h
        Source/Parameters/SampleSpecificParameterState.cpp
        Source/Parameters/SampleSpecificRealtimeCache.h
        Source/Parameters/SampleSpecificRealtimeCache.cpp
        Source/Midi/MidiNoteActivityState.h
        Source/Midi/MidiNoteActivityState.cpp
        Source/Effects/RzhavProcessor.h
        Source/Effects/RzhavProcessor.cpp
        Source/SampleLibrary/SampleNameParser.h
        Source/SampleLibrary/SampleNameParser.cpp
        Source/SampleLibrary/PercussionSampleLibrary.h
        Source/SampleLibrary/PercussionSampleLibrary.cpp
        Source/Tempo/HostTempoTracker.h
        Source/Tempo/HostTempoTracker.cpp
        Source/Warp/WarpCachePrewarmer.h
        Source/Warp/WarpCachePrewarmer.cpp
        Source/Playback/NoteStartDeclicker.h
        Source/Playback/NoteStartDeclicker.cpp
        Source/Playback/PunchEnvelope.h
        Source/Playback/PunchEnvelope.cpp
        Source/Playback/RealtimeWarpPlayer.h
        Source/Playback/RealtimeWarpPlayer.cpp
        Source/Playback/SamplePlaybackRenderer.h
        Source/Playback/SamplePlaybackRenderer.cpp
        Source/Playback/SustainTailShaper.h
        Source/Playback/SustainTailShaper.cpp
        Source/Playback/VelocityLayerGain.h
        Source/Playback/VelocityLayerGain.cpp
        Source/SampleMetadata.h
        Source/SampleMetadata.cpp
        Source/PercussionSynthesiser.h
        Source/PercussionSynthesiser.cpp
        Source/PercussionSound.h
        Source/PercussionSound.cpp
        Source/PercussionVoice.h
        Source/PercussionVoice.cpp
)

if (WIN32)
    set(PLUGIN_FORMATS VST3)
elseif (APPLE)
    set(PLUGIN_FORMATS VST3 AU Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

# Change these to your own preferences
juce_add_plugin(${PROJECT_NAME}
        COMPANY_NAME shroomnoise
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        JUCE_VST3_CAN_REPLACE_VST2 FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
        PLUGIN_MANUFACTURER_CODE Tap1
        PLUGIN_CODE Reg0
        FORMATS ${PLUGIN_FORMATS}
        PRODUCT_NAME "ulichpercs"
        VST3_CATEGORIES Instrument
)

# How we want our SourceFiles to appear in our IDE
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SourceFiles})

set(RB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/rubberband)

# Make the SourceFiles buildable
target_sources(${PROJECT_NAME} PRIVATE 
    ${SourceFiles}
    ${RB_DIR}/single/RubberBandSingle.cpp
)


# Headers live in external/rubberband/rubberband/...
# so we add external/rubberband to include path.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${RB_DIR}
)

# These are some toggleable options from the JUCE CMake API
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        NOMINMAX=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        $<$<PLATFORM_ID:Windows>:NOMINMAX=1>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN=1>
)

file(GLOB SAMPLE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.wav")
file(GLOB TRANSIENT_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/transients/*.json")

# Pass them to BinaryData
juce_add_binary_data(${PROJECT_NAME}Data
    SOURCES
        ui/bg.png
        ui/pomyatos.png
        ui/rzhav.png
        ui/tempo_on.png
        ui/tempo_off.png
        ${SAMPLE_FILES}
        ${TRANSIENT_FILES}
)

# JUCE libraries to bring into our project
set(JUCE_LTO_TARGET juce::juce_recommended_lto_flags)
if (WIN32)
    # Keep Windows CI/runtime behavior deterministic and avoid aggressive LTO
    # in plugin binaries loaded by third-party hosts.
    set(JUCE_LTO_TARGET)
    set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        juce::juce_analytics
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        ${JUCE_LTO_TARGET}
        juce::juce_recommended_warning_flags
        ${PROJECT_NAME}Data
)
