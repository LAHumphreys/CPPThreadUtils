//
// Created by lukeh on 12/10/2019.
//
#include <PipePublisher.h>
#include <gtest/gtest.h>
#include <AggPipePub.h>
#include <util_time.h>

using namespace std;
using namespace nstimestamp;

struct Msg {
    Msg (std::string m, size_t i)
        : message(std::move(m))
        , id(i) {}

    std::string   message;
    size_t        id;
};
struct WrappedMsg: public Msg {
    WrappedMsg (std::string m, size_t i)
        : Msg(std::move(m), i) {}

    using AggIdType = size_t;
    [[nodiscard]] constexpr const AggIdType& GetAggId() const { return id;}
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
        ASSERT_EQ(msg.id, recvd.id);

        ASSERT_EQ(exp[i].updateType, got[i].updateType);
    }
}

void Publish(MsgAgg& pub, const Msg& m) {
    auto cpy = std::make_shared<WrappedMsg>(m.message, m.id);
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

void PublishAndCheck (Msgs& toSend, ExpUpds& exp) {
    MsgAgg pub;
    ClientRef client = pub.NewClient(1024);

    Publish(pub, toSend);

    Upds got;
    ASSERT_NO_FATAL_FAILURE(GetMessages(exp.size(), client, got));
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(exp, got));

}

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












