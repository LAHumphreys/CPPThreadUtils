//
// Created by lukeh on 12/10/2019.
//
#include <PipePublisher.h>
#include <gtest/gtest.h>
#include <AggPipePub.h>
#include <util_time.h>
#include <WorkerThread.h>

using namespace std;
using namespace nstimestamp;

struct Msg {
    Msg (std::string m, size_t i)
        : message(std::move(m))
        , _id(i) {}

    std::string   message;
    size_t        _id;
};
struct IdMsg: public Msg {
    IdMsg(const Msg& m)
            : Msg(m), id(m._id) {}

    size_t id;
};
struct WrappedMsg: public Msg {
    WrappedMsg (std::string m, size_t i)
        : Msg(std::move(m), i) {}

    [[nodiscard]] constexpr const auto& GetAggId() const { return _id;}
    [[nodiscard]] bool  IsAggEqual (const Msg& other) const {
        return (message == other.message);
    }
};
using Msgs = std::vector<Msg>;

using MsgAgg = AggPipePub<WrappedMsg>;
using Upds = std::vector<MsgAgg::Upd>;

struct ExpUpd {
    Msg           m;
    AggUpdateType updateType = AggUpdateType::NONE;
};
using ExpUpds = std::vector<ExpUpd>;

using ClientRef = std::shared_ptr<PipeSubscriber<MsgAgg::Upd>>;

void MessagesMatch(ExpUpds& exp, Upds& got)
{
    ASSERT_EQ(exp.size(), got.size());

    for (size_t i = 0; i < exp.size(); ++i) {
        const Msg& msg = exp[i].m;
        const Msg& recvd = *got[i].data;
        ASSERT_EQ(msg.message, recvd.message);
        ASSERT_EQ(msg._id, recvd._id);

        ASSERT_EQ(exp[i].updateType, got[i].updateType);
    }
}

void Publish(MsgAgg& pub, const Msg& m) {
    auto cpy = std::make_shared<WrappedMsg>(m.message, m._id);
    pub.Update(std::move(cpy));
}
void Publish(MsgAgg& pub, const Msgs& msgs) {
    for (const Msg& m: msgs) {
        Publish(pub, m);
    }
}

void GetMessages(const size_t toGet, ClientRef& cli, Upds& dest) {
    MsgAgg::Upd upd;
    for (size_t i = 0; i < toGet; ++i) {
        ASSERT_TRUE(cli->GetNextMessage(upd));
        dest.push_back(upd);
    }
}

void CheckClient(ClientRef client, ExpUpds& exp) {
    Upds got;
    ASSERT_NO_FATAL_FAILURE(GetMessages(exp.size(), client, got));
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(exp, got));
}

void PublishAndCheck (Msgs& toSend, ExpUpds& exp) {
    MsgAgg pub;
    ClientRef client = pub.NewClient(1024);

    Publish(pub, toSend);
    CheckClient(client, exp);
}

/*
 * As updates for new items arrive, they are immediately
 * published to any subscribed clients. They'll be published
 * with an updateType of NEW
 */
TEST(TAggPuplisher,NewItems) {
    Msgs toSend{
        Msg{"Hello World!", 0},
        Msg{"A new message", 1},
        Msg{"Yet another message", 2}
    };

    ExpUpds exp{
        {toSend[0], AggUpdateType::NEW},
        {toSend[1], AggUpdateType::NEW},
        {toSend[2], AggUpdateType::NEW}
    };

    PublishAndCheck(toSend, exp);
}


/*
 * When an existing item is updated, with a new value, it
 * is published to clients with an updateType of UPDATE
 */
TEST(TAggPuplisher,UpdatedItems) {
    Msgs toSend {
        Msg{"Hello World!", 0},
        Msg{"A new message", 1},
        Msg{"Hello New World!", 0},
        Msg{"Yet another message", 2}
    };

    ExpUpds exp {
        {toSend[0], AggUpdateType::NEW},
        {toSend[1], AggUpdateType::NEW},
        {toSend[2], AggUpdateType::UPDATE},
        {toSend[3], AggUpdateType::NEW}
    };
    PublishAndCheck(toSend, exp);
}

