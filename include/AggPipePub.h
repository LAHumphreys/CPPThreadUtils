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
    /**
     * Create a new subscription to the update publisher.
     */
    template <class Client = PipeSubscriber<Upd>, class... Args>
    std::shared_ptr<Client> NewClient(Args... args);

private:
    std::map<IdType, MsgRef> data;
    Upd ManufactureUpdate(MsgRef msg);
    PipePublisher<Upd> pub_;
};

#include <AggPipePub.hpp>
#endif //THREADCOMMS_AGGPIPEPUB_H
