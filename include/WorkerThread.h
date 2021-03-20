/*
 * WorkerThread.h
 *
 *  Created on: 24 Jan 2016
 *      Author: lhumphreys
 */


#ifndef DEV_TOOLS_CPP_LIBRARIES_LIBTHREADCOMMS_WORKERTHREAD_H_
#define DEV_TOOLS_CPP_LIBRARIES_LIBTHREADCOMMS_WORKERTHREAD_H_

#include "IPostable.h"
#include <thread>
#include <condition_variable>
#include <queue>
#include <mutex>
#include <future>
#include <list>
#include <PipePublisher.h>

class WorkerThread: public IPostable {
public:
    WorkerThread();

    /**
     * Post a task to the event loop.
     *
     * @param t  The task to be run.
     */
    void PostTask(const Task& t);

    /**
     * Post a task, and wait for the result
     *
     * @param t  The task to be run.
     *
     * @returns true if the task was executed, or false if it was aborted
     */
    bool DoTask(const Task& t);

    virtual ~WorkerThread();

    /**
     * Stop the thread, may be called by any thread.
     *
     * The function will wait to acquire the lock before executing the abort.
     */
    void Abort();

    /**
     * Start the worker
     *  @returns true - the tread was successfully started, false otherwise
     */
    bool Start();

    /**
     * Join the thread
     */
    void Join();

    /**
     * Process updates from the specified publisher on the worker thread,
     * calling the task for each update.
     *
     * Updates are consumed using the OnNextMessage interface. In order to avoid
     * blocking out the thread the number of updates handled in one go is
     * limited by maxSlizeSize.
     *
     * The function will block until the handler is installed, which means that
     * either the thread must already be running, or another thread will need to
     * start it to avoid deadlock.
     *
     * @param publisher      The publisher to consume updates from
     * @param t              The task to execute for each update.
     * @param maxQueueSize   Argument to NewClient - must be large enough to the
     *                       ring buffer wrapping whilst other tasks are being
     *                       executed.
     * @param maxSlizeSize   The maximum number of updates to consume in one
     *                       "task". After this execution will return to the
     *                       event loop, and other tasks will be serviced before
     *                       consumption is resumed.
     */
    template<class Msg, class Publisher=PipePublisher<Msg>>
    void ConsumeUpdates(
        Publisher& publisher,
        const std::function<void (Msg& m)>& task,
        size_t maxQueueSize = 1000000,
        size_t maxSlizeSize = 100);

    template<class Msg>
    void ConsumeUpdates(
            std::shared_ptr<PipeSubscriber<Msg>> client,
            const std::function<void (Msg& m)>& task,
            size_t maxSlizeSize = 100);

    /**
     * Check if this function was invoked on the actual thread
     */
    bool CurrentlyOnWorkerThread() const;

private:
    enum STATE {NOT_STARTED, RUNNING, SLEEPING, STOPPED, ABORTED};
    /**
     * The thread's main loop
     */
    void Run();

    /**
     * Execute all tasks on the queue, until it is exhausted.
     *
     * lock will be released whilst individual tasks are being executed, and
     * then re-acquired.
     */
    void DoTasks(std::unique_lock<std::mutex>& lock);

    /**
     * Get the next task, MUST be called whilst already under lock
     */
    Task* GetTask();

    /**
     * Mark the current task as done, MUST be called whilst already under lock
     */
    void TaskDone();

    /**
     * Wake the run thread, in newState. MUST be invoked under lock.
     *
     * If the thread is not sleeping, the state is transitioned immediately.
     *
     * @param newState  The state of the thread after it has been woken. This
     *                  would usually be RUNNING, indicating the thread should
     *                  continue, but may also be ABORTED.
     */
    void Wake(STATE newState);

    /**
     * Nothing left to do, wait for more work.
     *
     * The lock will be released whilst we wait, and will be re-acquired before
     * Sleep returns...
     *
     * @param lock   The runtime lock
     */
    void Sleep(std::unique_lock<std::mutex>& lock);

    /**
     * Stop the thread.
     *
     * The queue lock will be acquired to perform this operation
     */
	void Stop();

    /**
     * Callback from ConsumeUpdates
     */
    template<class Msg>
    void HandleUpdates(
        PipeSubscriber<Msg>& client,
        const std::function<void (Msg& m)>& task,
        size_t maxSlizeSize = 100);

    struct Job {
        Task task;
        bool promised;
        std::promise<bool> result;

        /**
         * The job will never be executed.
         *
         * Notify anyone waiting on us that they will not get a result.
         */
        void Cancel();
    };

    /**
     * Cancel the specified jobs, and any that would be executed after it
     *
     * NOTE: This function must be called under lock
     *
     * @param it The first job to cancel
     */
	void CancelJobs(std::list<Job>::iterator it);

    std::mutex                   queueMutex;
    std::condition_variable      notification;
    std::list<Job>               workQueue;
    std::atomic<STATE>           state;
    std::thread                  worker;

    std::vector<std::shared_ptr<void>> clients;
};

template<class Msg, class Publisher>
inline void WorkerThread::ConsumeUpdates(
    Publisher& publisher,
    const std::function<void (Msg& m)>& task,
    size_t maxQueueSize,
    size_t maxSlizeSize)
{
    /**
     * We need to be on the worker thread to initialize the client...
     */
    auto initializer = [&publisher, task,maxQueueSize, maxSlizeSize, this] () -> void {
        std::shared_ptr<PipeSubscriber<Msg>> client =
                publisher.NewClient(maxQueueSize);

        clients.push_back(client);

        HandleUpdates(*client,task,maxSlizeSize);
    };

    this->DoTask(initializer);
}

template<class Msg>
void WorkerThread::ConsumeUpdates(std::shared_ptr<PipeSubscriber<Msg>> client,
                                  const std::function<void(Msg&)>& task,
                                  size_t maxSlizeSize)
{
    /**
     * We should hve been on the worker thread to initialise the client.
     *
     * Given we are not - it is the caller's responsibility to ensure that
     * this is safe.
     */
    auto initializer = [client, task, maxSlizeSize, this] () -> void {
        clients.push_back(client);
        HandleUpdates(*client,task,maxSlizeSize);
    };

    this->DoTask(initializer);

}

template<class Msg>
inline void WorkerThread::HandleUpdates(
    PipeSubscriber<Msg>& client,
    const std::function<void(Msg& m)>& task,
    size_t maxSlizeSize)
{
    Msg m;
    size_t count = 0;
    while (count < maxSlizeSize && client.GetNextMessage(m)) {
        task(m);
        ++count;
    }

    // This can't be done until this function call is complete...
    this->PostTask([this, &clientRef = client, maxSlizeSize, task] () -> void {
        auto callback =  [this, &clientRef = clientRef, maxSlizeSize, task] () -> void {
            this->HandleUpdates(clientRef,task,maxSlizeSize);
        };
        clientRef.OnNextMessage(callback,this);
    });

}


#endif /* DEV_TOOLS_CPP_LIBRARIES_LIBTHREADCOMMS_WORKERTHREAD_H_ */
