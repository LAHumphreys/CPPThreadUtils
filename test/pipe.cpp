/*
 *  PipePublisher -> PipeSubscriber: Workflow Definitions
 *
 *  The tests in this suite describe functionally the expected behaviour of the PipePublisher.
 *
 *  NOTE: The technical implementation of the PipeSubscriber uses a state model in order
 *        to remain lock free wherever possible (This delivers a much improved throughput
 *        under load, when publishing to a dedicated WorkerThread). As a result these simple
 *        functional tests are not sufficient on themselves to guarantee the behaviour in all
 *        cases. These thornier cases are covered in the pipe_state_model.cpp suite.
 */
#include <PipePublisher.h>
#include <gtest/gtest.h>
#include <WorkerThread.h>

using namespace std;

struct Msg {
    std::string   message;
};

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
 * Subscriber reads *after* publisher has finished
 *
 * Simplest possible case:
 *    - Publisher loads up the queue
 *    - Sometime later, the subscriber fully consumes the queue
 */
TEST(TPipePublisher,PublishSingleConsumer) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };
    std::thread worker(f);
    worker.join();

    MessagesMatch(toSend,got);
}

/**
 * OnNextMessage [NoTargetThread]: Use Next Message Interface to trigger code when the next message is available
 *
 *  The OnNextMessage interface allows the consumer to install some code to be
 *  triggered upon the next available message. (If there's already a message waiting, then the code
 *  will be triggered immediately). If a message hook was previously installed, this new hook
 *  replaces the old one (unless the previous hook is already being executed by the publisher thread,
 *  in which case it will be installed *after* completion of the old hook).
 *
 *  NOTE: The consumer has not provided an IPostable interface - which means it is undefined what
 *        thread will execute the code. There are 3 *functional* possibilities, although in terms
 *        of state model its more complicated (see also the state model test suite).
 *           1. There is already a message waiting - in which case this will be triggered immediately
 *              (within the current function call).
 *           2. There are no new messages - the publisher will invoke the trigger code
 *              (on the publisher thread), immediately after it has written the next message
 *              to the queue
 *           3. There was already a call-back installed, and its currently being executed by the
 *               publisher thread. In which case installation is deferred until after the current
 *               trigger has completed. At that point the trigger will be invoked as per (2)
 */


// See note on OnNextMessge [NoTargetThread] above.
//   Case 1: Messages already waiting
TEST(TPipeSubscriber,Notify_ExistingMessages) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };
    
    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    client->OnNextMessage(f);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

// See note on OnNextMessge [NoTargetThread] above.
//   Case 2: Publisher will trigger on next write
TEST(TPipeSubscriber,Notify_OnNextWrite) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
            {"Message 1"},
            {"Mesasge 2"},
            {"Hello World!"}
    };

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };

    client->OnNextMessage(f);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    std::vector<Msg> expected = { toSend[0] };
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));
}

// See note on OnNextMessge [NoTargetThread] above.
//   Case 3: Publisher will configure when the current trigger is complete
TEST(TPipeSubscriber,Notify_FollowOnTrigger) {
    std::vector<Msg> toSend = {
            {"Message 1"},
            {"Mesasge 2"}
    };
    std::vector<Msg> got;

    PipePublisher<Msg> publisher;
    auto client = publisher.NewClient(1024);

    WorkerThread clientThread;
    clientThread.Start();

    // Install an initial trigger, in this somewhat contrived case, all
    // it does is install the real trigger
    clientThread.DoTask([&] () -> void {
        auto consumeMessages = [&] () -> void {
            Msg msg;
            while (client->GetNextMessage(msg)) {
                got.push_back(msg);
            }
        };

        client->OnNextMessage([=, &client, &clientThread] () -> void {
            // Only the client thread is allowed to install these...
            // It is safe to make a blocking call
            clientThread.DoTask([=, &client] () -> void {
                client->OnNextMessage(consumeMessages);
            });
        });
    });

    // First trigger only resets the trigger, nothing actually recieved
    publisher.Publish(toSend[0]);
    ASSERT_NO_FATAL_FAILURE(MessagesMatch({},got));

    // Second trigger only pull all the messages
    publisher.Publish(toSend[1]);
    ASSERT_NO_FATAL_FAILURE(MessagesMatch({toSend[0], toSend[1]},got));
}

TEST(TPipeSubscriber,WaitForMessage) {
    PipePublisher<Msg> publisher;
    WorkerThread pushThread;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;

    // Start pushing messages...
    pushThread.Start();
    pushThread.PostTask([&] () -> void {
        for (auto& msg : toSend ) {
            publisher.Publish(msg);
        }
        publisher.Done();
    });

    Msg msg;
    while (client->WaitForMessage(msg)) {
        // This double copy isn't particularly efficient -
        // but its a test. get over it.
        got.push_back(msg);
    }


    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

TEST(TPipeSubscriber,BatchNotify) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };

    client->OnNextMessage(f);

    publisher.StartBatch();

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    std::vector<Msg> expected = { };

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    publisher.EndBatch();

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

