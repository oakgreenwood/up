# JUCE Checks

## APVTS

Read realtime parameters through the atomics returned by
`AudioProcessorValueTreeState::getRawParameterValue()`. Consider caching pointers
at initialization when repeated ID lookup costs hot-path work.
`copyState()` and `replaceState()` lock and are not realtime-safe. Flag their
realtime use; do not mutate the APVTS `ValueTree` from audio.

## Buffers And Host Blocks

`juce::AudioBuffer::setSize()` may reallocate. Prepare internal buffers before
rendering; never resize heap scratch space inside `processBlock` to fit a larger
host block. Prefer bounded chunks using preallocated scratch space.

`maximumExpectedSamplesPerBlock` is a hint, not a guarantee. Check larger,
smaller, changing, and zero-sized blocks: scratch capacity/indexing, algorithm
state, and divisions/loops at `numSamples == 0`.

## Synthesiser And Voices

`juce::Synthesiser` uses a `CriticalSection` for rendering/note triggers. Inspect
before claiming lock-freedom. Identify UI/loader operations changing voices,
sounds, notes, or related state during playback that can contend with this lock.
An unbounded hold by another thread is a blocker.

Voice rendering may receive single-sample sub-blocks. Check excessive per-call
setup, positive/minimum-size assumptions, buffer indexing that assumes a full
host block, and realtime safety of note/controller state changes.

## MIDI

`MidiBuffer::getNumEvents()` traverses events; avoid repeated calls in realtime
loops when one pass suffices. Inspect storage growth when adding/copying events.
`ensureSize()` preallocates; calling it in `processBlock` is not a realtime-safe fix.
