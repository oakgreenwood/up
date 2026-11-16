# ULICHPERC virtual MIDI component

The `ULICHPERCMidi` background application creates one system MIDI input:
`ULICHPERC MIDI`. It is an ordinary MIDI input,
shared by DAWs through the operating system. No DAW script or MIDI clipboard is
involved. macOS uses CoreMIDI; Windows 11 uses the Windows MIDI Services backend
in the pinned JUCE revision. Older Windows versions are outside this iteration.

Implementation is present, but builds, installers and host compatibility have
not been run or verified. Do not label the seven DAWs as certified. Windows
requires compatible SDK/runtime provisioning described below. The component is
not automatically installed by the existing plugin build scripts.

When updating the earlier multi-input implementation, rebuild/reinstall both the
helper and plugin, then select `ULICHPERC MIDI` in the DAW. Protocol version 2
prevents the updated plugin from connecting to the earlier helper. Saved plugin
routes 1..8 restore as virtual mode on the single input; the DAW's own previously
saved input routing must be changed in the DAW.

## Record a SHAKE performance

1. Install the component once for your OS user. It starts immediately and at
   subsequent logins; no manually launched script is needed for each recording.
2. In the DAW, enable `ULICHPERC MIDI` for note recording.
   If a running DAW does not refresh its device list, restart
   that DAW after the helper has started.
3. Route `ULICHPERC MIDI` to the instrument track and enable input monitoring.
   In Ableton: MIDI From = `ULICHPERC MIDI`, All Channels, Monitor = Auto, and
   arm the instrument track. Only monitor the intended instance/track.
4. Load ulichpercs, open Edit Samples for a directional group and turn
   **TRACK MIDI** on using the button below SHAKE. Turn **SHAKE** on too.
5. Move the cursor inside the plugin. The generated note returns through the
   DAW input and plays the sample. Start DAW recording to capture the notes;
   this routing works before, during and after recording.

TRACK MIDI off (default for new state) gives internal SHAKE audition without the
helper or missing-input alerts. TRACK MIDI on sends SHAKE only through the helper
and plays returned DAW MIDI; input monitoring is required even while stopped.
The choice is saved with the plugin. Switching routes keeps SHAKE enabled but
resets the gesture/input check. Normal incoming MIDI still plays in both modes.
There is no recording-state inference, Control Surface setup or routing popup.

TRACK MIDI + SHAKE starts a one-second check on the first unanswered stroke for
each `_u`/`_d` pitch. If no incoming positive note-on of that pitch returns, the
plugin shows **Enable ULICHPERC MIDI in your DAW settings.** It warns once per
mode/target session, whether the DAW is stopped, playing or recording. Repeated
unanswered strokes cannot delay it; failed helper sends also arm it. Turning
TRACK MIDI or SHAKE off, bypass, changing target or hide/close cancels checks.
Recording start/stop alone does not change the mode or cancel a pending check.

This checks input presence, not source identity or recording success. Another
controller playing the expected pitch can acknowledge it. Missing routing,
arming/monitoring, filtering, an unavailable helper or a busy lease may cause the
warning; check the tooltip and intended track's MIDI settings. No DAW preferences
are read or changed. The prototype Ableton transport bridge remains removed;
old prototype Control Surface entries can be set to None.

The existing SHAKE tooltip names the selected input and reports missing helper,
unavailable runtime or an input occupied by another instance. Turn off SHAKE in
the previous instance before using it in another instance.
Prefer an explicit input over All Inputs when several instruments are armed.
All enabled DAWs/tracks listening to that port can receive it; the helper cannot
select or arm a track for the user. There is one shared SHAKE input.

With TRACK MIDI on, SHAKE sends only cursor strokes; incoming MIDI is not echoed. The take cache
captures returned mapped notes once; DRAG MIDI and the separate legacy ADD MIDI
client retain their roles. Native host transport still supplies cache boundaries
where available, but SHAKE and DAW input recording do not depend on that flag.

The existing velocity, reversal, 4-second pause, transport reset and u/d choke
rules remain in effect. Returning MIDI follows the ordinary sampler path. A
fresh virtual session/reconnection requests a scalar sequence reset before
incoming dispatch. Playback/recording starts continue to reset the gesture.

## DAW setup differences

These are intended ordinary MIDI-input workflows, not results of host tests.
Menu names can differ by release.

