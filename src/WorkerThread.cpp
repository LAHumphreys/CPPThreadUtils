/*

 *
 *  Created on: 24 Jan 2016
 *      Author: lhumphreys
 */

#include "WorkerThread.h"

WorkerThread::WorkerThread()
   : state(NOT_STARTED)
{
}

void WorkerThread::PostTask(const Task& t) {
    std::unique_lock<std::mutex> lock(queueMutex);
    if (state != ABORTED) {
        workQueue.push_back({t,false, std::promise<bool>()});

        if (state == SLEEPING) {
            Wake(RUNNING);
        }
    }
}

void WorkerThread::CancelJobs(std::list<Job>::iterator it) {

    while (it != workQueue.end()) {
        it->Cancel();
        workQueue.erase(it++);
    }
}

void WorkerThread::Stop() {
    std::unique_lock<std::mutex> lock(queueMutex);
    if (!workQueue.empty())
    {
        // Can't pop the first item as it may be being executed.
        CancelJobs(++workQueue.begin());
    }
    Wake(STOPPED);
}

WorkerThread::~WorkerThread() {
    Abort();
    Join();
    CancelJobs(workQueue.begin());
}

void WorkerThread::Run() {

    std::unique_lock<std::mutex> lock(queueMutex);
    while (state == RUNNING) {
        // Drain the queue. Note that the lock will be relinquished during task
        // execution to allow further work to be posted.
        DoTasks(lock);

        if (state == RUNNING) {
            // Queue is exhausted, and we have the lock so no more work can be
            // posted until we start waiting...
            Sleep(lock);
        } else {
            // Abort received whilst executing tasks - time to leave...
        }
    }
}

void WorkerThread::DoTasks(std::unique_lock<std::mutex>& lock) {
    for (Task* t = GetTask(); t != nullptr; t = GetTask())
    {
        lock.unlock();
        (*t)();
        lock.lock();

        TaskDone();
    }
}

WorkerThread::Task* WorkerThread::GetTask() {
    Task* task = nullptr;
    if (state==RUNNING && !workQueue.empty()) {
        task = &workQueue.front().task;
    }
    return task;
}

void WorkerThread::Abort() {
    Stop();
    std::unique_lock<std::mutex> lock(queueMutex);
    Wake(ABORTED);
}

bool WorkerThread::Start() {
    bool started = false;
    std::unique_lock<std::mutex> lock(queueMutex);
    if (state == NOT_STARTED) {
        state = RUNNING;
        worker = std::thread([this]() -> void { this->Run(); });
        started = true;
    }

    return started;
}

void WorkerThread::Join() {
    if (worker.joinable())
    {
        worker.join();
    }
}

bool WorkerThread::DoTask(const Task& t) {
    bool ok = false;
    std::future<bool> result;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (state != ABORTED)
        {
            workQueue.push_back({t,true,std::promise<bool>()});
            result = workQueue.back().result.get_future();

            if (state == SLEEPING) {
                Wake(RUNNING);
            }
        }
    }

    if (result.valid()) {
        ok = result.get();
    }

    return ok;
}

void WorkerThread::TaskDone() {
    Job& job = workQueue.front();
    if (job.promised)
    {
        job.result.set_value(true);
    }
    workQueue.pop_front();
}

void WorkerThread::Wake(STATE newState) {
    bool sleeping = (state == SLEEPING);
    state = newState;

    if (sleeping) {
        notification.notify_all();
    }
}

void WorkerThread::Sleep(std::unique_lock<std::mutex>& lock) {
    if (state == RUNNING) {
        state = SLEEPING;
        notification.wait(lock);
    } else {
        // It is not valid to sleep unless we are running.
    }
}

bool WorkerThread::CurrentlyOnWorkerThread() const {
    return (state != NOT_STARTED) && (std::this_thread::get_id() == worker.get_id());
}

void WorkerThread::Job::Cancel() {
    if (promised) {
        result.set_value(false);
    }
}
