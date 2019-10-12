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
void AggPipePub<Message>::Update(std::shared_ptr<Message> m) {
    pub_.Publish(ManufactureUpdate(std::move(m)));
}

template<class Message>
typename AggPipePub<Message>::Upd AggPipePub<Message>::ManufactureUpdate(AggPipePub::MsgRef m) {
    return Upd{std::move(m)};
}

#endif //THREADCOMMS_AGGPIPEPUB_HPP
