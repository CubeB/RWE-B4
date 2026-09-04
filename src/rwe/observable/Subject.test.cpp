#include <catch2/catch_test_macros.hpp>
#include <rwe/observable/Subject.h>
#include <memory>

namespace rwe
{
    namespace
    {
        /**
         * A subscriber that outlives nothing: it records whether it is still
         * alive, so a callback delivered after its death is visible as a
         * failure rather than as undefined behaviour we happen to survive.
         */
        struct Listener
        {
            Subject<int>* subject;
            bool alive{true};
            int calls{0};
            bool calledWhileDead{false};
            std::unique_ptr<Subscription> subscription;

            explicit Listener(Subject<int>& s)
                : subject(&s)
            {
                subscription = subject->subscribe([this](int) {
                    if (!alive)
                    {
                        calledWhileDead = true;
                        return;
                    }
                    ++calls;
                });
            }

            ~Listener()
            {
                // The rule this pins: destroying the handle is NOT enough.
                subscription->unsubscribe();
                alive = false;
            }
        };
    }

    TEST_CASE("a subscriber that dies first must hand its subscription back", "[observable]")
    {
        // Subscription's destructor deliberately does nothing, because most
        // call sites discard the handle: the subscriber outlives the subject
        // and the callback is meant to live as long as it does. Every menu in
        // the game is wired that way.
        //
        // The exception is a subscriber that dies BEFORE the subject, and
        // getting it wrong is not a leak but a crash. A GameScene subscribes
        // to the audio service, which outlives every scene; when that
        // subscription was left behind, the mixer reported a finished channel
        // into freed memory on the next game's first frame, and loading a
        // saved game died inside a mutex belonging to a scene that no longer
        // existed.

        SECTION("unsubscribing on the way out keeps a dead listener out of the walk")
        {
            Subject<int> service;

            {
                Listener first(service);
                service.next(2);
                REQUIRE(first.calls == 1);
            }

            // The listener is gone. Nothing should reach it.
            Listener second(service);
            service.next(2);

            REQUIRE(second.calls == 1);
            REQUIRE_FALSE(second.calledWhileDead);
        }

        SECTION("a callback may subscribe or unsubscribe while it is being delivered")
        {
            // A menu button that swaps the panel does exactly this, and
            // walking the live subscriber list while it is edited is
            // undefined -- so next() iterates a copy.
            Subject<int> subject;
            int hits = 0;

            std::unique_ptr<Subscription> first;
            first = subject.subscribe([&](int) {
                ++hits;
                first->unsubscribe();
            });
            auto second = subject.subscribe([&](int) { ++hits; });

            subject.next(1);
            subject.next(1);

            // Twice on the first delivery, once on the second: the
            // self-removing subscriber is gone by then.
            REQUIRE(hits == 3);
        }
    }
}
