/*
 * Subscribe to updates from the Pipe Publisher
 *
 *  Created on: 29 Oct 2015
 *      Author: lhumphreys
 */

#ifndef DEV_TOOLS_CPP_LIBRARIES_LIB_THEAD_COMMS_PIPE_SUBSCRUBER_H__
#define DEV_TOOLS_CPP_LIBRARIES_LIB_THEAD_COMMS_PIPE_SUBSCRUBER_H__

#include <boost/lockfree/spsc_queue.hpp>
#include <atomic>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <bitset>
#include <thread>
#include <vector>

template <class Message>
class PipePublisher;
class IPostable;

template <class Message>
class IPipeConsumer {
public:
    IPipeConsumer();


    virtual ~IPipeConsumer() {}

    /**
     * Notify publishers that no further updates should be posted.
     *
     * NOTE: This operation is not synchronised, and it is possible due to
     *       re-ordering, that updates or published after the completion of
     *       abort. Application that care should check the return value of
     *       State() when handling an update.
     */
    void Abort();
protected:
    /**
     * Callback triggered by a new publication.
     */
    virtual void PushMessage(const Message& ) = 0;

    enum STATE {
        CONSUMING, // Consuming updates from the publisher, as normal
        FINSIHED,  // All updates have been consumed
        ABORTING,  // Abort requested, waiting for the publisher(s) to
                   // acknowledge, until ABORTED is declared.
        ABORTED    // Abort successful, no more updates will be published.
    };

    /**
     * Callback to indicate the state of the consumer has changed.
     *
     * Implementations may safely choose to ignore this callback, it is provided
     * to allow for lock-free optimizations.
     *
     * NOTE: This will be called from the thread which triggered the state
     *       change:
     *
     *          CONSUMING -> ABORTING: The aborting thread.
     *          ABORTING -> ABORTED:   The publisher's thread.
     */
    virtual void OnStateChange() { };

    /**
     * Thread safe check to observe the current state of the consumer.
     */
    STATE State() {
        return state;
    }

    /**
     * Callback from the publisher indicating that it has started a new batch.
     *
     * EndBatch will be called when it has finished.  Implementation of this
     * interface is voluntary, clients may choose to continue to process updates
     * as they are presented by the publisher, or wait until EndBatch if they
     * can afford to wait, and this more efficient.
     *
     * NOTE: This will be triggered on the publisher's thread
     */
    virtual void StartBatch() {};

    /**
     * The batch has finished.
     *
     * NOTE: This will be triggered on the publisher's thread
     */
    virtual void EndBatch() {}
private:
    /**
     *
     *
     * @param current  The state to change from, will be populated with the current
     *                 state in the event of a failure.
     *
     * @param to    The state to transition to.
     *
     * @returns true If the state was changed, false otherwise.
     */
    bool ChangeState(STATE& current, STATE to);

    /**
     * Called by the publisher to indicate that no more
     */
    void Done();

    std::atomic<STATE> state;
    friend class PipePublisher<Message>;
    std::vector<PipePublisher<Message>*> publishers;
};


/**
 * Consumes data down a single-producer / single-consumer pipe.
 *
 * The pipe is lockless, except for the configuration and dispatch of the
 * onMessage notification. (Choosing not to use trigger results in a lockless
 * pipe.
 */
template <class Message>
class PipeSubscriber: public IPipeConsumer<Message> {
public:
    typedef PipeSubscriber<Message> Type;

    virtual ~PipeSubscriber();

    /** 
     * Pop the next message off the queue and populate
     * msg with the result.
     *
     * If there is no message to pop, msg is left unchanged
     *
     * @param msg   The message to populate.
     *
     * @returns true if msg was populated, false otherwise.
     */ 
    bool GetNextMessage(Message& msg);

    /**
     * Trigger a callback function ON **ETIHER** the publisher thread OR the
     * current thread when there is at least one unread message. This can be
     * used to trigger a post to the subscriber thread if desired, e.g using
     * boost::asio::io_service::post.
     *
     * This callback will be triggered exactly once. 
     *
     * NOITE: No unread message notifications will be triggered whilst message
     *        forwarding has been enabled.
     *
     * NOTE: This callback may be triggered immediately (before returning) on
     *        the current thread if there is already unread data on the queue.
     */
    typedef std::function<void(void)> NextMessageCallback;
    void OnNextMessage(const NextMessageCallback& f);

    /**
     * Variant of the OnNextMessage callback which posts the task to another 
     * event loop.
     *
     *  @param  f          The callback to trigger
     *  @param  target     The object to post the task to.
     */
     void OnNextMessage(const NextMessageCallback& f, IPostable* target);

    /**
     * Trigger a callback function for each new message received by the
     * subsciber. The function will be called from the **PUBLISHER THREAD**.
     *
     * If there are any unread message currently in the queue, these will be 
     * triggered on the **CURRENT THREAD** before this function returns. 
     *
     * NOITE: Setting up a message forward via OnNewMessage implicitly
     *        suprresses the OnNextMessage callback since there will never be
     *        any unread data.
     *
     * NOTE: To preserve ordering this function will lock out the publisher thread
     *       until call-backss for all existing messages haeve been completed.
     */
    typedef std::function<void(const Message&)> NewMessasgCallback;
    void OnNewMessage(const NewMessasgCallback&  f);

protected:
    friend class PipePublisher<Message>;
    /*********************************
     *   Interface for Publisher
     *********************************/
    /**
     * Create a new consumer:
     *
     * @param maxSize  Maximum unread messages before an exception is
     *                 triggered on the producer.
     */
    PipeSubscriber(
        PipePublisher<Message>* parent,
        size_t maxSize);

     struct PushToFullQueueException {
         Message msg;
     };

     /**
      * Push a new message onto the queue. 
      * 
      * If the queue is full an instance of PushToFullQueueException is thrown.
      *
      * If there is an active OnNextMessage callback this will be triggered
      * before the function returns.
      *
      * @param msg  The message to add to the queue.
      */ 
     virtual void PushMessage(const Message& msg);

private:
    /***********************************
     *     Interface Implementation
     ***********************************/
     virtual void OnStateChange() final;
     virtual void StartBatch() final;
     virtual void EndBatch() final;
    void NotifyNextMessage();

    /***********************************
     *          Synchronisation
     ***********************************/
    typedef std::unique_lock<std::mutex> Lock;
    std::mutex                           onNotifyMutex;

    /***********************************
     * Unread data Notification
     ***********************************/
    NextMessageCallback  onNotify;
    IPostable*           targetToNotify;
    size_t               batchSize;
    std::atomic<bool>    notifyOnMessage;


    /***********************************
     * Forward Messages
     ***********************************/
    NewMessasgCallback   onNewMessage;
    std::atomic<bool>    forwardMessage;

    /*********************************
     *           Data
     *********************************/
    bool                 batching;
    bool                 aborted;

    boost::lockfree::spsc_queue<Message>  messages;
};


#include "PipeSubscriber.hpp"

#endif 
