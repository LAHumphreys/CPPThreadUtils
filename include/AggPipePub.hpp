//
// Created by lukeh on 12/10/2019.
//
#ifndef THREADCOMMS_AGGPIPEPUB_HPP
#define THREADCOMMS_AGGPIPEPUB_HPP
#include <AggPipePub.h>

template<class Message>
template<class Client, class... Args>
std::shared_ptr<Client> AggPipePub<Message>::NewClient(Args... args) {
    return pub_.template NewClient<Client>(args...);
}

template<class Message>
void AggPipePub<Message>::Update(MsgRef m) {
    auto upd = ManufactureUpdate(std::move(m));
    if (upd.updateType != AggUpdateType::NONE) {
        pub_.Publish(std::move(upd));
    }
}

template<class Message>
typename AggPipePub<Message>::Upd AggPipePub<Message>::ManufactureUpdate(AggPipePub::MsgRef m) {
    AggUpdateType updType = AggUpdateType::NEW;

    auto id = GetId(*m);
    auto it = data.find(id);
    if (it == data.end()) {
        data[id] =  m;
    } else if (IsUpdated(*it->second, *m)) {
        data.erase(it);
        updType = AggUpdateType::UPDATE;
    } else {
        updType = AggUpdateType::NONE;
    }

    return Upd{std::move(m), updType};
}

#endif //THREADCOMMS_AGGPIPEPUB_HPP
