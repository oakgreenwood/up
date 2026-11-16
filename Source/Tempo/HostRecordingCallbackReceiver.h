#pragma once

namespace Ulichperc
{
enum class HostRecordingCallbackAvailability { unknown, unavailable, available };

// The AU wrapper reports only callback presence, during its existing render-time
// position query. Implementations must remain allocation-free and nonblocking.
class HostRecordingCallbackReceiver
{
public:
    virtual ~HostRecordingCallbackReceiver() = default;
    virtual void setHostRecordingCallbackAvailable(bool available) noexcept = 0;
};
}
