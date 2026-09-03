#pragma once

namespace rwe
{
    /**
     * A handle to a subscription. Destroying it does NOT unsubscribe -- most
     * call sites discard the handle on purpose, because the subscriber
     * outlives the subject (a scene listening to a panel it owns) and the
     * callback should live as long as the subject does.
     *
     * The exception is a subscriber that dies BEFORE the subject: anything
     * subscribing to a long-lived service must call unsubscribe() before it
     * goes away, or the service will keep calling into freed memory. A
     * GameScene listening to the audio service is exactly that case, and
     * forgetting it crashed the second game of any session.
     */
    class Subscription
    {
    public:
        virtual ~Subscription() = default;
        virtual void unsubscribe() = 0;
    };
}
