#ifndef THREADS_SHARED_DADA_MANAGER_H
#define THREADS_SHARED_DADA_MANAGER_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp

template <typename T>
class ThreadsSharedDataManager
{
private:
    std::queue<T> m_shared_data;  
    std::mutex m_mutex;
    std::condition_variable m_cond_var;

public:
    /**
     * Push new data to shared queue and notify the data's consumer
     * @param item - the data to be added to the queue
     */
    void push_data(T item)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shared_data.push(std::move(item));
        m_cond_var.notify_one(); // only 1 unit of data was added, so we only need to notify one waiting thread (the data-consumer) to wake up and consume this new data
    }

    /**
     * Pop data from the shared queue
     * @param item - reference where to store the popped data
     * @return true if data was successfully popped, false if the queue is empty and not active
     */
    bool pop_data(T& item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        // The thread that calls pop() will wait here until either there is data in the queue or the program is stopped (for example, by Ctrl+C or system interrupt),
        m_cond_var.wait(lock, [this]() { return !m_shared_data.empty() || !g_keep_running_system; });

        // If there is some un-poped shared data, even if the program is stopped, we want to pop it and process it, because it may be important data that we don't want to lose!!
        // In that case, only when all data was popped, the data-consumer thread (the "pop" caller) can be killed safely, so we return "false" to mention this.
        if (!g_keep_running_system && m_shared_data.empty())
            return false;

        item = std::move(m_shared_data.front()); // use "move" instead copy for better performance (time)
        m_shared_data.pop(); // the first data unit was taken, so we can pop it from the queue

        return true;
    }

    /**
     * Wake up all the threads that are waiting on the condition variable.
     * Used, for example, when execute a safely close of the entire program
    */
    void wake_up_all()
    {
        m_cond_var.notify_all();
    }
};

#endif // THREADS_SHARED_DATA_MANAGER_H