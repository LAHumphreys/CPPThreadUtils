//
// Created by lukeh on 12/10/2019.
//
#ifndef THREADCOMMS_AGGPIPEPUB_HPP
#define THREADCOMMS_AGGPIPEPUB_HPP
#include <AggPipePub.h>

template<class Message,
         AggPipePub_Config::AggPipePubUpdateMode updateMode>
typename AggPipePub<Message, updateMode>::ClientRef
    AggPipePub<Message,updateMode>::NewClient(const size_t& max)
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

template<class Message, AggPipePub_Config::AggPipePubUpdateMode updateMode>
typename AggPipePub<Message, updateMode>::ClientRef
    AggPipePub<Message, updateMode>::NewClient(const size_t& max,
                                               const AggPipePub::Filter& filter)
{
    std::vector<Upd> initialData;
    dataStore.WithData([&] (auto& data) -> void {
        initialData.reserve(data.size());
        for ( const auto& pair: data) {
            if (filter(*pair.second)) {
                initialData.push_back(ManufactureNewUpdate(pair.second));
            }
        }
    });

    return pub_.template NewClient<Client>(max, std::move(initialData));
}

template<class Message,
        AggPipePub_Config::AggPipePubUpdateMode updateMode>
void AggPipePub<Message,updateMode>::Update(MsgRef m) {
    for (Upd& upd: ManufactureUpdate(std::move(m))) {
        pub_.Publish(std::move(upd));
    }
}

template<class Message, AggPipePub_Config::AggPipePubUpdateMode updateMode>
void AggPipePub<Message, updateMode>::ResetMessages(const std::vector<MsgRef>& msgs) {
    typename LockedData::DataType newMap;
    for (const MsgRef& msg: msgs) {
        newMap[GetId(*msg)] = msg;
    }

    dataStore.WithData([&] (auto& existingData) -> void {
        auto existingIt = existingData.begin();
        auto newIt = newMap.begin();

        while (existingIt != existingData.end() && newIt != newMap.end()) {
            MsgRef existing = existingIt->second;
            MsgRef newEl = newIt->second;

            const auto eid = GetId(*existing);
            const auto nid = GetId(*newEl);

            if (eid == nid) {
                if (IsUpdated(*existing, *newEl)) {
                    if (replace_mods) {
                        pub_.Publish({existing, AggUpdateType::DELETE});
                        pub_.Publish({newEl, AggUpdateType::NEW});
                    } else {
                        pub_.Publish({newEl, AggUpdateType::UPDATE});
                    }
                }
                ++existingIt;
                ++newIt;
            } else if (eid < nid) {
                pub_.Publish({existing, AggUpdateType::DELETE});
                ++existingIt;
            } else if (eid > nid) {
                pub_.Publish({newEl, AggUpdateType::NEW});
                ++newIt;
            }
        }

        while (existingIt != existingData.end()) {
            pub_.Publish({existingIt->second, AggUpdateType::DELETE});
            ++existingIt;
        }

        while (newIt != newMap.end()) {
            pub_.Publish({newIt->second, AggUpdateType::NEW});
            ++newIt;
        }

        existingData = std::move(newMap);
    });
}


template<class Message,
        AggPipePub_Config::AggPipePubUpdateMode updateMode>
std::vector<typename AggPipePub<Message, updateMode>::Upd>
    AggPipePub<Message,updateMode>::ManufactureUpdate(AggPipePub::MsgRef m)
{
    AggUpdateType updType = AggUpdateType::NEW;
    const auto id = GetId(*m);
    MsgRef oldItem = nullptr;

    dataStore.WithData([&] (typename LockedData::DataType& data) -> void {
        auto it = data.find(id);
        if (it == data.end()) {
            data[id] = m;
        } else if (IsUpdated(*it->second, *m)) {
            if (replace_mods) {
                oldItem = it->second;
            } else {
                updType = AggUpdateType::UPDATE;
            }
            data.erase(it);
            data[id] = m;
        } else {
            updType = AggUpdateType::NONE;
        }
    });

    if (oldItem.get() != nullptr) {
        return { Upd{std::move(oldItem), AggUpdateType::DELETE},
                 Upd{std::move(m), updType} };
    } else if (updType != AggUpdateType::NONE) {
        return { Upd{std::move(m), updType} };
    } else {
        return {};
    }
}

template<class Message,
        AggPipePub_Config::AggPipePubUpdateMode updateMode>
typename AggPipePub<Message,updateMode>::Upd
    AggPipePub<Message,updateMode>::ManufactureNewUpdate(AggPipePub::MsgRef msg)
{
    return AggPipePub::Upd{msg, AggUpdateType::NEW};
}

template<class Message,
        AggPipePub_Config::AggPipePubUpdateMode updateMode>
void AggPipePub<Message,updateMode>::LockedData::WithData(
        const std::function<void(DataType &data)>& task)
{
    std::unique_lock<std::mutex> lock(dataMutex);
    task(data);
}


#endif //THREADCOMMS_AGGPIPEPUB_HPP
