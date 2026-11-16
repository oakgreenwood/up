# Ableton transport prototype retired

TRACK MIDI on sends SHAKE through `ULICHPERC MIDI` and checks returned input
without detecting recording state. TRACK MIDI off auditions internally without
that warning. The virtual mode needs the system MIDI helper and normal input
routing/monitoring only. No Control Surface or transport companion is required.
See [MIDI setup](../virtual-midi/README.md).

The prototype `ULICHPERC_Transport` script/installers and C++ transport override
were removed. If a prototype Control Surface entry was configured, set it to
None; it is no longer used by the plugin.

The separate legacy ADD MIDI client still expects its own clip-insertion
companion, which is not included here. This change does not implement that
protocol or alter the existing DRAG MIDI workflow.
