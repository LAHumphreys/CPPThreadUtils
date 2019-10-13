#include <PipePublisher.h>
#include <gtest/gtest.h>
#include <WorkerThread.h>

using namespace std;

struct Msg {
    std::string   message;
};

void MessagesMatch(std::vector<Msg>& sent,
                   std::vector<Msg>& got)
{
    ASSERT_EQ(sent.size(), got.size());

    for (size_t i = 0; i < sent.size(); ++i) {
        Msg& msg = sent[i];
        Msg& recvd = got[i];

        ASSERT_EQ(msg.message, recvd.message);
    }
}

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

TEST(TPipeSubscriber,Notify) {
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

    std::vector<Msg> expected = {
        {"Message 1"}
    };

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,got));

    client->OnNextMessage(f);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,got));
}

TEST(TPipeSubscriber,WaitForMessage) {
    WorkerThread pushThread;
    PipePublisher<Msg> publisher;
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
