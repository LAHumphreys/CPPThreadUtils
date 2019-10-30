#include <PipePublisher.h>
#include <IPostable.h>
#include <future>


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

template<class Message, class NextMessageCallback, class NewMessageCallback>
PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PipeSubscriber(
    PipePublisher<Message>* _parent,
    size_t maxSize,
    std::vector<Message> initialData)
        : notifyState(DISABLED),
          onNotify(nullptr),
          targetToNotify(nullptr),
          onNewMessage(nullptr),
          batching(false),
          aborted(false),
          messages(maxSize)
{
    forwardMessage = false;
    for (const auto& m: initialData ) {
        PushMessage(m);
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::~PipeSubscriber() {
    IPipeConsumer<Message>::Abort();
    if (batching) {
        PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::EndBatch();
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PushComplete()
{
    bool handled = false;
    enum ACTION {
        NOTHING,
        PUBLISH
    };

    NOTIFY_STATE currentState = notifyState;
    ACTION action = NOTHING;
    while (handled == false) {
        action = NOTHING;
        NOTIFY_STATE newState = currentState;
        switch (currentState) {
        case DISABLED:
            // Fine - nothing more to do.
            handled = true;
            break;

        case NOTHING_TO_NOTIFY:
            // Notification is primed - fire it.
            newState = PUBLISHING;
            action = PUBLISH;
            break;

        case CONFIGURING:
            // Notification configuration is in process -
            // we'll need to trigger it when done
            newState = PULL_REQUIRED;
            break;

        case PULL_REQUIRED:
            // Client will pull when its finished reconfiguring
            handled = true;
            break;

        case CLIENT_WILL_RECONFIGURE:
            // Client will pull when its finished reconfiguring
            handled = true;
            break;

        case PULLING:
            // Notification already being triggered, nothing else to do.
            handled = true;
            break;

        case RECONFIGURE_REQUIRED:
            // The client is currently busy, but will reconfigure (and fire) a notification when ready
            handled = true;
            break;

        case SUB_WAKEUP_REQUIRED:
            // This can't happen - as per the state model.
            // SUB_WAKEUP_REQUIRED is a state which blocks out both threads -
            // This can't have happened if we were publishing.
            throw ThreadingViolation { "Non-publisher thread has published!"};

        case PUBLISHING:
            // PUBLISHING is a Publisher controlled state.
            // We can't have been pushing *and* publishing.
            throw ThreadingViolation { "Non-publisher thread has published!"};

        case PUB_WILL_RECONFIGURE:
            // PUB_WILL_RECONFIGURE results from a configure request in the PUBLISHING
            // state. It is publisher controlled, so we can't have been both pushing and
            // publishing.
            throw ThreadingViolation { "Non-publisher thread has published!"};
        case PUB_RECONFIGURING:
            // We can't have been both reconfigurin and publishing.
            throw ThreadingViolation { "Non-publisher thread has published!"};

        case PREPARING_PUB_RECONFIGURE:
            // This state represents a publisher thread that was already publishing.
            // We can't have published!
            throw ThreadingViolation { "Non-publisher thread has published!"};
        }

        // State change triggered...
        if (!handled) {
            handled = notifyState.compare_exchange_strong(
                          currentState,
                          newState);
        }
    }

    switch (action) {
    case PUBLISH:
        PublishNextMessage();
        break;
    case NOTHING:
        // nothing to do...
        break;
    }

}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PublishComplete()
{
    bool handled = false;
    enum ACTION {
        NOTHING,
        RECONFIGURE
    };

    NOTIFY_STATE currentState = notifyState;
    ACTION action = NOTHING;
    while (handled == false) {
        action = NOTHING;
        NOTIFY_STATE newState = currentState;
        switch (currentState) {
            case PUBLISHING:
                // Expected case - move back to the rest state
                newState = DISABLED;
                break;

            case PREPARING_PUB_RECONFIGURE:
                // A pending hook is being prepared by the client. Once it
                // is done it will do a state check. Inform it that we're
                // done and the client can make the hook live.
                newState = CLIENT_WILL_RECONFIGURE;
                break;

            case PUB_WILL_RECONFIGURE:
                // Client installed a pending hook whilst we were publishing
                // Prepare to
                action = RECONFIGURE;
                newState = PUB_RECONFIGURING;
                break;

            case PUB_RECONFIGURING:
                // We are supposed to be busy reconfiguring, we can't
                // possible have published
                throw ThreadingViolation { "Publish occurred whilst publisher was reconfiguring!"};

            case RECONFIGURE_REQUIRED:
                // Client controlled state - we would not have been
                // allowed to publish
                throw ThreadingViolation { "Publish occurred whilst nothing to push!"};

            case SUB_WAKEUP_REQUIRED:
                // Sub wakeup required is a pub controlled state
                // which occurrs during a publisher side reconfigure.
                // We can't have been publishing and reconfiguring.
                throw ThreadingViolation { "Non publisher thread has published!"};

            case DISABLED:
                // This is simply ludicrous
                throw ThreadingViolation { "Disabled client published!"};

            case PULLING:
                // PULLING is a client controlled state - we cannot have
                // started a publish if the client was already pulling!
                throw ThreadingViolation { "Publish occured whilst the client was pulling!"};

            case PULL_REQUIRED:
                // PULL_REQUIRED is a client controlled state representing an
                // in progress configure. We can't have published whilst configuring.
                throw ThreadingViolation { "Publish occured whilst the client was configuring!"};

            case CONFIGURING:
                // We can't have published whilst configuring
                throw ThreadingViolation { "Publish occured whilst the client was configuring!"};

            case NOTHING_TO_NOTIFY:
                // If there was nothing to notify, we shouldn't have published.
                throw ThreadingViolation { "Publish occurred whilst nothing to push!"};

            case CLIENT_WILL_RECONFIGURE:
                // The publisher had already handed over to us - it should not have started to publish again.
                throw ThreadingViolation { "Publish occurred whilst was responsible for reconfiguring"};

        }

        // State change triggered...
        if (!handled) {
            handled = notifyState.compare_exchange_strong(
                    currentState,
                    newState);
        }
    }

    switch (action) {
        case RECONFIGURE:
            ConfigurePendingNotify(PUBLISHER);
            break;
        case NOTHING:
            // nothing to do...
            break;
    }

}

template<class Message, class NextMessageCallback, class NewMessageCallback>
typename PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::CONFIGURE_ACTION
PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::NotifyRequested()
{
    bool handled = false;
    enum ACTION {
        NOTHING,
        CONFIGURE,
        SETUP_PENDING,
        WAIT
    };

    NOTIFY_STATE currentState = notifyState;
    ACTION action = NOTHING;
    while (handled == false) {
        action = NOTHING;
        NOTIFY_STATE newState = currentState;
        switch (currentState) {
            case DISABLED:
                // Expected case - start the configuration...
                newState = CONFIGURING;
                action = CONFIGURE;
                break;

            case NOTHING_TO_NOTIFY:
                // Replace the old notification with a new one...
                newState = CONFIGURING;
                action = CONFIGURE;
                break;

            case PUBLISHING:
                // The publisher is already busy publishing, we'll need to wait
                newState = PREPARING_PUB_RECONFIGURE;
                action = SETUP_PENDING;
                break;

            case PUB_WILL_RECONFIGURE:
                // OK fine, but if you're continuously reconfiguring under
                // a publish it suggests poorly performing code...
                newState = PREPARING_PUB_RECONFIGURE;
                action = SETUP_PENDING;
                break;

            case PUB_RECONFIGURING:
                // The publisher is busy reconfiguring (therefore we must be the client)
                // We're going to need to wait until its done
                newState = SUB_WAKEUP_REQUIRED;
                action = WAIT;
                break;

            case CONFIGURING:
                // The publisher is done - and has released control back to us;
                action = CONFIGURE;
                handled = true;
                break;

            case SUB_WAKEUP_REQUIRED:
                // Sub wakeup required represents both threads inside the
                // framework (not publishing / pulling).
                // Therefore this can't happen unless a third thread is involved.
                throw ThreadingViolation { "Reconfigure requires whilst client was sleeping"};

            case PREPARING_PUB_RECONFIGURE:
                // TODO: Add some tests for reconfiguring from publisher whilst client is etc...
                // (Potentially we can make this work with a wakeup flag?)
                throw ThreadingViolation { "Reconfigure attempt whilst preparing the pending reconfigure"};

            case PULL_REQUIRED:
                throw ThreadingViolation { "Reconfigure requested whilst the client was already reconfiguring"};

            case CLIENT_WILL_RECONFIGURE:
                throw ThreadingViolation { "Reconfigure requested whilst the client was already reconfiguring"};

            case PULLING:
                // Client is busy pulling. setup the pending...
                action = SETUP_PENDING;
                newState = RECONFIGURE_REQUIRED;
                break;

            case RECONFIGURE_REQUIRED:
                // Fine - but if you're continually reconfiugring it suggest poorly performing code...
                action = SETUP_PENDING;
                handled = true;
                break;
        }

        // State change triggered...
        if (!handled) {
            handled = notifyState.compare_exchange_strong(
                    currentState,
                    newState);
        }

        if (handled && action == WAIT)
        {
            handled = false;
            WaitForWake();
        }
    }

    CONFIGURE_ACTION op;
    if (action == SETUP_PENDING) {
        op = CONFIGURE_PENDING;
    } else {
        op = CONFIGURE_LIVE;
    }
    return op;
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::NotifyConfigured() {
    bool handled = false;
    enum ACTION {
        NOTHING,
        PULL,
        WAKE
    };

    NOTIFY_STATE currentState = notifyState;
    ACTION action = NOTHING;
    while (handled == false) {
        action = NOTHING;
        NOTIFY_STATE newState = currentState;
        switch (currentState) {
            case CONFIGURING:
                /**
                 * We can't use read_available, since it is not provided in legacy boost
                 * versions (such as that available on Travis CLI).
                 *
                 * Unlike read_available, the empty method uses memory_order_relaxed to access
                 * the write pointer. This exposes us to concurrency issues, and is flagged up
                 * by the API:
                 *    "Due to the concurrent nature of the ringbuffer the result may be inaccurate."
                 *
                 * Whilst this is true in the general sense, the PipeSubscriber is protected by its
                 * state model. All reads and writes of the state variable have strict
                 * ordering, and therefore establish a memory fence. Since all pushes to the queue
                 * occurr *before* state update, we know that either:
                 *
                 *     1. The current value of empty() matches that seen by the WRITER
                 *        when the state was last updated
                 *     2. The WRITER thread is about to perform state resolution, in which
                 *        case it can handle both possible states (See ::PushComplete)
                 *           PULLING:           No action required from WRITER
                 *           NOTHING_TO_NOTIFY: Transition to PUBLISHING triggered
                 */
                if (messages.empty() == false) {
                    action = PULL;
                    newState = PULLING;
                } else {
                    newState = NOTHING_TO_NOTIFY;
                }
                break;

            case PULL_REQUIRED:
                // Fine - start the pull
                action = PULL;
                newState = PULLING;
                break;

            case PUB_RECONFIGURING:
                // Even if there are messages in the queue, this is fine. The publisher notified its
                // last write, which means there's nothing new to notify.
                action = NOTHING;
                newState = NOTHING_TO_NOTIFY;
                break;

            case SUB_WAKEUP_REQUIRED:
                action = WAKE;
                newState = CONFIGURING;
                break;

            case PREPARING_PUB_RECONFIGURE:
                throw ThreadingViolation { "Reconfiguration whilst the publisher was publishing!"};

            case PUB_WILL_RECONFIGURE:
                throw ThreadingViolation { "No reconfiguration was in progress!"};

            case DISABLED:
                throw ThreadingViolation { "Notifications were disabled whilst the client thread was configuring them!"};

            case NOTHING_TO_NOTIFY:
                throw ThreadingViolation { "Notifications were reset whilst the client thread was configuring them"};

            case RECONFIGURE_REQUIRED:
                throw ThreadingViolation { "Subscriber still requires reconfiguration, after reconfiguration is complete!"};

            case PUBLISHING:
                throw ThreadingViolation { "Publication started whilst the client was reconfiguring"};

            case PULLING:
                throw ThreadingViolation { "Second subscriber thread detected - a reconfiguration is already in process!"};

            case CLIENT_WILL_RECONFIGURE:
                throw ThreadingViolation { "Configuration completed whilst client was supposed to be setting up pending config!"};

        }

        // State change triggered...
        if (!handled) {
            handled = notifyState.compare_exchange_strong(
                    currentState,
                    newState);
        }
    }

    switch (action)
    {
    case NOTHING:
        break;
    case WAKE:
        WakeWaiter();
        break;
    case PULL:
        PullNextMessage();
        break;
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::NotifyPendingConfigured() {
    bool handled = false;

    enum ACTION {
        NOTHING,
        RECONFIGURE
    };

    ACTION action = NOTHING;
    NOTIFY_STATE currentState = notifyState;
    while (handled == false) {
        action = NOTHING;
        NOTIFY_STATE newState = currentState;
        switch (currentState) {
            case PREPARING_PUB_RECONFIGURE:
                newState = PUB_WILL_RECONFIGURE;
                break;

            case CLIENT_WILL_RECONFIGURE:
                newState = CONFIGURING;
                action = RECONFIGURE;
                break;

            case CONFIGURING:
                throw ThreadingViolation { "Pending configuration configured whilst the client was busy configuring"};

            case PULL_REQUIRED:
                throw ThreadingViolation { "Pending configuration configured whilst the client was busy configuring"};

            case PUB_RECONFIGURING:
                throw ThreadingViolation { "Pending configuration configured whilst the publisher was busy configuring"};

            case SUB_WAKEUP_REQUIRED:
                throw ThreadingViolation { "Pending configuration configured whilst the subscriber was waiting for the pub"};

            case PUB_WILL_RECONFIGURE:
                throw ThreadingViolation { "No pending reconfiguration was in progress!"};

            case DISABLED:
                throw ThreadingViolation { "No pending reconfiguration was in progress!"};

            case NOTHING_TO_NOTIFY:
                throw ThreadingViolation { "No pending reconfiguration was in progress!"};

            case RECONFIGURE_REQUIRED:
                throw ThreadingViolation { "No pending reconfiguration was in progress!"};

            case PUBLISHING:
                throw ThreadingViolation { "No pending reconfiguration was in progress!"};

            case PULLING:
                throw ThreadingViolation { "No pending reconfiguration was in progress!"};

        }

        // State change triggered...
        if (!handled) {
            handled = notifyState.compare_exchange_strong(
                    currentState,
                    newState);
        }
    }
    switch (action)
    {
    case NOTHING:
        break;
    case RECONFIGURE:
        ConfigurePendingNotify(CLIENT);
        break;
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PullComplete()
{
    enum ACTION {
        NOTHING,
        RECONFIGURE
    } action;

    bool handled = false;
    NOTIFY_STATE currentState = notifyState;
    while (handled == false) {
        action = NOTHING;
        NOTIFY_STATE newState = currentState;
        switch (currentState) {
            case PULLING:
                // Expected case - move back to the rest state
                newState = DISABLED;
                break;

            case RECONFIGURE_REQUIRED:
                // A reconfiguration request was triggered whilst we we pulling
                newState = PULL_REQUIRED;
                action = RECONFIGURE;
                break;

            case DISABLED:
                throw ThreadingViolation { "Pulling whilst the notifcation was disabled!"};

            case PUBLISHING:
                throw ThreadingViolation { "Pulling whilst the publisher was publishing!"};

            case PULL_REQUIRED:
                throw ThreadingViolation { "Pulling whilst the client was reconfiguring"};

            case CONFIGURING:
                throw ThreadingViolation { "Pulling whilst the client was reconfiguring"};

            case NOTHING_TO_NOTIFY:
                throw ThreadingViolation { "Pulling whilst there was nothing to notify!"};

            case SUB_WAKEUP_REQUIRED:
                throw ThreadingViolation { "Pulling whilst waiting for the publisher to release control!"};

            case PUB_WILL_RECONFIGURE:
                throw ThreadingViolation { "Pulling whilst waiting for the publisher to release control!"};

            case PREPARING_PUB_RECONFIGURE:
                throw ThreadingViolation { "Pulling whilst waiting for the publisher to release control!"};

            case PUB_RECONFIGURING:
                throw ThreadingViolation { "Pulling whilst waiting for the publisher to release control!"};

            case CLIENT_WILL_RECONFIGURE:
                throw ThreadingViolation { "Pulling whilst client was supposed to be reconfiguring!"};
        }

        // State change triggered...
        if (!handled) {
            handled = notifyState.compare_exchange_strong(
                    currentState,
                    newState);
        }

        switch(action)
        {
        case NOTHING:
            // Nothing to do...
            break;
         case RECONFIGURE:
             ConfigurePendingNotify(CLIENT);
             break;
        }
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::WakeWaiter() {
    onNotifyFlag.notify_one();
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::WaitForWake() {
    Lock lock(onNotifyMutex);
    onNotifyFlag.wait(lock);
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::ConfigurePendingNotify(const THREAD& thread) {
    onNotify = std::move(pending_onNotify);
    targetToNotify = pending_targetToNotify;
    pending_targetToNotify = nullptr;
    NotifyConfigured();
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PublishNextMessage() {
    if (targetToNotify) {
        targetToNotify->PostTask(onNotify);
        targetToNotify = nullptr;
    } else {
        onNotify();
    }
    onNotify = nullptr;
    PublishComplete();
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PullNextMessage() {
    if (targetToNotify) {
        targetToNotify->PostTask(onNotify);
        targetToNotify = nullptr;
    } else {
        onNotify();
    }
    onNotify = nullptr;
    PullComplete();
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::PushMessage(const Message& msg) {
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
        if ( !messages.push(msg) ) {
            throw PushToFullQueueException {msg};
        }
        if (!batching)
        {
            PushComplete();
        }
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::OnStateChange() {
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
    else if (state == IPipeConsumer<Message>::FINSIHED) {
        /**
         * By definition this must have come from the publisher thread.
         *
         * Nothing else will ever be pushed to the queue. Wake the client
         * thread (if it is waiting...)
         */
         this->PushComplete();
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
bool PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::GetNextMessage(Message& msg) {
    bool gotMsg = false;
    if ( messages.pop(msg) ) {
        gotMsg = true;
    }

    return gotMsg;
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
bool PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::WaitForMessage(Message &msg) {
    bool gotMsg = GetNextMessage(msg);
    if (!gotMsg) {
        bool complete = Complete();

        if (complete) {
            // One final check in case the final push
            // came in between the two...
            gotMsg = GetNextMessage(msg);
        } else {
            // Looks like we could require a block - prepare the callback
            auto prom = std::make_shared<std::promise<bool>>();
            OnNextMessage([=] () -> void {
                prom->set_value(true);
            });

            // But did the publisher catch up in the interim?
            gotMsg = GetNextMessage(msg);

            if (!gotMsg && !Complete()) {
                // Ok fine - no choice but to block
                prom->get_future().wait();

                gotMsg = GetNextMessage(msg);
            }
        }
    }
    return gotMsg;
}


template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::OnNextMessage(const NextMessageCallback& f) {
    this->OnNextMessage(f,nullptr);
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::StartBatch() {
    batching = true;
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::EndBatch() {
    if (batching) {
        this->PushComplete();
        batching = false;
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::OnNextMessage(
         const NextMessageCallback& f,
         IPostable* target)
{
    if (!this->aborted) {

        CONFIGURE_ACTION  action = this->NotifyRequested();
        switch (action)
        {
        case CONFIGURE_LIVE:
            onNotify = f;
            targetToNotify = target;
            NotifyConfigured();
            break;

        case CONFIGURE_PENDING:
            pending_onNotify = f;
            pending_targetToNotify = target;
            NotifyPendingConfigured();
            break;
        }
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
void PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::OnNewMessage(const NewMessageCallback& f) {
    if (!this->aborted) {
        Lock notifyLock(onNotifyMutex);

        forwardMessage = true;
        onNewMessage = f;
        while (!this->aborted && messages.consume_one(f)) { }
    }
}

template<class Message, class NextMessageCallback, class NewMessageCallback>
bool PipeSubscriber<Message, NextMessageCallback, NewMessageCallback>::Complete() const {
    auto state = this->State();
    return (state == IPipeConsumer<Message>::FINSIHED ||
            state == IPipeConsumer<Message>::ABORTED);
}

