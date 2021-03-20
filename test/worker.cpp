#include <WorkerThread.h>
#include <util_time.h>
#include <gtest/gtest.h>
#include <chrono>
#include <AggPipePub.h>


using namespace nstimestamp;
using namespace std::chrono_literals;

TEST(TWorker,PostSingleTask) {
    WorkerThread worker;
    worker.Start();

    std::thread::id this_thread = std::this_thread::get_id();
    std::thread::id write_id;
    std::mutex target_mutex;
    std::atomic<size_t> target(0);

    auto f = [&] () -> void {
        std::unique_lock<std::mutex> lock(target_mutex);
        target = 1;
        write_id = std::this_thread::get_id();
    };

    std::unique_lock<std::mutex> lock(target_mutex);
    worker.PostTask(f);

    while (target == 0){
        lock.unlock();
        std::this_thread::yield();
        lock.lock();
    }

    ASSERT_EQ(target, 1);
    ASSERT_NE(write_id, this_thread);
}

TEST(TWorker,ChainTasks) {

    std::thread::id this_thread = std::this_thread::get_id();
    WorkerThread worker;
    worker.Start();

    std::mutex target_mutex;
    std::vector<size_t> targets = { 0, 0 ,0};

    std::vector<std::thread::id> write_ids;
    write_ids.resize(3);

    std::vector<Time> write_times;
    write_times.resize(3);

    auto f1 = [&] () -> void {
        std::unique_lock<std::mutex> lock(target_mutex);
        targets[1] = 1;
        write_times[1].SetNow();
        write_ids[1] = std::this_thread::get_id();
    };

    auto f2 = [&] () -> void {
        std::unique_lock<std::mutex> lock(target_mutex);
        targets[2] = 2;
        write_times[2].SetNow();
        write_ids[2] = std::this_thread::get_id();
    };

    auto f0 = [&] () -> void {
        std::unique_lock<std::mutex> lock(target_mutex);
        worker.PostTask(f1);
        worker.PostTask(f2);
        targets[0] = 0;
        write_times[0].SetNow();
        write_ids[0] = std::this_thread::get_id();
    };

    std::unique_lock<std::mutex> lock(target_mutex);
    worker.PostTask(f0);

    while (targets[2] == 0){
        lock.unlock();
        std::this_thread::yield();
        lock.lock();
    }

    for (size_t i =0; i < 3; ++i) {
        size_t& target = targets[i];
        ASSERT_EQ(target, i);
        std::thread::id& write_id = write_ids[i];
        ASSERT_NE(write_id, this_thread);
    }

    ASSERT_GT(write_times[1].DiffNSecs(write_times[0]), 0);
    ASSERT_GT(write_times[2].DiffNSecs(write_times[1]), 0);
}

TEST(TWorker,ExternalAbort) {
    WorkerThread worker;
    size_t target1 = 0;
    size_t target2 = 0;
    std::mutex write_mutex;
    std::condition_variable permission_to_continue;
    std::condition_variable write_complete;

    auto f1 = [&] () -> void {
        std::unique_lock<std::mutex> lock(write_mutex);
        target1 = 1;
        write_complete.notify_all();
        permission_to_continue.wait(lock);
    };

    auto f2 = [&] () -> void {
        std::unique_lock<std::mutex> lock(write_mutex);
        target2 = 1;
    };


    std::unique_lock<std::mutex> lock(write_mutex);

    worker.PostTask(f1);
    worker.PostTask(f2);

    worker.Start();

    write_complete.wait(lock);
    worker.Abort();
    permission_to_continue.notify_all();
    lock.unlock();
    worker.Join();

    ASSERT_EQ(target1, 1);
    ASSERT_EQ(target2, 0);
}

