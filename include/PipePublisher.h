/*
 * Publisher updates to multiple consumers
 *
 *  Created on: 22nd December 2015
 *      Author: lhumphreys
 */

#ifndef DEV_TOOLS_CPP_LIBRARIES_LIB_THEAD_COMMS_PIPE_PUBLISHER_H__
#define DEV_TOOLS_CPP_LIBRARIES_LIB_THEAD_COMMS_PIPE_PUBLISHER_H__

#include <PipeSubscriber.h>
#include <mutex>
#include <map>
#include <thread>
#include <vector>

#include <boost/lockfree/queue.hpp>

/**
 * Publishes copies of data to multiple consumers. The copy construction is
 * done on the publicatin thread, as in any locking, whilst the consumer is
 * lockless. Therefore the pipe is suitable for offloading work to a worker
 * thread, for consumption by one or more client threads.
 *
 * The client may either poll the consumer object periodically, or register
 * for an unread message notificaiton. To reduce overhead, the notification
 * will only be triggered once, at which point the client should drain the
 * queue, and re-configure the notification. See PipeSubscriber
 *
 */
template <class Message>
class PipePublisher { 
public:
    typedef PipePublisher<Message> Type;

    PipePublisher();

    virtual ~PipePublisher();

    /**
     * Create a new subscription to the publisher.
     *
     * @param  maxSize  Maximum number of unread messages
     */
    template <class Client = PipeSubscriber<Message>, class... Args>
    std::shared_ptr<Client> NewClient(Args... args);

    void InstallClient(std::shared_ptr<IPipeConsumer<Message>> client);

    /**
     * Publish a new message to all clients.
     */
    void Publish(const Message& msg);

    /**
     * Start a new batch of messages.
     *
     * If there is already a batch in progress, this is ended, and a new batch
     * started.
     *
     * EndBatch MUST be called on completion of the batch.
     *
     * Dispatching messages as part of a batch is more efficient, but it leaves
     * the publisher in a locked state, meaning:
     *    - New clients may not be added or removed.
     *    - Dead clients will be kept alive until the batch is complete
     *    - Some clients may not process any updates until EndBatch is called.
     *      (The client chooses whether or not to respect we are publishing as
     *      part of a batch..)
     */
    void StartBatch();

    /**
     * End the current batch.
     *
     * If there is not currently a batch being processed, this call has no
     * effect.
     */
    void EndBatch();

    /**
     * Notify all clients that no more updates will be published.
     */
    void Done();

    size_t NumClients();
private:

    class Lock {
    public:
        Lock(Type& parent);

        ~Lock();
    private:
        Type& parent;
    };

    class Batch {
    public:
        Batch(Type& parent);

        ~Batch();
    private:
        Type&      parent;
        Type::Lock lock;
    };

    friend class Type::Lock;
    friend class Type::Batch;
    friend class PipeSubscriber<Message>;
    friend class IPipeConsumer<Message>;

    typedef std::shared_ptr<IPipeConsumer<Message>> ClientRef;
    typedef std::weak_ptr<IPipeConsumer<Message>> pClient;

    typedef std::vector<ClientRef> ClientList;

    void RemoveClient(typename ClientList::iterator client);

    // Lock the subscription mutex
    void DoLock();
    bool TryLock();

    // Unlock the subscription mutex
    void Unlock();

    // Check if the current thread has a lock on the publisher.
    bool HaveLock();
    /*********************************
     *           Data
     *********************************/
    std::mutex                         subscriptionMutex;
    std::thread::id                    subscriptionLockOwner;
    ClientList                         clients;
    std::atomic<size_t>                numClients;
    std::unique_ptr<Batch>             currentBatch;
};


#include "PipePublisher.hpp"
#endif 
