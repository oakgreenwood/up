#include "TrackpadPlayView.h"

#include <cmath>
#include <utility>
#import <AppKit/AppKit.h>
#import <objc/message.h>
#import <objc/runtime.h>
#if JUCE_DEBUG
#include <os/log.h>
#endif
#include <juce_core/native/juce_ObjCHelpers_mac.h>

namespace
{
constexpr int defaultVelocity = 100;
constexpr int softVelocity = 30;
constexpr float fullVelocityPressure = 0.40f;
constexpr int pressureWindowMs = 65;
constexpr double tapContactLifetimeSeconds = 0.1;

#if JUCE_DEBUG
os_log_t trackpadLog()
{
    static const auto log = os_log_create("audio.ulichperc", "trackpad");
    return log;
}
#endif

juce::Point<float> positionOf(NSTouch* touch)
{
    const auto position = touch.normalizedPosition;
    return { static_cast<float>(position.x), static_cast<float>(position.y) };
}
}

// A runtime class name avoids collisions if a DAW loads both the AU and VST3.
struct TrackpadPlayView::ViewClass : juce::ObjCClass<NSView>
{
    static constexpr auto ownerIvar = "trackpadPlayViewOwner";

    ViewClass() : ObjCClass("UlichpercTrackpadView_")
    {
        addIvar<TrackpadPlayView*>(ownerIvar);
        addMethod(@selector(isFlipped), [] (id, SEL) -> BOOL { return YES; });
        addMethod(@selector(acceptsFirstMouse:), [] (id, SEL, NSEvent*) -> BOOL { return YES; });
        addMethod(@selector(hitTest:), hitTest);
        addMethod(@selector(mouseDown:), mouseDown);
        addMethod(@selector(mouseUp:), mouseUp);
        addMethod(@selector(pressureChangeWithEvent:), pressureChanged);
        addMethod(@selector(touchesBeganWithEvent:), contactsChanged);
        addMethod(@selector(touchesMovedWithEvent:), contactsChanged);
        addMethod(@selector(touchesEndedWithEvent:), contactsEnded);
        addMethod(@selector(touchesCancelledWithEvent:), contactsCancelled);
        registerClass();
    }

    static TrackpadPlayView* owner(id view)
    {
        return juce::getIvar<TrackpadPlayView*>(view, ownerIvar);
    }

    static NSView* hitTest(id self, SEL selector, NSPoint point)
    {
        NSView* view = sendSuperclassMessage<NSView*>(self, selector, point);
        auto* component = owner(self);
        if (view == nil || component == nullptr)
            return nil;

        const NSPoint local = [view convertPoint:point fromView:view.superview];
        const NSSize size = view.bounds.size;
        if (size.width <= 0.0 || size.height <= 0.0)
            return nil;
        const juce::Point<float> position {
            static_cast<float>(local.x * component->getWidth() / size.width),
            static_cast<float>(local.y * component->getHeight() / size.height)
        };
        return component->passThroughBounds.contains(position) ? nil : view;
    }

    static void mouseDown(id self, SEL, NSEvent* event)
    {
        auto* component = owner(self);
        if (component == nullptr || !component->isShowing())
            return;
        const auto receivedMs = juce::Time::getMillisecondCounterHiRes();
        if (component->isTimerRunning())
            component->finishHit("nextMouseDown");

        const auto mask = event.associatedEventsMask;
        const bool hasPressure = (mask & NSEventMaskPressure) != 0;
        Touch position;
        if (event.subtype == NSEventSubtypeTouch || hasPressure)
        {
            position = component->contact;
            const auto sinceTouchEnded = event.timestamp - component->endedAt;
            if (!position && sinceTouchEnded >= 0.0 && sinceTouchEnded <= tapContactLifetimeSeconds)
                position = component->endedContact;
        }
        component->endedContact.reset();

        if (hasPressure)
        {
            component->mouseDownTimestamp = event.timestamp;
            component->mouseDownReceivedMs = receivedMs;
            // A binary mouse-down pressure of 1 supplies no force measurement.
            component->pendingContact = position;
            const float pressure = event.pressure;
            component->peakPressure = pressure > 0.0f && pressure < 1.0f ? pressure : 0.0f;
            component->startTimer(pressureWindowMs);
        }

#if JUCE_DEBUG
        os_log(trackpadLog(),
               "mouseDown t=%.6f window=%ld subtype=%ld mask=0x%llx raw=%.4f pressureCapable=%d contact=%d",
               event.timestamp, static_cast<long>(event.windowNumber), static_cast<long>(event.subtype),
               static_cast<unsigned long long>(mask), event.pressure, hasPressure, position.has_value());
#endif

        if (!hasPressure)
            component->hit(defaultVelocity, position, receivedMs);
    }

    static void mouseUp(id self, SEL, [[maybe_unused]] NSEvent* event)
    {
        auto* component = owner(self);
        if (component == nullptr)
            return;
        const bool pending = component->isTimerRunning();
#if JUCE_DEBUG
        if (component->isShowing())
            os_log(trackpadLog(), "mouseUp t=%.6f window=%ld raw=%.4f pending=%d",
                   event.timestamp, static_cast<long>(event.windowNumber), event.pressure, pending);
#endif
        if (pending)
            component->finishHit("mouseUp");
    }