TEST(TWorker,Abort_WaitingJob) {
    std::promise<bool> readyToAbort;
    std::promise<bool> taskResult;

    WorkerThread eventLoop;
    WorkerThread clientThread;

    eventLoop.Start();
    eventLoop.PostTask([&] () -> void {
        if (readyToAbort.get_future().get()) {
            std::this_thread::yield();
            std::this_thread::sleep_for(10us);
            std::this_thread::yield();
            eventLoop.Abort();
        }
    });
    clientThread.Start();
    clientThread.PostTask([&] () -> void {
        readyToAbort.set_value((true));
        taskResult.set_value( eventLoop.DoTask([] () -> void { }) );
    });

    ASSERT_FALSE(taskResult.get_future().get());
}

TEST(TWorker, InternalAbort) {
    WorkerThread worker;
    size_t target1 = 0;
    size_t target2 = 0;
    std::mutex write_mutex;

    auto f1 = [&] () -> void {
        std::unique_lock<std::mutex> lock(write_mutex);
        target1 = 1;
        worker.Abort();
    };

    auto f2 = [&] () -> void {
        std::unique_lock<std::mutex> lock(write_mutex);
        target2 = 1;
    };


    std::unique_lock<std::mutex> lock(write_mutex);

    worker.PostTask(f1);
    worker.PostTask(f2);

    worker.Start();
    lock.unlock();
    worker.Join();

    ASSERT_EQ(target1, 1);
    ASSERT_EQ(target2, 0);
}

TEST(TWorker, WaitForTask) {
    WorkerThread worker;
    worker.Start();

    std::thread::id this_thread = std::this_thread::get_id();
    std::thread::id write_id;
    std::mutex target_mutex;
    std::atomic<size_t> target(0);

    auto f = [&] () -> void {
        std::unique_lock<std::mutex> lock(target_mutex);
        target = 1;
        write_id = std::this_thread::get_id();
    };

    ASSERT_TRUE(worker.DoTask(f));

    std::unique_lock<std::mutex> lock(target_mutex);

    ASSERT_EQ(target, 1);
    ASSERT_NE(write_id, this_thread);
}

TEST(TWorker, NoRestart_NoParallelStart) {
    WorkerThread targetThread;
    std::vector<WorkerThread> contendingThreads(20);

    std::atomic<size_t> hits(0);
    std::atomic<bool> running;

    for (auto& contendingThread: contendingThreads) {
        contendingThread.PostTask([&] () -> void {
            if (targetThread.Start()) {
                ++hits;
            }
        });
    }
    for (auto& contendingThread: contendingThreads) {
        contendingThread.Start();
    }

    // flush the threads
    for (auto& contendingThread: contendingThreads) {
        contendingThread.DoTask([&] () -> void { });
    }

    ASSERT_EQ(hits, 1);

    targetThread.DoTask([&] () -> void {
        running = true;
    });

    // check it started ok
    ASSERT_TRUE(running);
}

// The state model currently can't handle a second start
TEST(TWorker, NoRestart_WhilstRunning) {
    std::promise<bool> finishFlag, startFlag;
    auto finishFlag_future = finishFlag.get_future();
    auto startFlag_future = startFlag.get_future();

    WorkerThread worker;
    ASSERT_TRUE(worker.Start());

    // Wait for the worker to be in the middle of doing some work
    worker.PostTask([&] () -> void {
        startFlag.set_value(true);
        ASSERT_TRUE(finishFlag_future.get());
    });
    ASSERT_TRUE(startFlag_future.get());

    // No restart whilst we're running
    ASSERT_FALSE(worker.Start());

    // release the worker
    finishFlag.set_value(true);
}

TEST(TWorker, NoRestart_WhilstSleeping) {
    WorkerThread worker;
    ASSERT_TRUE(worker.Start());

    // Flush it into a sleeping state
    worker.DoTask([] () -> void { });

    // This is a little in-precise, since technically there's a race condition here. If the
    // worker immediately gets shelved by the scheduler then it is possible it doesn't fall
    // all the way back to sleep before we continue. Give it the best possible chance.
    std::this_thread::yield();
    std::this_thread::sleep_for(10us);
    std::this_thread::yield();

    ASSERT_FALSE(worker.Start());

}

