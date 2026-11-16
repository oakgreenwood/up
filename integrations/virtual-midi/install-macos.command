#!/bin/bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_app="${1:-${script_dir}/bin/ULICHPERC MIDI.app}"
component_dir="${HOME}/Library/Application Support/shroomnoise/ulichpercs/VirtualMidi"
destination_app="${component_dir}/ULICHPERC MIDI.app"
agent_dir="${HOME}/Library/LaunchAgents"
agent_file="${agent_dir}/com.shroomnoise.ulichpercs.midi.plist"
login_domain="gui/$(id -u)"

if [[ "${1:-}" == "--uninstall" ]]; then
    /bin/launchctl bootout "${login_domain}" "${agent_file}" 2>/dev/null || true
    /bin/rm -f -- "${agent_file}" "${component_dir}/ready.json"
    /bin/rm -rf -- "${destination_app}"
    echo "Removed the ULICHPERC MIDI component for this user."
    exit 0
fi

if [[ ! -x "${source_app}/Contents/MacOS/ULICHPERC MIDI" ]]; then
    echo "Helper app not found. Build and stage the VirtualMidi CMake component first." >&2
    exit 1
fi
if [[ "${source_app}" == "${destination_app}" || "${source_app}" -ef "${destination_app}" ]]; then
    echo "Run this installer from the distribution folder, not the installed helper folder." >&2
    exit 1
fi

/bin/mkdir -p "${component_dir}" "${agent_dir}"
/bin/chmod 700 "${component_dir}"
/bin/launchctl bootout "${login_domain}" "${agent_file}" 2>/dev/null || true
/bin/rm -rf -- "${destination_app}"
/usr/bin/ditto "${source_app}" "${destination_app}"
/bin/rm -f -- "${agent_file}"
/usr/bin/plutil -create xml1 "${agent_file}"
/usr/bin/plutil -insert Label -string com.shroomnoise.ulichpercs.midi "${agent_file}"
/usr/bin/plutil -insert ProgramArguments -array "${agent_file}"
/usr/bin/plutil -insert ProgramArguments.0 -string "${destination_app}/Contents/MacOS/ULICHPERC MIDI" "${agent_file}"
/usr/bin/plutil -insert RunAtLoad -bool YES "${agent_file}"
/usr/bin/plutil -insert KeepAlive -bool YES "${agent_file}"
/usr/bin/plutil -insert ThrottleInterval -integer 10 "${agent_file}"
/bin/chmod 600 "${agent_file}"
/bin/launchctl bootstrap "${login_domain}" "${agent_file}"
echo "Installed ULICHPERC MIDI. Enable the required ULICHPERC MIDI input in your DAW."