| DAW | Input setup / recording route |
| --- | --- |
| Ableton Live | Enable Track for the input in MIDI settings; select it on the armed ulichpercs track, Monitor Auto. Standard and Suite use the same approach. |
| FL Studio | Enable the input in MIDI settings as a generic controller; route its notes to the ulichpercs generator and record note data. |
| Logic Pro | Use the input for the software-instrument track; select/record-enable the intended track and record its MIDI region. |
| Cubase | Make the port available in MIDI Port Setup; select it as the instrument track's Input Routing and enable recording/monitoring. |
| Studio One | Add a New Keyboard with Receive From set to this input; select that keyboard on the instrument track, monitor and arm. |
| REAPER | Enable the device for MIDI input, choose it as the track's MIDI input, enable record monitoring and record input MIDI. |
| Pro Tools | Enable the MIDI input and route it to an armed MIDI/instrument track. The system input is usable independently, but this repository does not build an AAX ulichpercs instrument; VST3/AU cannot be loaded directly in Pro Tools. |

References: [Ableton input/recording](https://www.ableton.com/en/live-manual/12/recording-new-clips/),
[FL Studio MIDI settings](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_midi.htm),
[Logic record enable](https://support.apple.com/guide/logicpro/enable-tracks-for-recording-lgcp0c445ccf/10.7/mac/11.0),
[Cubase input routing](https://www.steinberg.help/r/cubase-elements/15.0/en/cubase_nuendo/topics/setting_up/setting_up_midi_inputs_setting_up_t.html),
[Studio One external devices](https://support.presonus.com/hc/en-us/articles/210040463-MIDI-External-Devices-Control-Surfaces-Setup),
[REAPER user guide](https://www.reaper.fm/userguide.php),
[Avid AAX](https://developer.avid.com/aax/).

## Build and stage (developer)

`ULICHPERC_BUILD_MIDI_COMPONENT` defaults ON. It creates a separate
`ULICHPERCMidi` target with the existing JUCE revision and no sample resources or
RubberBand. Its SDK definitions are private; the plugin does not load the Windows
MIDI Services backend. The existing Windows plugin-only CI explicitly disables
this separate component and does not provide a helper artifact.

After configuring the project with its usual generator/architecture:

```sh
cmake --build build --config Release --target ULICHPERCMidi
cmake --install build --config Release --component VirtualMidi --prefix stage
```

Ship `stage/virtual-midi` together with the updated plugin. Plugin binaries are
built/distributed separately. For plugin-only builds, configure with
`-DULICHPERC_BUILD_MIDI_COMPONENT=OFF`; do not interpret that build as providing
virtual MIDI recording. macOS universal/Intel options apply to the helper too.

`integrations/virtual-midi` is the source folder: edit the helper, installation
scripts and this guide there. `stage/virtual-midi` is generated distribution
output, excluded from Git. CMake copies the relevant installer and README there
alongside the built helper so the distribution can be used without the source
repository. These copies are intentional; do not edit or maintain them separately.
After source changes, rebuild the helper and rerun the install command above to
refresh the staged files. The stage folder can be deleted and regenerated; it
is not used as input when building the helper or plugin.

### macOS

Run the staged `install-macos.command` once (or invoke it from the product's
installer). It installs:

- Helper: `~/Library/Application Support/shroomnoise/ulichpercs/VirtualMidi/ULICHPERC MIDI.app`
- Login agent: `~/Library/LaunchAgents/com.shroomnoise.ulichpercs.midi.plist`

No MIDI driver folder is scanned or written. CoreMIDI publishes the input; the
fixed app bundle identifier and name give JUCE a stable macOS port ID.
The LaunchAgent keeps the helper running before DAWs enumerate their inputs.
Modern macOS may expose background-item controls; users can disable the helper
there. Sign/notarize the helper along with the plugin for public distribution;
the script does not remove quarantine or bypass OS security prompts.

For development, the same script accepts the built `.app` as its first argument.
To uninstall, run `bash install-macos.command --uninstall`. Stop recording before
installing, updating or removing the helper. A manually started development
helper must be quit before replacing its installed app.

### Windows 11

The helper target requires MSVC, a Windows SDK at least 10.0.20348.0, and JUCE's
CppWinRT and Windows MIDI Services NuGet dependencies. JUCE finds package folders
under `%USERPROFILE%/AppData/Local/PackageManagement/NuGet/Packages`; alternatively
set `JUCE_CPPWINRT_PACKAGE_LOCATION` and
`JUCE_WINDOWS_MIDI_SERVICES_PACKAGE_LOCATION` to parent directories containing
the respective `Microsoft.Windows.CppWinRT.*` and
`Microsoft.Windows.Devices.Midi2.*` package folders. JUCE invokes CppWinRT during
configuration and links the generated headers into the helper only.

SDK compatibility matters: the pinned JUCE backend uses the
`Microsoft.Windows.Devices.Midi2` App SDK API. Its Projucer references package
`1.0.3-preview-11.250228-237`; a random latest SDK/in-box
`Windows.Devices.Midi2` package is not a drop-in replacement. Provision a
compatible SDK/runtime pair and test it on the intended Windows 11 build before
shipping. No SDK/runtime installer is downloaded or redistributed by this code.
Changing to Microsoft's newer API requires a separately verified JUCE/backend
update. Windows 11 alone does not guarantee this backend is available, nor does
enabling the CMake flag install the runtime on end users' machines.

Run the staged `install-windows.ps1` once from PowerShell, or invoke its
installation steps from the signed product installer. It installs:

- Helper: `%APPDATA%\shroomnoise\ulichpercs\VirtualMidi\ULICHPERC MIDI.exe`
- Login startup: HKCU `Software\Microsoft\Windows\CurrentVersion\Run`, value
  `ULICHPERC MIDI`.

The script installs only the helper, with no admin requirement for these files;
the compatible MIDI Services runtime must be installed separately or packaged by
the release installer. Its installation may require administrator permission.
Do not disable PowerShell execution policy; distribute signed installation code
where policy requires it. Pass `-Uninstall` to remove this per-user installation.
Older Windows versions, loopMIDI and third-party virtual drivers are not included.
MIDI Services must expose the virtual input to the MIDI API the particular DAW
uses; legacy port visibility and persistence require host/runtime testing.

## IPC, timing and lifecycle

The plugin/editor and helper use fixed 64-byte UDP datagrams bound to IPv4
loopback. The helper publishes a private per-user `ready.json` with protocol,
ephemeral port, random session cookie, available-input mask and heartbeat.
Discovery runs at most once per second on the editor thread. There is no audio
thread socket, file access, MIDI device call, wait, new queue or owning pointer.
The editor can launch an installed helper when discovery is missing/stale, at
most once every ten seconds while SHAKE is enabled, including stopped audition.

Claims are refreshed every 250 ms. The single input has one active client
lease, expiring after two seconds without a claim/accepted stroke. Replies are
authenticated and checked for order/freshness. Nonowners cannot inject strokes
or release another owner's notes. Ordered sequence numbers reject duplicates
and delayed messages. Strokes older than 250 ms are discarded. UDP remains
best-effort: send success is not a guarantee that the DAW recorded a note.

The helper worker checks note deadlines between bounded batches of at most 32
datagrams. A retrigger closes the previous generated note, then starts a 30 ms
channel-1 gate. Release, lease expiry and graceful shutdown close held notes.
The helper message thread publishes discovery files separately, so file writes
do not block note deadlines. The editor reads at most 16 replies per poll, with
nonblocking readiness checks. No network broadcast or remote interface is used.

Closing/hiding sample editing, selecting a different group, disabling SHAKE,
bypass and editor teardown release the route. TRACK MIDI off releases the route too. Playback/recording stop leaves
the route active while TRACK MIDI and SHAKE stay enabled. A suspended host/UI or
lost connection is handled by the finite gate and lease deadlines. A killed
helper cannot send its final note-off; stop/All Notes Off in the DAW can recover
a held note. Restart the helper after a system MIDI service reset.

The helper is not a hard-realtime process. OS scheduling, DAW MIDI timestamps,
monitoring, latency compensation and input quantization determine recording and
playback timing. Test reconnects, helper restart, multiple armed tracks/instances,
busy inputs, device refresh, AU/VST3, host focus while recording, high mouse event
rates and transport/4-second resets on both architectures/platforms.

See [realtime audit](../../docs/realtime-audio-audit-virtual-midi.md). The retained
RubberBand fallback remains a known audio-thread allocation/reclamation blocker.
