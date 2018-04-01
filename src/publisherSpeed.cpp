#include <iostream>
#include <PipePublisher.h>
#include <util_time.h>
#include <iomanip>
#include <sstream>
#include <memory>
#include <queue>
#include <WorkerThread.h>

struct Msg {
    long   i;
    int    j;
    short  k;
    double d;
};
using namespace std;
using namespace nstimestamp;

size_t COUNT = 1000000; // SHORT
// LONG:  const size_t COUNT = 10000000;
//const size_t COUNT = 1000;
long baseline_duration = 0;


void Header(size_t n);
void Footer();
void DoTimedTest(const std::string& name,
                 size_t n,
                 std::function<void(size_t count)> f);
/*
 * Tests
 */
void BaselineLoop(size_t count);
void BaselineQueue(size_t count);
void EmptyPush(size_t count);
void PassiveClients(size_t count, size_t clients);
void IgnoringClients(size_t count, size_t clients);

void ThreadConsumers(size_t count, size_t clients, size_t threads);
void ThreadConsumersBatched(size_t count, size_t clients, size_t threads);

void OnNewMessage(size_t count, size_t clients);
void OnNewMessageBatched(size_t count, size_t clients);
void OnNewMessageCustom(size_t count, size_t clients);

int main(int argc, const char *argv[])
{
    std::map<std::string,std::string> args;
    for (int i = 0; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.length() > 1 && arg[0] == '-') {
            arg = arg.substr(1);
            size_t split_pos = arg.find("=");
            if (split_pos != std::string::npos) {
                std::string name = arg.substr(0,split_pos);
                std::string value = arg.substr(split_pos+1);
                args[name] = value;
            } else {
                args[arg] = "SET";
            }
        }
    }


    bool threads = (args["noThreads"] != "SET");

    if (args["count"] != "") {
        long tmpCount = atol(args["count"].c_str());
        if (tmpCount > 0) {
            COUNT = static_cast<size_t>(tmpCount);
        }
    }

    if (args["threadTorture"] != "") {
        DoTimedTest("Pushing to 10 client, 3 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,10,3); });
        return 0;
    }

    Header(COUNT);
    DoTimedTest("Baseline Loop",COUNT, BaselineLoop);
    DoTimedTest("Baseline Queue",COUNT, BaselineQueue);

    /**
     * Pushing when there are no clients...
     */
    Footer();
    DoTimedTest("Pushing with no clients",COUNT, EmptyPush);

    /**
     * Pushing to clients who are not consuming data (the queue is just getting bigger...)
     */
    Footer();
    DoTimedTest("Pushing to 1 client, no reads",COUNT, [] (size_t count) -> void { IgnoringClients(count,1);});
    DoTimedTest("Pushing to 2 clients, no reads",COUNT, [] (size_t count) -> void { IgnoringClients(count,2);});
    DoTimedTest("Pushing to 3 clients, no reads",COUNT, [] (size_t count) -> void { IgnoringClients(count,3);});
    DoTimedTest("Pushing to 5 clients, no reads",COUNT, [] (size_t count) -> void { IgnoringClients(count,5);});
    DoTimedTest("Pushing to 10 clients, no reads",COUNT, [] (size_t count) -> void { IgnoringClients(count,10);});

    Footer();
    DoTimedTest("Pushing to 1 client, single thread",COUNT, [] (size_t count) -> void { PassiveClients(count,1); });
    DoTimedTest("Pushing to 2 client, single thread",COUNT, [] (size_t count) -> void { PassiveClients(count,2); });
    DoTimedTest("Pushing to 3 client, single thread",COUNT, [] (size_t count) -> void { PassiveClients(count,3); });
    DoTimedTest("Pushing to 5 client, single thread",COUNT, [] (size_t count) -> void { PassiveClients(count,5); });
    DoTimedTest("Pushing to 10 client, single thread",COUNT, [] (size_t count) -> void { PassiveClients(count,10); });

    Footer();
    DoTimedTest("Fowarding to 1 client, single thread",COUNT, [] (size_t count) -> void { OnNewMessage(count,1); });
    DoTimedTest("Fowarding to 2 client, single thread",COUNT, [] (size_t count) -> void { OnNewMessage(count,2); });
    DoTimedTest("Fowarding to 3 client, single thread",COUNT, [] (size_t count) -> void { OnNewMessage(count,3); });
    DoTimedTest("Fowarding to 5 client, single thread",COUNT, [] (size_t count) -> void { OnNewMessage(count,5); });
    DoTimedTest("Fowarding to 10 client, single thread",COUNT, [] (size_t count) -> void { OnNewMessage(count,10); });

    Footer();
    DoTimedTest("Fowarding to 1 client, single thread, batched",COUNT, [] (size_t count) -> void { OnNewMessageBatched(count,1); });
    DoTimedTest("Fowarding to 2 client, single thread, batched",COUNT, [] (size_t count) -> void { OnNewMessageBatched(count,2); });
    DoTimedTest("Fowarding to 3 client, single thread, batched",COUNT, [] (size_t count) -> void { OnNewMessageBatched(count,3); });
    DoTimedTest("Fowarding to 5 client, single thread, batched",COUNT, [] (size_t count) -> void { OnNewMessageBatched(count,5); });
    DoTimedTest("Fowarding to 10 client, single thread, batched",COUNT, [] (size_t count) -> void { OnNewMessageBatched(count,10); });

    Footer();
    DoTimedTest("Fowarding to 1 custom client, single thread",COUNT, [] (size_t count) -> void { OnNewMessageCustom(count,1); });
    DoTimedTest("Fowarding to 2 custom client, single thread",COUNT, [] (size_t count) -> void { OnNewMessageCustom(count,2); });
    DoTimedTest("Fowarding to 3 custom client, single thread",COUNT, [] (size_t count) -> void { OnNewMessageCustom(count,3); });
    DoTimedTest("Fowarding to 5 custom client, single thread",COUNT, [] (size_t count) -> void { OnNewMessageCustom(count,5); });
    DoTimedTest("Fowarding to 10 custom client, single thread",COUNT, [] (size_t count) -> void { OnNewMessageCustom(count,10); });

    if (threads) {
        Footer();
        DoTimedTest("Pushing to 1 client, 1 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,1,1); });
        DoTimedTest("Pushing to 2 client, 1 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,2,1); });
        DoTimedTest("Pushing to 3 client, 1 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,3,1); });
        DoTimedTest("Pushing to 5 client, 1 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,5,1); });
        DoTimedTest("Pushing to 10 client, 1 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,10,1); });


        Footer();
        DoTimedTest("Pushing to 2 client, 2 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,2,2); });
        DoTimedTest("Pushing to 3 client, 2 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,3,2); });
        DoTimedTest("Pushing to 5 client, 2 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,5,2); });
        DoTimedTest("Pushing to 10 client, 2 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,10,2); });

        Footer();
        DoTimedTest("Pushing to 3 client, 3 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,3,3); });
        DoTimedTest("Pushing to 5 client, 3 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,5,3); });
        DoTimedTest("Pushing to 10 client, 3 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,10,3); });

        Footer();
        DoTimedTest("Pushing to 5 client, 5 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,5,5); });
        DoTimedTest("Pushing to 10 client,5 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,10,5); });

        Footer();
        DoTimedTest("Pushing to 10 client,10 client thread",COUNT, [] (size_t count) -> void { ThreadConsumers(count,10,10); });

        Footer();
        DoTimedTest("Pushing to 1 client, 1 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,1,1); });
        DoTimedTest("Pushing to 2 client, 1 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,2,1); });
        DoTimedTest("Pushing to 3 client, 1 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,3,1); });
        DoTimedTest("Pushing to 5 client, 1 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,5,1); });
        DoTimedTest("Pushing to 10 client, 1 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,10,1); });


        Footer();
        DoTimedTest("Pushing to 2 client, 2 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,2,2); });
        DoTimedTest("Pushing to 3 client, 2 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,3,2); });
        DoTimedTest("Pushing to 5 client, 2 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,5,2); });
        DoTimedTest("Pushing to 10 client, 2 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,10,2); });

        Footer();
        DoTimedTest("Pushing to 3 client, 3 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,3,3); });
        DoTimedTest("Pushing to 5 client, 3 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,5,3); });
        DoTimedTest("Pushing to 10 client, 3 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,10,3); });

        Footer();
        DoTimedTest("Pushing to 5 client, 5 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,5,5); });
        DoTimedTest("Pushing to 10 client,5 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,10,5); });

        Footer();
        DoTimedTest("Pushing to 10 client,10 client thread, batched",COUNT, [] (size_t count) -> void { ThreadConsumersBatched(count,10,10); });
    }

    Footer();
    return 0;
}


int dummy(const Msg& m);
void BaselineLoop(size_t count) {
    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        dummy(m);
    }
}

void BaselineQueue(size_t count) {
    std::queue<Msg> queue;
    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        queue.push(m);
    }

    for (size_t i = 0; i < count; ++i)
    {
        Msg& m = queue.front();
        dummy(m);
        queue.pop();
    }
}
void EmptyPush(size_t count) {
    PipePublisher<Msg> publisher;
    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
    }
}

void IgnoringClients(size_t count, size_t clients) {
    PipePublisher<Msg> publisher;
    std::vector<std::shared_ptr<PipeSubscriber<Msg>>> client_list;
    client_list.reserve(clients);

    for (size_t i =0; i < clients; ++i) {
        client_list.push_back(publisher.NewClient(count));
    }

    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
    }
}

void PassiveClients(size_t count, size_t clients) {
    PipePublisher<Msg> publisher;

    std::vector<std::shared_ptr<PipeSubscriber<Msg>>> client_list;
    client_list.reserve(clients);

    for (size_t i =0; i < clients; ++i) {
        client_list.push_back(publisher.NewClient(count));
    }

    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
    }

    for (size_t i =0; i < clients; ++i) {
        auto& client = client_list[i];
        Msg m;
        while (client->GetNextMessage(m)) {
            dummy(m);
        }
    }
}

class ClientConsumer {
public:
    ClientConsumer(
        size_t udpatesToGet,
        WorkerThread& worker,
        PipePublisher<Msg>& publisher);

    void WaitForCompletion();

private:
    void NotifyDone();
    std::mutex               completion_mutex;
    std::condition_variable  completion_flag;
    bool                     done;
    bool                     waiting;
    size_t count;

};

ClientConsumer::ClientConsumer(
    size_t updatesToGet,
    WorkerThread& worker,
    PipePublisher<Msg>& publisher)
    : done(false), waiting(false), count(0)
{
    std::function<void (Msg&)> task = [&publisher, this, updatesToGet]
                                      (Msg& m) -> void {
        dummy(m);
        this->count += 1;
        if (this->count >= updatesToGet) {
            this->NotifyDone();
        }
    };

    worker.ConsumeUpdates(publisher,task,updatesToGet,1000);
}

inline void ClientConsumer::NotifyDone() {
    std::unique_lock<std::mutex> lock(this->completion_mutex);
    done = true;
    if (waiting) {
        this->completion_flag.notify_all();
    }
}

void ClientConsumer::WaitForCompletion() {
    std::unique_lock<std::mutex> lock(this->completion_mutex);
    if (!done) {
        waiting = true;
        this->completion_flag.wait(lock);
    }
}

void ThreadConsumers(size_t count, size_t clients, size_t threads) {
    PipePublisher<Msg> publisher;
    std::map<size_t,WorkerThread> workers;
    std::map<size_t,ClientConsumer> consumers;

    for (size_t i =0; i < threads; ++i) {
        WorkerThread& worker = workers[i];
        worker.Start();
    }

    for (size_t i = 0; i < clients; ++i) {
        WorkerThread& worker = workers[i%threads];
        consumers.emplace(std::piecewise_construct,
                          std::forward_as_tuple(i),
                          std::forward_as_tuple(count, worker, publisher));
    }

    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
    }

    for (size_t i = 0; i < clients; ++i) {
        auto it = consumers.find(i);
        it->second.WaitForCompletion();
    }


}

void ThreadConsumersBatched(size_t count, size_t clients, size_t threads) {
    PipePublisher<Msg> publisher;
    std::map<size_t,WorkerThread> workers;
    std::map<size_t,ClientConsumer> consumers;

    for (size_t i =0; i < threads; ++i) {
        WorkerThread& worker = workers[i];
        worker.Start();
    }

    for (size_t i = 0; i < clients; ++i) {
        WorkerThread& worker = workers[i%threads];
        consumers.emplace(std::piecewise_construct,
                          std::forward_as_tuple(i),
                          std::forward_as_tuple(count, worker, publisher));
    }

    publisher.StartBatch();
    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
        if ( i % 1000 == 0) {
            publisher.StartBatch();
        }
    }
    publisher.EndBatch();

    for (size_t i = 0; i < clients; ++i) {
        auto it = consumers.find(i);
        it->second.WaitForCompletion();
    }


}

void OnNewMessage(size_t count, size_t num_clients) {
    PipePublisher<Msg> publisher;
    std::vector<std::shared_ptr<PipeSubscriber<Msg>>> clients;
    clients.reserve(num_clients);

    for (size_t i = 0; i < num_clients; ++i) {
        auto client = publisher.NewClient(count);

        client->OnNewMessage(dummy);
        clients.push_back(client);
    }

    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
    }

}

void OnNewMessageBatched(size_t count, size_t num_clients) {
    PipePublisher<Msg> publisher;
    std::vector<std::shared_ptr<PipeSubscriber<Msg>>> clients;
    clients.reserve(num_clients);

    for (size_t i = 0; i < num_clients; ++i) {
        auto client = publisher.NewClient(count);

        client->OnNewMessage(dummy);
        clients.push_back(client);
    }

    publisher.StartBatch();
    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
        if ( i % 1000 == 0) {
            publisher.StartBatch();
        }
    }
    publisher.EndBatch();

}

void OnNewMessageCustom(size_t count, size_t num_clients) {
    PipePublisher<Msg> publisher;

    class Handler: public IPipeConsumer<Msg> {
    public:
        void PushMessage(const Msg& m) {
            dummy(m);
        }
    };

    std::vector<std::shared_ptr<Handler>> clients;
    clients.reserve(num_clients);

    for (size_t i = 0; i < num_clients; ++i) {
        std::shared_ptr<Handler> client(new Handler);
        clients.push_back(client);
        std::shared_ptr<IPipeConsumer<Msg>> p_client = client;
        publisher.InstallClient(p_client);
    }

    for (size_t i = 0; i < count; ++i)
    {
        Msg m {1,2,3,4};
        publisher.Publish(m);
    }

}

void Header(size_t n) {
    cout << "| ";
    cout << setw(50) << "Test Name";
    cout << " | ";
    cout << setw(22) << "Duration (ms)";
    cout << " | ";
    cout << setw(14) << "Rate (/ms)";
    cout << " | ";
    cout << setw(14) << "Push Cost (ns)";
    cout << " |";
    cout << endl;
    cout << "|-";
    cout << setw(50) << setfill('-') << "";
    cout << "-|-";
    cout << setw(22) << setfill('-') << "";
    cout << "-|-";
    cout << setw(14) << setfill('-') << "";
    cout << "-|-";
    cout << setw(14) << setfill('-') << "";
    cout << "-|";
    cout << setfill(' ');
    cout << endl;
}

void Footer() {
    cout << "|-";
    cout << setw(50) << setfill('-') << "";
    cout << "-|-";
    cout << setw(22) << setfill('-') << "";
    cout << "-|-";
    cout << setw(14) << setfill('-') << "";
    cout << "-|-";
    cout << setw(14) << setfill('-') << "";
    cout << "-|";
    cout << setfill(' ');
    cout << endl;
}

void DoTimedTest(const std::string& name,
                 size_t n,
                 std::function<void(size_t count)> f)
{
    Time start;
    f(n);
    Time stop;
    long duration_us = stop.DiffUSecs(start);
    long duration_ms = duration_us / 1000;
    double rate =   (n) /  (1.0 * duration_ms);
    double cost =   1000* duration_us / (1.0 * n);

    if ( baseline_duration == 0 )
    {
        baseline_duration = duration_us;

        cout << "| ";
        cout << setw(50) << left << name;
        cout << " | ";
        cout << left << setw(22) << duration_ms;
        cout << " | ";
        cout << setw(14) << left << rate;
        cout << " | ";
        cout << setw(14) << left << cost;
        cout << " |";
    } else {
        long durationRatio = (duration_us / baseline_duration);
        cout << "| ";
        cout << setw(50) << left << name;
        cout << " | ";
        std::stringstream buf;
        buf << left << duration_ms << " (x" << durationRatio<< ")";
        cout << setw(22) << buf.str();
        cout << " | ";
        cout << setw(14) << left << rate;
        cout << " | ";
        cout << setw(14) << left << cost;
        cout << " |";
    }

    cout << endl;
}

