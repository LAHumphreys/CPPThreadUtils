//
// Created by lukeh on 12/10/2019.
//
#ifndef THREADCOMMS_AGGPIPEPUB_HPP
#define THREADCOMMS_AGGPIPEPUB_HPP
#include <AggPipePub.h>

template<class Message>
typename AggPipePub<Message>::ClientRef
    AggPipePub<Message>::NewClient(const size_t& max)
{
    std::vector<Upd> initialData;
    dataStore.WithData([&] (auto& data) -> void {
        initialData.reserve(data.size());
        for ( const auto& pair: data) {
            initialData.push_back(ManufactureNewUpdate(pair.second));
        }
    });

    return pub_.template NewClient<Client>(max, std::move(initialData));
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

    dataStore.WithData([&] (auto& data) -> void {
        auto it = data.find(id);
        if (it == data.end()) {
            data[id] = m;
        } else if (IsUpdated(*it->second, *m)) {
            data.erase(it);
            updType = AggUpdateType::UPDATE;
            data[id] = m;
        } else {
            updType = AggUpdateType::NONE;
        }
    });

    return Upd{std::move(m), updType};
}

template<class Message>
typename AggPipePub<Message>::Upd
    AggPipePub<Message>::ManufactureNewUpdate(AggPipePub::MsgRef msg)
{
    return AggPipePub::Upd{msg, AggUpdateType::NEW};
}

template<class Message>
void AggPipePub<Message>::LockedData::WithData(
        const std::function<void(DataType &data)>& task)
{
    std::unique_lock<std::mutex> lock(dataMutex);
    task(data);
}


#endif //THREADCOMMS_AGGPIPEPUB_HPP
