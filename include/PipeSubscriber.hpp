#include <PipePublisher.h>
#include <IPostable.h>


template<class Message>
IPipeConsumer<Message>::IPipeConsumer()
   : state(CONSUMING)
{

}

template<class Message>
void IPipeConsumer<Message>::Done() {
    /**
     * Failure is fine, it means abort was already initiated.
     */
    STATE fromState = CONSUMING;
    ChangeState(fromState,FINSIHED);
}

template<class Message>
void IPipeConsumer<Message>::Abort() {
    /**
     * Failure is fine, it means abort was already initiated.
     */
    STATE fromState = CONSUMING;
    ChangeState(fromState,ABORTING);
}

template<class Message>
bool IPipeConsumer<Message>::ChangeState(STATE& current, STATE to) {
    bool ok = false;

    const STATE from = current;
    while (current == from) {
        state.compare_exchange_strong(current,to);
    }

    if (current == to) {
        this->OnStateChange();
        ok = true;
    }

    return ok;
}

template <class Message>
PipeSubscriber<Message>::PipeSubscriber(
    PipePublisher<Message>* _parent,
    size_t maxSize)
        : onNotify(nullptr),
          targetToNotify(nullptr),
          onNewMessage(nullptr),
          batching(false),
          aborted(false),
          messages(maxSize)
{
    forwardMessage = false;
    notifyOnMessage = false;
}

template <class Message>
PipeSubscriber<Message>::~PipeSubscriber() {
    IPipeConsumer<Message>::Abort();
    if (batching) {
        PipeSubscriber<Message>::EndBatch();
    }
}

template<class Message>
void PipeSubscriber<Message>::NotifyNextMessage() {
    if (targetToNotify) {
        targetToNotify->PostTask(onNotify);
        targetToNotify = nullptr;
    } else {
        onNotify();
    }
    onNotify = nullptr;
    notifyOnMessage = false;
}

template <class Message>
void PipeSubscriber<Message>::PushMessage(const Message& msg) {
    /**
     * Remember, only one thread is allowed to publish...
     */
    if ( forwardMessage ) {
        if (batching) {
            // Already locked...
            onNewMessage(msg);
        } else {
            Lock notifyLock(onNotifyMutex);
            // No need to re-check post-lock since it is not possible to
            // unset the onNewMessage callback
            onNewMessage(msg);
        }
    } else {
        if (batching)
        {
            if ( !messages.push(msg) ) {
                throw PushToFullQueueException {msg};
            }
        }
        else
        {
            if (notifyOnMessage)
            {
                Lock notifyLock(onNotifyMutex);

                if ( !messages.push(msg) ) {
                    throw PushToFullQueueException {msg};
                }

                if (notifyOnMessage)
                {
                    NotifyNextMessage();
                }
            }
            else
            {
                if ( !messages.push(msg) ) {
                    throw PushToFullQueueException {msg};
                }

                if (notifyOnMessage)
                {
                    Lock notifyLock(onNotifyMutex);
                    if (notifyOnMessage)
                    {
                        NotifyNextMessage();
                    }
                }
            }
        }
    }
}

template<class Message>
void PipeSubscriber<Message>::OnStateChange() {
    typename IPipeConsumer<Message>::STATE state = this->State();
    if (state == IPipeConsumer<Message>::ABORTING)
    {
        /**
         * The subscriber is a single threaded CLIENT, which means that the
         * abort MUST have come from the CLIENT thread, so it is safe to
         * not-atomically acquire and update aborted;
         *
         * (Aborting from the publisher thread, or any other thread, is a bug)
         */
        aborted = true;
    }
}

template <class Message>
bool PipeSubscriber<Message>::GetNextMessage(Message& msg) {
    bool gotMsg = false;
    if ( messages.pop(msg) ) {
        gotMsg = true;
    }

    return gotMsg;
}

template <class Message>
void PipeSubscriber<Message>::OnNextMessage(const NextMessageCallback& f) {
    this->OnNextMessage(f,nullptr);
}

template<class Message>
void PipeSubscriber<Message>::StartBatch() {
    batching = true;
    onNotifyMutex.lock();

}

template<class Message>
void PipeSubscriber<Message>::EndBatch() {
    if (batching) {

        batching = false;

        if (notifyOnMessage) {
            if (targetToNotify) {
                targetToNotify->PostTask(onNotify);
                targetToNotify = nullptr;
            } else {
                onNotify();
            }
            onNotify = nullptr;
            notifyOnMessage = false;
        }

        onNotifyMutex.unlock();
    }
}

template <class Message>
void PipeSubscriber<Message>::OnNextMessage(
         const NextMessageCallback& f,
         IPostable* target)
{
    if (!this->aborted) {
        Lock notifyLock(onNotifyMutex);

        // We have the lock, so the publisher is now locked out.
        notifyOnMessage = true;

        if ( messages.read_available() > 0) {
            onNotify = nullptr;
            targetToNotify = nullptr;
            notifyOnMessage = false;
            if (target) {
                target->PostTask(f);
            } else {
                f();
            }
        } else {
            onNotify = f;
            targetToNotify = target;
        }
    }
}

template <class Message>
void PipeSubscriber<Message>::OnNewMessage(const NewMessasgCallback& f) {
    if (!this->aborted) {
        Lock notifyLock(onNotifyMutex);

        forwardMessage = true;
        onNewMessage = f;
        while (!this->aborted && messages.consume_one(f)) { }
    }
}
