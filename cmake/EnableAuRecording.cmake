# The pinned JUCE AUv2 wrapper only queries the legacy transport callback,
# which has no recording flag. Generate a narrowly patched build-time copy;
# keep the downloaded dependency and all other wrapper sources unchanged.
function(ulichperc_enable_au_recording)
    get_target_property(wrapper_sources juce_audio_plugin_client_AU INTERFACE_SOURCES)
    set(patched_sources)
    set(found_transport_source FALSE)
    foreach(source IN LISTS wrapper_sources)
        if(source MATCHES "/juce_audio_plugin_client_AU_1\\.mm$")
            set(found_transport_source TRUE)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
            file(READ "${source}" wrapper)
            string(REPLACE "\r\n" "\n" wrapper "${wrapper}")
            set(needle "            if (audioUnit.CallHostTransportState (&playing,")
            string(FIND "${wrapper}" "${needle}" transport_offset)
            if(transport_offset EQUAL -1)
                message(FATAL_ERROR "JUCE AU transport code changed; review EnableAuRecording.cmake")
            endif()
            set(replacement [=[            const auto& hostCallbacks = audioUnit.GetHostCallbackInfo();
            if (auto* receiver = dynamic_cast<Ulichperc::HostRecordingCallbackReceiver*> (audioUnit.juceFilter.get()))
                receiver->setHostRecordingCallbackAvailable (hostCallbacks.transportStateProc2 != nullptr);
            Boolean recording = false;
            const bool transportStateProvided = hostCallbacks.transportStateProc2 != nullptr
                && hostCallbacks.transportStateProc2 (hostCallbacks.hostUserData,
                                                       &playing, &recording, &playchanged,
                                                       &outCurrentSampleInTimeLine, &looping,
                                                       &outCycleStartBeat, &outCycleEndBeat) == noErr;
            if (transportStateProvided)
                info.setIsRecording (recording);

            if (transportStateProvided || audioUnit.CallHostTransportState (&playing,]=])
            string(REPLACE "${needle}" "${replacement}" wrapper "${wrapper}")
            string(PREPEND wrapper "#include \"HostRecordingCallbackReceiver.h\"\n\n")
            set(generated "${CMAKE_CURRENT_BINARY_DIR}/juce-au-transport/juce_audio_plugin_client_AU_1.mm")
            # file(CONFIGURE) only updates the output timestamp when content changes.
            file(CONFIGURE OUTPUT "${generated}" CONTENT "${wrapper}" @ONLY)
            list(APPEND patched_sources "${generated}")
        else()
            list(APPEND patched_sources "${source}")
        endif()
    endforeach()
    if(NOT found_transport_source)
        message(FATAL_ERROR "JUCE AU wrapper source missing; review EnableAuRecording.cmake")
    endif()
    set_property(TARGET juce_audio_plugin_client_AU PROPERTY INTERFACE_SOURCES "${patched_sources}")
    target_include_directories(juce_audio_plugin_client_AU INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Source/Tempo")
endfunction()
