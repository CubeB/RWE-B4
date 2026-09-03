#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <rwe/observable/Observable.h>
#include <rwe/observable/Subscription.h>

namespace rwe
{
    template <typename T>
    class Subject : public Observable<T>
    {
    public:
        using SubscriberCallback = typename Observable<T>::SubscriberCallback;

    private:
        using SubscriberId = unsigned int;

        struct Subscriber
        {
            SubscriberId id;
            SubscriberCallback callback;
        };

        class ConcreteSubscription : public Subscription
        {
        private:
            Subject* observable;
            SubscriberId id;

        public:
            ConcreteSubscription(Subject<T>* observable, SubscriberId id)
                : observable(observable), id(id)
            {
            }

            ConcreteSubscription(const ConcreteSubscription&) = delete;
            ConcreteSubscription& operator=(const ConcreteSubscription&) = delete;

        protected:
            void unsubscribe() override
            {
                observable->unsubscribe(id);
            }
        };

    private:
        SubscriberId nextId{0};

        std::vector<Subscriber> subscribers;

    public:
        void next(const T& newValue)
        {
            // Over a copy: a callback is allowed to subscribe or unsubscribe
            // (a menu button that swaps the panel does exactly that), and
            // walking the live vector while it is edited is undefined.
            auto snapshot = subscribers;
            for (const Subscriber& s : snapshot)
            {
                s.callback(newValue);
            }
        }

        std::unique_ptr<Subscription> subscribe(const SubscriberCallback& onNext) override
        {
            auto id = nextId++;
            subscribers.push_back({id, onNext});

            return std::unique_ptr<Subscription>(new ConcreteSubscription(this, id));
        }

        std::unique_ptr<Subscription> subscribe(SubscriberCallback&& onNext) override
        {
            auto id = nextId++;
            subscribers.push_back({id, std::move(onNext)});

            return std::unique_ptr<Subscription>(new ConcreteSubscription(this, id));
        }

    private:
        void unsubscribe(SubscriberId id)
        {
            auto it = std::find_if(subscribers.begin(), subscribers.end(), [id](const Subscriber& s) { return s.id == id; });
            if (it == subscribers.end())
            {
                return;
            }

            subscribers.erase(it);
        }
    };
}
