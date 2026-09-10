# Search heuristics

Use repository search as a starting point, then inspect semantics. A keyword hit is not automatically a bug.

Useful patterns around realtime-reachable code include:

```text
processBlock
processBlockBypassed
renderNextBlock
startNote
stopNote
controller
pitchWheel

setSize
resize
reserve
push_back
emplace_back
insert
erase
clear
reset
make_unique
make_shared
shared_ptr
ReferenceCounted
std::function

CriticalSection
SpinLock
mutex
WaitableEvent
condition_variable
join
sleep
yield

copyState
replaceState
ValueTree
callAsync
sendChangeMessage
```

Also inspect project-specific wrapper/helper functions that hide these operations.
