#include <PipePublisher.h>
#include <gtest/gtest.h>
#include <WorkerThread.h>

struct Msg {
    std::string   message;
};

struct InvokeOnCopyFunction {
    std::function<void ()> f;
    std::function<void()> onCopy = [] () -> void {};
    std::function<void()> onMove = [] () -> void {};

    InvokeOnCopyFunction() = default;
    InvokeOnCopyFunction(nullptr_t null): f(null) {}

    void operator() () const {
        f();
    }
    explicit operator const std::function<void ()>&() const { return f;}

    InvokeOnCopyFunction(InvokeOnCopyFunction&& rhs) {
        (*this) = std::move(rhs);
    }
    InvokeOnCopyFunction& operator=(InvokeOnCopyFunction&& rhs) {
        rhs.onMove();
        f = std::move(rhs.f);
        onCopy = std::move(rhs.onCopy);
        onMove = std::move(rhs.onMove);

        return *this;
    }

    InvokeOnCopyFunction(const InvokeOnCopyFunction& rhs) {
        (*this) = rhs;
    }
    InvokeOnCopyFunction& operator=(const InvokeOnCopyFunction& rhs) {
        rhs.onCopy();
        f = rhs.f;
        onCopy = rhs.onCopy;
        onMove = rhs.onMove;

        return *this;
    }
};

using Client = PipeSubscriber<Msg, InvokeOnCopyFunction>;

void MessagesMatch(const std::vector<Msg>& expected,
                   const std::vector<Msg>& got)
{
    ASSERT_EQ(expected.size(), got.size());

    for (size_t i = 0; i < expected.size(); ++i) {
        const Msg& msg = expected[i];
        const Msg& recvd = got[i];

        ASSERT_EQ(msg.message, recvd.message);
    }
}

/**
 * In this edge case, the publisher thread completes publication, just as
 * the trigger is being set by the client thread. It can't safely trigger
 * whist its being updated, so instead it updates the state to notify the
 * client it must immediately trigger once it is done configuring
 *
 * To actually hit this code we'd have to be very unlucky, so to force it
 * to happen, we slow down the copy constructor...
 */
TEST(TPipeSubscriperStateModel, ClientStillConfiguringAfterPublish) {
    PipePublisher<Msg> pub;
    auto client = pub.NewClient<Client>(1024);
    std::vector<Msg> got;

    std::promise<bool> startedCopy;
    std::promise<bool> completeCopy;

    WorkerThread clientThread;
    clientThread.Start();
    clientThread.PostTask([&] () -> void {
        // Setup the trigger function to block until we're done publishing...
        InvokeOnCopyFunction trigger;
        trigger.onCopy = [&] () -> void {
            ASSERT_TRUE(completeCopy.get_future().get());
        };
        trigger.f = [&] () -> void {
            Msg m;
            while (client->GetNextMessage(m))  {
                got.push_back(m);
            }
        };

        startedCopy.set_value(true);
        client->OnNextMessage(trigger);
    });

    std::vector<Msg> toSend {
        {"Message 1"},
        {"Mesasge 2"},
        {"Mesasge 3"},
    };

    if (startedCopy.get_future().get()) {
        pub.Publish(toSend[0]);
        pub.Publish(toSend[1]);
        completeCopy.set_value(true);

        // Flush client thread...
        clientThread.DoTask([] () -> void {});

        // Final message shouldn't be picked up by the trigger
        pub.Publish(toSend[2]);

        MessagesMatch({toSend[0], toSend[1]}, got);
    } else {
        ASSERT_FALSE(true) << "Failed to start the copy!";
    }

}

/**
 * In the following set of edge cases we deal with scenarios where the
 * client thread attempts to re-configure, *whilst* the publisher is
 * already publishing. When this happens the client thread will
 * do an initial copy into the "pending" notification storage. What
 * happens next depends on state resolution:
 *
 *   1. (The publisher is already done) If the publisher completed whilst
 *      we were budy setting up the pending notify, then its up to us to
 *      complete the configuration. This essentially returns us to a
 *      standard CONFIGURING state:
 *             CLIENT_WILL_RECONFIGURE -> CONFIGURING
 *      NOTE: If there are still message in the queue, this will result
 *            in an immediate trigger.
 */

// See notes on client re-configuring above
// This test test covers edge case (1)
TEST(TPipeSubscriperStateModel, ClientWillReConfigure) {
    std::vector<Msg> toSend {
            {"Message 1"},
            {"Mesasge 2"}
    };
    std::vector<Msg> got;

    PipePublisher<Msg> pub;
    auto client = pub.NewClient<Client>(1024);

    std::promise<bool> startedPublishing;
    std::promise<bool> startedPendingReconfigure;

//STATE: DISABLED

    auto wait = [&] () -> void {
        startedPublishing.set_value(true);
        startedPendingReconfigure.get_future().get();
    };
    InvokeOnCopyFunction initialTrigger;
    initialTrigger.f = wait;

    client->OnNextMessage(initialTrigger);

//STATE: NOTHING_TO_NOTIFY

    WorkerThread pubThread, clientThread;
    pubThread.Start();
    clientThread.Start();

    pubThread.PostTask([&] () -> void {
        // NOTE: This will block until the client has started the pending-configure
        pub.Publish(toSend[0]);
    });

//STATE: PUBLISHING

    InvokeOnCopyFunction blockingReconfigure;
    blockingReconfigure.f = [&] () -> void {
        Msg m;
        while (client->GetNextMessage(m)) {
            got.push_back(m);
        }
    };
    blockingReconfigure.onCopy = [&] () -> void {
        startedPendingReconfigure.set_value(true);

        // Flush the publisher...
        pubThread.DoTask( [&] () -> void { });

        // Once that message is done, *another* message
        // comes in: should be picked up  when the client
        // realises it needs to pull
        pubThread.DoTask( [&] () -> void {
            pub.Publish(toSend[1]);
        });

    };

    clientThread.DoTask([&] () -> void {
        client->OnNextMessage(blockingReconfigure);
    });

    MessagesMatch({toSend[0], toSend[1]}, got);
}

// See notes on client re-configuring above
// This test test covers edge case (1B)
