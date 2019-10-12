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
    explicit Msg (std::string m): message(std::move(m)) {}

    std::string   message;
};
using Msgs = std::vector<Msg>;

using MsgAgg = AggPipePub<Msg>;
using Upds = std::vector<MsgAgg::Upd>;

struct ExpUpd {
    Msg m;
};
using ExpUpds = std::vector<ExpUpd>;

using ClientRef = std::shared_ptr<PipeSubscriber<MsgAgg::Upd>>;

void MessagesMatch(ExpUpds& exp, Upds& got)
{
    ASSERT_EQ(exp.size(), got.size());

    for (size_t i = 0; i < exp.size(); ++i) {
        Msg& msg = exp[i].m;
        Msg& recvd = *got[i].data;

        ASSERT_EQ(msg.message, recvd.message);
    }
}

void Publish(MsgAgg& pub, const Msg& m) {
    auto cpy = std::make_shared<Msg>(m);
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

TEST(TAggPuplisher,NewItems) {
    MsgAgg pub;
    ClientRef client = pub.NewClient(1024);

    Msgs sent {
        Msg{"Hello World!"}
    };
    Publish(pub, sent);

    ExpUpds exp {
        {Msg{"Hello World!"}}
    };

    Upds got;
    ASSERT_NO_FATAL_FAILURE(GetMessages(1, client, got));
    ASSERT_NO_FATAL_FAILURE(MessagesMatch(exp, got));
}












