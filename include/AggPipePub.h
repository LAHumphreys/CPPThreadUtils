//
// Created by lukeh on 12/10/2019.
//
#ifndef THREADCOMMS_AGGPIPEPUB_H
#define THREADCOMMS_AGGPIPEPUB_H

#include <PipePublisher.h>
#include <AggUpdate.h>
#include <map>

#include <AggPipePub_MessageAdaption.h>


// Aggregated Data Update Publisher
//
// This is a special variant of the PipePublisher, designed to throttle
// update notices, protecting client threads that have perform expensive
// operations on each update.
//
// The primary use case is model a remote data-set, over a potentially
// flakey connection. The interface wrapping an AggPipePub may recover
// and only notify the
//
//  Remote                  Remote Interface            Client
//                        (Adapting AggPipePub)
//  SYSTEM START
//     |              <--Subscribe-- |                          |
//     | -- Initial Data {0,1,2} --> |                          |
//     |                             |  --    NEW {0} -->       |
//     |                             |  --    NEW {1} -->       |
//     |                             |  --    NEW {2} -->       |
//     | -- Update {1} -->           |                          |
//     |                             |  -- UPDATE {1} -->       |
//     | -- Update {2} -->           |                          |
//                                  /* {2} unchanged - No update  */
//     | -- Update {3} -->           |                          |
//     |                             |  --    NEW {3} -->       |
//  Remote disconnect - wait for reconnect
//  ...
//  Remote reconnected - determine missed updates
//     |              <--Subscribe-- |                          |
//     |-- Initial Data {0,1,3,4}--> |                          |
//                                  /* {0} unchanged - No update  */
//     |                             |  -- UPDATE {1} -->       |
//     |                             |  -- DELETE {2} -->       |
//                                  /* {3} unchanged - No update  */
//     |                             |  --    NEW {4} -->       |
//
// NOTE: Unlike the underlying PipePublisher, the AggPub publishes a
//       shared_ptr to the Message, rather than performing a full copy
//       to each thread.
template <class Message>
class AggPipePub {
public:
// PUBLIC TYPE DEFINITIONS

    // Shared Reference to the Message will be published to each subscriber
    // thread.
    using MsgRef = std::shared_ptr<const Message>;

    // The AggUpdate wraps a MsgRef with an update type, so clients know
    // whether the message is a NEW insert for this id, or an UPDATE to
    // an already published
    using Upd = AggUpdate<Message>;

    // Clients subscribe using a standard PipeSubscriber object.
    using Client = PipeSubscriber<Upd>;
    using ClientRef = std::shared_ptr<Client>;

// PUBLIC INTERFACE
    // Nothing special required for construction.
    AggPipePub() = default;

    // Push a new Message into the data set. Existing clients will
    // get a NEW if they this is the first time the id has been seen,
    // and an UPDATE otherwise.
    //
    // Clients who subscribe after the message has been processed will
    // see the update as a NEW when they get their initial data (the
    // latest message per id)
    //
    // NOTE: Due to the use of a PipePublisher, only *ONE THREAD* is allowed
    //       to publish (ever). Although not strictly required, convention is
    //       that it is the same thread that constructed the AggPipePub.
    void Update(MsgRef msg);

    // Register a new client.
    //
    // This is a standard PipeSubscriber object, receiving update publication
    // when data changes (see comment against the Update method).
    //
    // The client will be pre-loaded with the existing data set - a NEW update
    // for each unique idea that has already been seen by the publisher.
    //
    // NOTE: The publisher is locked during construction, and no further updates
    //       may be posted until the client has been successfully initialised.
    //
    // NOTE: See note on threading restrctions documented on PipeSubscriber.
    std::shared_ptr<PipeSubscriber<Upd>> NewClient(const size_t& maxQueueSize);

private:
    using Adapter = AggPipePub_Adaption::MessageAdapter<Message>;
    using IdType = typename Adapter::GetAggIdValueType;

    constexpr const IdType GetId(const Message& m) {
        return Adapter::Get(m);
    }

    constexpr bool IsUpdated(const Message& orig, const Message& n) {
        return !Adapter::IsEqual(orig, n);
    }

    class LockedData {
    public:
        using DataType = std::map<IdType, MsgRef>;
        void WithData(const std::function<void (DataType& data)>&);
    private:
        std::mutex   dataMutex;
        DataType data;
    };
    LockedData dataStore;
    Upd ManufactureUpdate(MsgRef msg);
    Upd ManufactureNewUpdate(MsgRef msg);
    PipePublisher<Upd> pub_;
};

#include <AggPipePub.hpp>
#endif //THREADCOMMS_AGGPIPEPUB_H