TEST(TPipeSubscriber,PostNotify) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };
    
    struct PostableTarget: public IPostable {
        PostableTarget() : posted_count(0) { } 
        void PostTask(const Task& task) {
            ++posted_count;
            task();
        }

        size_t posted_count;
    } dummy_event_loop;

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };
    
    client->OnNextMessage(f,&dummy_event_loop);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    std::vector<Msg> expected = {
        {"Message 1"}
    };

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    ASSERT_EQ(dummy_event_loop.posted_count, 1);

    client->OnNextMessage(f);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));

    // Assert that the task was not re-posted
    ASSERT_EQ(dummy_event_loop.posted_count, 1);

    got.clear();
    publisher.Publish(expected[0]);

    client->OnNextMessage(f,&dummy_event_loop);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    ASSERT_EQ(dummy_event_loop.posted_count, 2);
}

TEST(TPipePublisher,DoubleConsumer) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };
    std::thread worker(f);
    worker.join();

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));

    std::vector<Msg> toSend2 = {
        {"2nd Message 1"},
        {"2nd Mesasge 2"},
        {"2nd Hello World!"}
    };
    std::shared_ptr<PipeSubscriber<Msg>> client2(publisher.NewClient(1024));

    for (auto& msg : toSend2 ) {
        publisher.Publish(msg);
    }

    got.clear();
    auto f2 = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };
    std::vector<Msg> got2;
    auto f3 = [&] () -> void {
        Msg recvMsg;
        while(client2->GetNextMessage(recvMsg)) {
            got2.push_back(recvMsg);
        }
    };
    std::thread worker2(f2);
    std::thread worker3(f3);
    worker3.join();
    worker2.join();

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend2,got));
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend2,got2));
}

TEST(TPipeSubscriber,ForEachData) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
    };
    
    client->OnNewMessage(f);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

TEST(TPipeSubscriber,InitialData) {
    PipePublisher<Msg> publisher;
    std::vector<Msg> toSend = {
            {"Message 1"},
            {"Mesasge 2"},
            {"Hello World!"}
    };
    auto sent = toSend;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024, std::move(toSend)));

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
    };

    client->OnNewMessage(f);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(sent,got));
}

TEST(TPipeSubscriber,BatchForEachData) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
    };

    client->OnNewMessage(f);
    publisher.StartBatch();

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

TEST(TIPipeConsumer,CustomHandler) {
    PipePublisher<Msg> publisher;
    class Handler: public IPipeConsumer<Msg> {
    public:
        Handler(PipePublisher<Msg>* parent) { }

        virtual void PushMessage(const Msg& m) {
            got.push_back(m);
        }
        std::vector<Msg> got;
    };
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::shared_ptr<Handler> client(publisher.NewClient<Handler>());

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,client->got));
}

TEST(TIPipeConsumer,CustomBatchHandler) {
    PipePublisher<Msg> publisher;
    class Handler: public IPipeConsumer<Msg> {
    public:
        Handler(PipePublisher<Msg>* parent) {
            startBatch = 0;
            endBatch = 0;
        }

        virtual void PushMessage(const Msg& m) {
            got.push_back(m);
        }

        virtual void StartBatch() {
            startBatch++;
        }

        virtual void EndBatch() {
            endBatch++;
        }
        std::vector<Msg> got;
        size_t           startBatch;
        size_t           endBatch;
    };
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::shared_ptr<Handler> client(publisher.NewClient<Handler>());

    publisher.StartBatch();

    ASSERT_EQ(client->startBatch, 1);
    ASSERT_EQ(client->endBatch, 0);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);

        ASSERT_EQ(client->startBatch, 1);
        ASSERT_EQ(client->endBatch, 0);
    }


    publisher.EndBatch();

    ASSERT_EQ(client->startBatch, 1);
    ASSERT_EQ(client->endBatch, 1);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,client->got));
}

TEST(TIPipeConsumer,CustomBatchReset) {
    std::unique_ptr<PipePublisher<Msg>> publisher(new PipePublisher<Msg>);
    class Handler: public IPipeConsumer<Msg> {
    public:
        Handler(PipePublisher<Msg>* parent) {
            startBatch = 0;
            endBatch = 0;
        }

        virtual void PushMessage(const Msg& m) { }

        virtual void StartBatch() {
            startBatch++;
        }

        virtual void EndBatch() {
            endBatch++;
        }
        size_t           startBatch;
        size_t           endBatch;
    };

    std::shared_ptr<Handler> client(publisher->NewClient<Handler>());

    publisher->StartBatch();

    ASSERT_EQ(client->startBatch, 1);
    ASSERT_EQ(client->endBatch, 0);

    publisher->StartBatch();

    ASSERT_EQ(client->startBatch, 2);
    ASSERT_EQ(client->endBatch, 1);

    publisher.reset(nullptr);

    ASSERT_EQ(client->startBatch, 2);
    ASSERT_EQ(client->endBatch, 2);
}