    static void pressureChanged(id self, SEL, NSEvent* event)
    {
        auto* component = owner(self);
        if (component == nullptr)
            return;
        const bool pending = component->isTimerRunning();
        const auto stage = event.stage;
        const auto pressure = event.pressure;
        const bool accepted = pending && stage == 1 && std::isfinite(pressure)
            && event.timestamp <= component->mouseDownTimestamp + pressureWindowMs * 0.001;
        if (accepted)
        {
            if (!component->pendingContact)
                component->pendingContact = component->contact;
            component->peakPressure = juce::jmax(component->peakPressure, pressure);
        }
#if JUCE_DEBUG
        if (component->isShowing())
            os_log(trackpadLog(),
                   "pressure t=%.6f window=%ld stage=%ld behavior=%ld transition=%.4f raw=%.4f pending=%d accepted=%d",
                   event.timestamp, static_cast<long>(event.windowNumber), static_cast<long>(stage),
                   static_cast<long>(event.pressureBehavior), event.stageTransition, pressure, pending, accepted);
#endif
    }

    static Touch activeContact(NSView* view, NSEvent* event)
    {
        auto* touches = [event touchesMatchingPhase:NSTouchPhaseTouching inView:view];
        return touches.count == 1 ? Touch(positionOf(touches.anyObject)) : std::nullopt;
    }

    static void contactsChanged(id self, SEL, NSEvent* event)
    {
        if (auto* component = owner(self))
        {
            component->contact = activeContact(self, event);
            component->endedContact.reset();
        }
    }

    static void contactsEnded(id self, SEL, NSEvent* event)
    {
        if (auto* component = owner(self))
        {
            const Touch previous = component->contact;
            component->contact = activeContact(self, event);
            // Tap-to-click can post its mouse-down after the finger lifts.
            auto* ended = [event touchesMatchingPhase:NSTouchPhaseEnded inView:self];
            if (previous && !component->contact && ended.count == 1)
            {
                component->endedContact = positionOf(ended.anyObject);
                component->endedAt = event.timestamp;
            }
        }
    }

    static void contactsCancelled(id self, SEL, NSEvent*)
    {
        if (auto* component = owner(self))
            component->clearContacts();
    }
};

TrackpadPlayView::TrackpadPlayView(std::function<void(int, Touch, double)> onHit)
    : hit(std::move(onHit))
{
    static const ViewClass viewClass;
    juce::NSUniquePtr<NSView> view { [viewClass.createInstance() initWithFrame:NSZeroRect] };
    object_setInstanceVariable(view.get(), ViewClass::ownerIvar, this);
    [view.get() setAllowedTouchTypes:NSTouchTypeMaskIndirect];
    juce::NSUniquePtr<NSPressureConfiguration> pressure {
        [[NSPressureConfiguration alloc] initWithPressureBehavior:NSPressureBehaviorPrimaryGeneric]
    };
    [view.get() setPressureConfiguration:pressure.get()];
    setView(view.get());
#if JUCE_DEBUG
    os_log(trackpadLog(), "view ready behavior=%ld pressureWindowMs=%d",
           static_cast<long>(NSPressureBehaviorPrimaryGeneric), pressureWindowMs);
#endif
}

TrackpadPlayView::~TrackpadPlayView()
{
    stopTimer();
    if (auto* view = static_cast<NSView*>(getView()))
        object_setInstanceVariable(view, ViewClass::ownerIvar, nullptr);
    setView(nullptr);
}

void TrackpadPlayView::timerCallback()
{
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - mouseDownReceivedMs;
    if (elapsedMs < pressureWindowMs)
    {
        // JUCE's shared timer can fire early. Allow the full pressure window.
        startTimer(static_cast<int>(std::ceil(pressureWindowMs - elapsedMs)));
        return;
    }
    finishHit("timer");
}

void TrackpadPlayView::visibilityChanged()
{
    if (!isShowing())
    {
        stopTimer();
        pendingContact.reset();
        clearContacts();
    }
}

void TrackpadPlayView::clearContacts()
{
    contact.reset();
    endedContact.reset();
}

void TrackpadPlayView::finishHit([[maybe_unused]] const char* reason)
{
    stopTimer();
    const auto position = std::exchange(pendingContact, std::nullopt);
    if (isShowing())
    {
        const float pressureAmount = juce::jlimit(0.0f, fullVelocityPressure, peakPressure)
            / fullVelocityPressure;
        const int velocity = softVelocity + juce::roundToInt(pressureAmount * (127 - softVelocity));
#if JUCE_DEBUG
        os_log(trackpadLog(), "velocity reason=%{public}s elapsedMs=%.2f peak=%.4f velocity=%d",
               reason, juce::Time::getMillisecondCounterHiRes() - mouseDownReceivedMs, peakPressure, velocity);
#endif
        hit(velocity, position, mouseDownReceivedMs);
    }
}

#if JUCE_DEBUG
void TrackpadPlayView::logMidiHit(int note, int velocity, bool trackMidi, bool accepted)
{
    os_log(trackpadLog(), "midi note=%d velocity=%d trackMidi=%d accepted=%d",
           note, velocity, trackMidi, accepted);
}
#endif