TEST(TWorker, NoRestart_AfterAbort) {
    WorkerThread worker;
    ASSERT_TRUE(worker.Start());

    // Flush it into a sleeping state
    worker.DoTask([] () -> void { });

    worker.Abort();

    ASSERT_FALSE(worker.Start());

}

TEST(TWorker, OnThread) {
    WorkerThread worker, other;
    ASSERT_FALSE(worker.CurrentlyOnWorkerThread());

    worker.Start();
    worker.DoTask([&] () -> void {
        ASSERT_TRUE(worker.CurrentlyOnWorkerThread());
    });
    ASSERT_FALSE(worker.CurrentlyOnWorkerThread());

    other.Start();
    other.DoTask([&] () -> void {
        ASSERT_FALSE(worker.CurrentlyOnWorkerThread());
    });
}

struct Msg {
    Time t;
    std::string message;
    std::string id;
};


void MessagesMatch(const std::vector<Msg>& sent,
                   const std::vector<Msg>& got)
{
    bool match = true;
    ASSERT_EQ(sent.size(), got.size());

    for (size_t i = 0; match && i < sent.size(); ++i) {
        const Msg& msg = sent[i];
        const Msg& recvd = got[i];
        ASSERT_EQ(msg.message, recvd.message);
        ASSERT_EQ(msg.t.DiffUSecs(recvd.t), 0);
        ASSERT_EQ(msg.id, recvd.id);
    }
}

TEST(TSubsriberWorker, SingleClient) {
    PipePublisher<Msg> publisher;
    WorkerThread worker;
    std::vector<Msg> messages;
    std::mutex  comms_mutex;
    std::condition_variable wait_for_complete;

    std::vector<Msg> toSend {
        {Time(), "Hello"},
        {Time(), "World!"}
    };

    std::function<void (Msg&)> push = 
        [&comms_mutex, &wait_for_complete, &toSend, &messages] (Msg& m) -> void {
            messages.push_back(m);
            if ( messages.size() == toSend.size()) {
                std::unique_lock<std::mutex> lock(comms_mutex);
                wait_for_complete.notify_all();
            }
        };

    worker.Start();

    worker.ConsumeUpdates(publisher,push,1000,1000);

    std::unique_lock<std::mutex> lock(comms_mutex);

    for (Msg& m: toSend) {
        publisher.Publish(m);
    }

    wait_for_complete.wait(lock);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,messages));
}

TEST(TSubsriberWorker, AggClient) {
    constexpr auto NoDupes =
        AggPipePub_Config::AggPipePubUpdateMode::NO_DUPLICATE_CHECKING;
    using Publisher = AggPipePub<Msg, NoDupes>;
    Publisher publisher;
    WorkerThread worker;
    std::vector<Msg> messages;
    std::mutex  comms_mutex;
    std::condition_variable wait_for_complete;

    std::vector<Msg> toSend {
            {Time(), "Hello", "first"},
            {Time(), "World!", "second"}
    };

    std::function<void (Publisher::Upd&)> push =
            [&comms_mutex, &wait_for_complete, &toSend, &messages](Publisher::Upd& m) -> void {
                messages.push_back(*m.data);
                if ( messages.size() == toSend.size()) {
                    std::unique_lock<std::mutex> lock(comms_mutex);
                    wait_for_complete.notify_all();
                }
            };

    worker.Start();

    auto client = publisher.NewClient(1024);

    worker.ConsumeUpdates(client, push);

    std::unique_lock<std::mutex> lock(comms_mutex);

    for (Msg& m: toSend) {
        auto update = std::make_shared<Msg>(m);
        publisher.Update(update);
    }

    wait_for_complete.wait(lock);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend,messages));
}

