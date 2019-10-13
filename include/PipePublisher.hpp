template <class Message>
PipePublisher<Message>::PipePublisher() 
   : numClients(0)
{
}

template <class Message>
PipePublisher<Message>::~PipePublisher() 
{
}

template <class Message>
void PipePublisher<Message>::DoLock() {
    thread_local std::thread::id THIS_THREAD = std::this_thread::get_id();
    subscriptionMutex.lock();
    subscriptionLockOwner = THIS_THREAD;
}

template <class Message>
bool PipePublisher<Message>::TryLock() {
    bool locked = subscriptionMutex.try_lock();
    thread_local std::thread::id THIS_THREAD = std::this_thread::get_id();

    if (locked)
    {
        subscriptionLockOwner = THIS_THREAD;
    }

    return locked;
}

template <class Message>
void PipePublisher<Message>::Unlock() {
    static std::thread::id NO_THREAD;
    subscriptionLockOwner = NO_THREAD;
    subscriptionMutex.unlock();
}

template<class Message>
inline void PipePublisher<Message>::Done() {
    EndBatch();

    Lock clientLock(*this);

    for (auto it = clients.begin(); it != clients.end();) {
        ClientRef& client = *it;

        bool aborting = (
                client->state.load(std::memory_order_relaxed) ==
                IPipeConsumer<Message>::ABORTING);

        if (aborting || client.unique())
        {
            RemoveClient(it);
        }
        else
        {
            client->Done();
            ++it;
        }
    }
}

template <class Message>
void PipePublisher<Message>::Publish(const Message& msg) {
    if (currentBatch.get()) {
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            ClientRef& client = *it;
            client->PushMessage(msg);
        }
    } else if (numClients > 0) {
        Lock clientLock(*this);

        for (auto it = clients.begin(); it != clients.end();) {
            ClientRef& client = *it;

            bool aborting = (
                    client->state.load(std::memory_order_relaxed) ==
                    IPipeConsumer<Message>::ABORTING);

            if (aborting || client.unique())
            {
                RemoveClient(it);
            }
            else
            {
                client->PushMessage(msg);
                ++it;
            }
        }
    }
}

template <class Message>
template <class Client, class... Args>
std::shared_ptr<Client> PipePublisher<Message>::NewClient(Args&&... args) {
    std::shared_ptr<Client> client (new Client(this, std::forward<Args>(args)...));
    InstallClient(client);
    return client;

}

template <class Message>
void PipePublisher<Message>::RemoveClient(typename ClientList::iterator it) {
    clients.erase(it);
    --numClients;
}

template<class Message>
inline PipePublisher<Message>::Lock::Lock(Type& _parent)
    : parent(_parent)
{
    parent.DoLock();
}

template<class Message>
inline PipePublisher<Message>::Lock::~Lock() {
    parent.Unlock();
}

template<class Message>
size_t PipePublisher<Message>::NumClients() {
    return numClients;
}

template<class Message>
void PipePublisher<Message>::InstallClient(
    std::shared_ptr<IPipeConsumer<Message>> client)
{
    Lock clientLock(*this);
    client->publishers.push_back(this);
    clients.emplace_back(std::move(client));
    ++numClients;
}

template<class Message>
void PipePublisher<Message>::StartBatch() {
    currentBatch.reset(nullptr); // Must destroy the old BEFORE allocating the
                                 // new, otherwise we get a deadlock

    currentBatch.reset(new Batch(*this));
}

template<class Message>
void PipePublisher<Message>::EndBatch() {
    currentBatch.reset(nullptr);
}


template<class Message>
inline PipePublisher<Message>::Batch::Batch(Type& _parent)
   : parent(_parent), lock(Type::Lock(_parent))
{
    for (auto it = parent.clients.begin(); it != parent.clients.end(); ++it) {
        ClientRef& client = *it;
        client->StartBatch();
    }
}


template<class Message>
PipePublisher<Message>::Batch::~Batch() {
    for (auto it = parent.clients.begin(); it != parent.clients.end();) {
        ClientRef& client = *it;

        bool aborting = (
                client->state.load(std::memory_order_relaxed) ==
                IPipeConsumer<Message>::ABORTING);

        if (aborting || client.unique())
        {
            parent.RemoveClient(it);
        }
        else
        {
            client->EndBatch();
            ++it;
        }
    }
}


template<class Message>
bool PipePublisher<Message>::HaveLock() {
    thread_local std::thread::id THIS_THREAD = std::this_thread::get_id();
    return subscriptionLockOwner == THIS_THREAD;
}