/*
 * If diffing has been disabled, then all updates should be broadcast,
 * regardless of whether a material change has happened to the object.
 *
 * The Msg is not required to provide a diffing util.
 */
TEST(TAggPuplisher,UpdatedItems_NoDiffing) {
    Msgs toSend {
            Msg{"Hello World!", 0},
            Msg{"A new message", 1},
            Msg{"Hello New World!", 0},
            Msg{"Hello New World!", 0},
            Msg{"Yet another message", 2}
    };

    ExpUpds exp {
            {toSend[0], AggUpdateType::NEW},
            {toSend[1], AggUpdateType::NEW},
            {toSend[2], AggUpdateType::UPDATE},
            {toSend[3], AggUpdateType::UPDATE},
            {toSend[4], AggUpdateType::NEW}
    };
    NonChecking_AggPipePub<IdMsg> pub;
    auto client = pub.NewClient(64);
    for (auto m: toSend) {
        auto mref = std::make_shared<IdMsg>(m);
        pub.Update(std::move(mref));
    }

    NonChecking_AggPipePub<IdMsg>::Upd upd;
    for (size_t i = 0; i < exp.size(); ++i) {
        ASSERT_TRUE(client->GetNextMessage(upd));
        ASSERT_EQ(upd.updateType, exp[i].updateType);
        ASSERT_EQ(upd.data->message, exp[i].m.message);
        ASSERT_EQ(upd.data->_id, exp[i].m._id);
    }
    ASSERT_FALSE(client->GetNextMessage(upd));
}

/*
 * If an existing item is updated, but has not changed, then
 * the update is *not* re-published
 *
 */
TEST(TAggPuplisher,NoDiffNoUpdate) {
    Msgs toSend {
            Msg{"Hello World!", 0},
            Msg{"A new message", 1},
            Msg{"Hello World!", 0},
            Msg{"Yet another message", 2},
            Msg{"Hello New World!", 0},
            Msg{"Yet another message", 3}
    };

    ExpUpds exp {
            {toSend[0], AggUpdateType::NEW},
            {toSend[1], AggUpdateType::NEW},
            // Update [2] is skipped, as there is no material change
            {toSend[3], AggUpdateType::NEW},
            {toSend[4], AggUpdateType::UPDATE},
            {toSend[5], AggUpdateType::NEW},
    };
    PublishAndCheck(toSend, exp);
}

TEST(TAggPuplisher,LateSubscriber) {
    Msgs toSend {
            Msg{"Hello World!", 0},
            Msg{"A new message", 1},
            Msg{"Hello World!", 0},
            Msg{"Yet another message", 2},
            Msg{"Hello New World!", 0},
            Msg{"Yet another message", 3}
    };

    ExpUpds exp {
            {toSend[0], AggUpdateType::NEW},
            {toSend[1], AggUpdateType::NEW},
            // Update [2] is skipped, as there is no material change
            {toSend[3], AggUpdateType::NEW},
            {toSend[4], AggUpdateType::UPDATE},
            {toSend[5], AggUpdateType::NEW},
    };

    MsgAgg pub;
    ClientRef client = pub.NewClient(1024);

    Publish(pub, toSend);
    CheckClient(client, exp);

    ExpUpds lateUpdates {
            {toSend[4], AggUpdateType::NEW},
            {toSend[1], AggUpdateType::NEW},
            {toSend[3], AggUpdateType::NEW},
            {toSend[5], AggUpdateType::NEW},
    };

    ClientRef lateClient = pub.NewClient(1024);
    CheckClient(lateClient, lateUpdates);
}


/*
 * The above test define the basic behaviourial properties
 * of the AggPub - but in reality we expect each sub to
 * be on their own thread.
 *
 * This first threaded test replicates the basic flow,
 * without trying to force any potential race conditions
 */