TEST(TPipePublisher,InstallCustomHandler) {
    PipePublisher<Msg> publisher;
    class Handler: public IPipeConsumer<Msg> {
    public:
        virtual void PushMessage(const Msg& m) {
            got.push_back(m);
        }
        std::vector<Msg> got;
    };
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::shared_ptr<Handler> client(new Handler);

    publisher.InstallClient(client);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,client->got));
}

TEST(TPipeSubscriber,ForEachDataUnread) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"}
    };

    std::vector<Msg> toSend2 = {
        {"Hello World!"}
    };

    std::vector<Msg> end_result = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
    };
    
    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    client->OnNewMessage(f);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));

    for (auto& msg : toSend2 ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(end_result,got));
}

TEST(PipeSubscriber,Abort) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    client->Abort();

    Msg finalMessage = {"This should not be published"};
    publisher.Publish(finalMessage);

    std::vector<Msg> got;
    auto f = [&] () -> void {
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
    };
    std::thread worker(f);
    worker.join();

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

TEST(TIPipeConsumer,CustomAbort) {
    PipePublisher<Msg> publisher;

    class Handler: public IPipeConsumer<Msg> {
    public:
        virtual void PushMessage(const Msg& m) {
            got.push_back(m);
        }
        std::vector<Msg> got;
    };

    std::shared_ptr<Handler> client(new Handler);

    publisher.InstallClient(client);

    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    publisher.StartBatch();
    client->Abort();

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    publisher.EndBatch();

    Msg finalMessage = {"This should not be published"};
    publisher.Publish(finalMessage);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,client->got));
}

TEST(TIPipeConsumer,CustomBatchDone) {
    PipePublisher<Msg> publisher;

    class Handler: public IPipeConsumer<Msg> {
    public:
        Handler() : endBatchCalls(0) { }
        virtual void PushMessage(const Msg& m) {
            got.push_back(m);
        }
        std::vector<Msg> got;

        // Track calls to the OnStateChange callback
        void OnStateChange() {
            stateChanges.push_back(State());
        }

        // Track calls to endBatchCalls
        void EndBatch() {
            endBatchCalls++;
        }

        void ValidateStateChanges() {
            ASSERT_EQ(stateChanges.size(), 1);
            ASSERT_EQ(stateChanges[0], Handler::FINSIHED);
        }

        std::vector<STATE> stateChanges;
        size_t             endBatchCalls;
    };

    std::shared_ptr<Handler> client(new Handler);

    publisher.InstallClient(client);

    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    publisher.StartBatch();

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    publisher.Done();

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,client->got));

    ASSERT_EQ(client->endBatchCalls, 1);

    ASSERT_NO_FATAL_FAILURE(client->ValidateStateChanges());
}

TEST(TPipeSubscriber,AbortNotify) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> got;
    size_t callCount = 0;
    auto f = [&] () -> void {
        ++callCount;
        Msg recvMsg;
        while(client->GetNextMessage(recvMsg)) {
            got.push_back(recvMsg);
        }
        client->Abort();
    };
    
    client->OnNextMessage(f);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    std::vector<Msg> expected = {
        {"Message 1"}
    };

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    client->OnNextMessage(f);

    f();
    // No more messages should have been published...
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    ASSERT_EQ(callCount, 2);
}

TEST(TPipeSubscriber,AbortForEachData) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> expected = {
        {"Message 1"}
    };

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
        client->Abort();
    };
    
    client->OnNewMessage(f);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));
}

TEST(TPipeSubscriber,AbortForEachDataUnread) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"}
    };

    std::vector<Msg> toSend2 = {
        {"Hello World!"}
    };

    std::vector<Msg> expected = {
        {"Message 1"}
    };

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
        client->Abort();
    };
    
    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    client->OnNewMessage(f);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    for (auto& msg : toSend2 ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));
}

TEST(TPipeSubscriber,AbortHandleDestruction) {
    PipePublisher<Msg> publisher;
    std::shared_ptr<PipeSubscriber<Msg>> client(publisher.NewClient(1024));
    std::vector<Msg> toSend = {
        {"Message 1"},
        {"Mesasge 2"},
        {"Hello World!"}
    };

    std::vector<Msg> expected = {
        {"Message 1"}
    };

    std::vector<Msg> got;
    auto f = [&] (const Msg& newMsg) -> void {
        got.push_back(newMsg);
        client.reset();
    };
    
    client->OnNewMessage(f);

    for (auto& msg : toSend ) {
        publisher.Publish(msg);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));
}