TEST(TSubsriberWorker, TwoClients) {
    PipePublisher<Msg> publisher;
    WorkerThread worker;
    std::vector<Msg> messages;
    std::vector<Msg> messages2;
    std::mutex  comms_mutex;
    std::mutex  comms_mutex2;
    std::condition_variable wait_for_complete;
    std::atomic<bool> done1(false);
    std::atomic<bool> done2(false);

    std::vector<Msg> toSend1 {
        {Time(), "Hello"},
        {Time(), "World!"}
    };

    std::vector<Msg> toSend2 {
        {Time(), "Another String"},
        {Time(), "And Another"}
    };

    std::function<void (Msg&)> push = [&] (Msg& m) -> void {
			std::unique_lock<std::mutex> lock(comms_mutex);
            messages.push_back(m);
            if ( messages.size() == 2) {
                std::unique_lock<std::mutex> lock2(comms_mutex2);
                done1 = true;
                wait_for_complete.notify_all();
            }
        };

    std::function<void (Msg&)> push2 = [&] (Msg& m) -> void {
			std::unique_lock<std::mutex> lock(comms_mutex);
            messages2.push_back(m);
            if ( messages2.size() == 2) {
                std::unique_lock<std::mutex> lock2(comms_mutex2);
                done2 = true;
                wait_for_complete.notify_all();
            }
        };

    worker.Start();

    worker.ConsumeUpdates(publisher,push,1000,1000);

    std::unique_lock<std::mutex> lock(comms_mutex);

    for (Msg& m: toSend1) {
        publisher.Publish(m);
    }

    wait_for_complete.wait(lock);

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend1,messages));
    ASSERT_NO_FATAL_FAILURE(MessagesMatch({},messages2));

    messages.clear();

    done1 = false;
    done2 = false;

    worker.ConsumeUpdates(publisher,push2,1000,1000);


    for (Msg& m: toSend2) {
        publisher.Publish(m);
    }

    wait_for_complete.wait(lock);
    if (!done1 || !done2) {
        wait_for_complete.wait(lock);
    }

    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend2,messages));
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(toSend2,messages2));
}

TEST(TSubsriberWorker, SliceSize) {
    PipePublisher<Msg> publisher;
    WorkerThread worker;
    std::vector<Msg> messages;
    std::mutex  comms_mutex;
    std::condition_variable wait_for_complete;

    std::vector<Msg> toSend {
        {Time(), ""},
        {Time(), ""},
        {Time(), ""},
        {Time(), ""},
        {Time(), ""}
    };
    const size_t toGet = toSend.size();

    std::vector<Msg> expected;
    for (Msg& m: toSend) {
        expected.push_back({m.t,"ONE"});
        expected.push_back({m.t,"TWO"});
    }

    size_t count1 = 0;
    size_t count2 = 0;
    auto f = [&comms_mutex, &messages]
             (size_t& count, Msg& m, std::condition_variable& doneFlag, size_t toGet, std::string name) -> void {
                std::unique_lock<std::mutex> lock(comms_mutex);
                messages.push_back({m.t,name});
                ++count;
                if (count == toGet) {
                    doneFlag.notify_all();
                }
            };

    std::function<void(Msg&)> task1 = [&f, &count1, &wait_for_complete, &toGet]
                 (Msg& m) -> void {
                     f(count1, m,wait_for_complete,toGet,"ONE");
                 };

    std::function<void(Msg&)> task2 = [&f, &count2, &wait_for_complete, &toGet]
                 (Msg& m) -> void {
                     f(count2, m,wait_for_complete,toGet,"TWO");
                 };

    worker.Start();
    std::unique_lock<std::mutex> lock(comms_mutex);

    worker.ConsumeUpdates(publisher,task1,100,1);
    worker.ConsumeUpdates(publisher,task2,100,1);

    for (Msg& m: toSend) {
        publisher.Publish(m);
    }

    while (count1 != toGet || count2 != toGet) {
        wait_for_complete.wait(lock);
    }
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(expected,messages));
}