TEST(TAggPuplisherThreads,BasicFlow) {
    WorkerThread pubThread;
    WorkerThread clientThread;
    WorkerThread lateClientThread;
    pubThread.Start();
    clientThread.Start();
    lateClientThread.Start();

    Msgs toSend {
            Msg{"Hello World!", 0},
            Msg{"A new message", 1},
            Msg{"Hello World!", 0},
            Msg{"Yet another message", 2},
            Msg{"Hello New World!", 0},
            Msg{"Yet another message", 3}
    };

    Msgs toSend_late {
            Msg{"Hello New World!", 0},
            Msg{"A new message", 4},
            Msg{"Brand new 0", 0},
    };

    ExpUpds exp {
            {toSend[0], AggUpdateType::NEW},
            {toSend[1], AggUpdateType::NEW},
            // Update [2] is skipped, as there is no material change
            {toSend[3], AggUpdateType::NEW},
            {toSend[4], AggUpdateType::UPDATE},
            {toSend[5], AggUpdateType::NEW},
            // late [0] is skipped, as there is no material change
            {toSend_late[1], AggUpdateType::NEW},
            {toSend_late[2], AggUpdateType::UPDATE},
    };

    ExpUpds lateUpdates {
            {toSend[4], AggUpdateType::NEW},
            {toSend[1], AggUpdateType::NEW},
            {toSend[3], AggUpdateType::NEW},
            {toSend[5], AggUpdateType::NEW},
            // late [0] is skipped, as there is no material change
            {toSend_late[1], AggUpdateType::NEW},
            {toSend_late[2], AggUpdateType::UPDATE},
    };

    MsgAgg pub;
    ClientRef client, lateClient;

    clientThread.DoTask([&] () -> void {
        client = pub.NewClient(1024);
    });

    pubThread.DoTask([&] () -> void {
        Publish(pub, toSend);
    });

    lateClientThread.DoTask([&] () -> void {
        lateClient = pub.NewClient(1024);
    });

    pubThread.DoTask([&] () -> void {
        Publish(pub, toSend_late);
    });

    clientThread.DoTask([&] () -> void {
        CheckClient(client, exp);
    });

    lateClientThread.DoTask([&] () -> void {
        CheckClient(lateClient, lateUpdates);
    });
}

/*
 * We have to be careful when installing a late subscriber -
 * the client thread will attempt to traverse publisher's data
 * store to build an intiial image.
 *
 * *However*, if the publisher thread is attempting to publish
 * at the same time, it might be trying to delete / update from
 * that store.
 *
 * We're optimizing for the case where installing a client is rare
 * - as a result, a simple lock around the data store is acceptable.
 */
TEST(TAggPuplisherThreads,LastSubFastPubRace) {
    struct IdStamp {
        IdStamp (size_t id)
          : id(id)
        {
            stamp.SetNow();
        }

        size_t id    = 0;
        Time   stamp = Time().SetNow();
    };
    using IdPub = NonChecking_AggPipePub<IdStamp>;
    using IdPubRef = std::shared_ptr<IdPub>;
    using IdClientRef = IdPub::ClientRef;

    IdPubRef pub = std::make_shared<IdPub>();
    IdClientRef client;
    std::atomic_bool clientDone = false;

    WorkerThread pubThread, clientThread;

    auto loadData =  [&] () -> void {
        for (size_t i = 0; i < 10; ++i) {
            pub->Update(std::make_shared<IdStamp>(i));
        }
    };

    pubThread.Start();
    pubThread.DoTask([&] () -> void {
            loadData();
    });

    pubThread.PostTask([&] () -> void {
        while (!clientDone) {
            loadData();
        }
    });

    clientThread.Start();
    clientThread.DoTask([&] () -> void {
        client = pub->NewClient(102400);
        clientDone = true;
        // The first ten items, should be the first 10 ids: since
        // this is the initial data set.
        for (size_t i = 0; i < 10; ++i) {
            AggUpdate<IdStamp> upd;
            ASSERT_TRUE(client->GetNextMessage(upd));
            ASSERT_EQ(i, upd.data->id);
            ASSERT_EQ(AggUpdateType::NEW, upd.updateType);
        }
    });
}









