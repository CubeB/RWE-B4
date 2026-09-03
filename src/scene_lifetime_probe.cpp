// Reproduces the crash the play-test found: a scene that subscribes to a
// service which outlives it must hand the subscription back, or the service
// calls into freed memory. Models the exact shape without needing a window.
#include <iostream>
#include <memory>
#include <rwe/observable/Subject.h>

using namespace rwe;

namespace
{
    int liveCallbacks = 0;

    struct Service
    {
        Subject<int> channelFinished;
        void dispatch(int c) { channelFinished.next(c); }
    };

    struct Scene
    {
        Service* service;
        int id;
        bool alive{true};
        std::unique_ptr<Subscription> sub;

        Scene(Service* service, int id) : service(service), id(id)
        {
            sub = service->channelFinished.subscribe([this](int c) {
                if (!alive)
                {
                    std::cout << "  FAIL: dead scene " << this->id << " called with " << c << "\n";
                    return;
                }
                ++liveCallbacks;
            });
        }

        ~Scene()
        {
            // The fix under test.
            sub->unsubscribe();
            alive = false;
        }
    };
}

int main()
{
    Service service;

    {
        auto first = std::make_unique<Scene>(&service, 1);
        service.dispatch(2);
        first.reset(); // exit to the main menu
    }

    liveCallbacks = 0;
    auto second = std::make_unique<Scene>(&service, 2);
    service.dispatch(2); // the frame that used to crash
    std::cout << "live callbacks after second scene: " << liveCallbacks << " (want 1)\n";

    // A callback that unsubscribes mid-dispatch must not corrupt the walk.
    Subject<int> s;
    std::unique_ptr<Subscription> a;
    int hits = 0;
    a = s.subscribe([&](int) { ++hits; a->unsubscribe(); });
    auto b = s.subscribe([&](int) { ++hits; });
    s.next(1);
    s.next(1);
    std::cout << "hits with mid-dispatch unsubscribe: " << hits << " (want 3)\n";

    std::cout << (liveCallbacks == 1 && hits == 3 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return (liveCallbacks == 1 && hits == 3) ? 0 : 1;
}
