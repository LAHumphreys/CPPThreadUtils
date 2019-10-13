//
// Created by lukeh on 12/10/2019.
//
#ifndef THREADCOMMS_AGGPIPEPUB_H
#define THREADCOMMS_AGGPIPEPUB_H

#include <PipePublisher.h>
#include <AggUpdate.h>
#include <map>

template <class Message>
class AggPipePub {
public:
    using MsgRef = std::shared_ptr<const Message>;
    using IdType = typename Message::AggIdType;
    constexpr const IdType& GetId(const Message& m) {
        return m.GetAggId();
    }
    constexpr bool IsUpdated(const Message& orig, const Message& n) {
        return !orig.IsAggEqual(n);
    }
    void Update(MsgRef msg);

    using Upd = AggUpdate<Message>;
    using Client = PipeSubscriber<Upd>;
    using ClientRef = std::shared_ptr<Client>;
    /**
     * Create a new subscription to the update publisher.
     */
    std::shared_ptr<PipeSubscriber<Upd>> NewClient(const size_t& maxQueueSize);

private:
    std::map<IdType, MsgRef> data;
    Upd ManufactureUpdate(MsgRef msg);
    Upd ManufactureNewUpdate(MsgRef msg);
    PipePublisher<Upd> pub_;
};

#include <AggPipePub.hpp>
#endif //THREADCOMMS_AGGPIPEPUB_H
