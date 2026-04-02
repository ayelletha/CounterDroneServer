#ifndef THREADS_SHARED_DADA_MANAGER_H
#define THREADS_SHARED_DADA_MANAGER_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp

/**
 * This class is a general template to implement a machanism of thread-safe sharring data
 */
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
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shared_data.push(std::move(item));
        } // unlock 'm_mutex' AFTER finish push new data, but BEFORE notify threads that are waiting on 'm_cond_var', to avoid case of Pessimization
       
        m_cond_var.notify_one(); // only 1 unit of data was added, so we only need to notify one waiting thread (the data-consumer) to wake up and consume this new data
    }

    /**
     * Pop the first data from the shared queue
     * @param item - reference where to store the popped data
     * @return true if data was successfully popped, false if the queue is empty and not active
     */
    bool pop_data_with_timeout(T& item, int timeout_ms)
    {
        /* when we call mutex's lock before 'm_cond_var.wait_for', we avoid the race condition of 
             pushing new data (by the producer thread) to the queue
             a micro-sec AFTER the consumer thread found out that there is no data to pop, so started falling a sleep. 
           Inside the 'wait_for' the OS unlock the mutex AND puts the thread into sleep - in single atomic operation, to allow now a new data to be added by the producer thread.
           Once new data was added, the producer thread notify to wake up the consumer thread, that lock the mtex again and pop this new data while the mutex is locked
        */
        std::unique_lock<std::mutex> lock(m_mutex);
        
        // The thread that calls pop() will wait here until either timeout reached or there is data in the queue or the program is stopped (for example, by Ctrl+C or system interrupt),
        bool woke_up_with_data = m_cond_var.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                                                     [this]() { return !m_shared_data.empty() || !g_keep_running_system; });

        // Graceful shutdown check
        if (!g_keep_running_system && m_shared_data.empty())
            return false;

        // If we woke up because we actually have data (not just a timeout)
        if (woke_up_with_data && !m_shared_data.empty())
        {
            item = std::move(m_shared_data.front());
            m_shared_data.pop();
            return true;
        }

        // Timeout reached, queue is still empty
        return false;
    }

    /**
     * Fetch all currently available items in the queue at once (Batch Processing).
        If the queue is empty, it blocks and waits until at least one item arrives.
    */
    bool pop_all(std::vector<T>& items)
    {
        /* See comment at "pop_data_with_timeout" about the mutex-locking & waiting_for notification on the condition variable...
        */
        std::unique_lock<std::mutex> lock(m_mutex);
           
        // The thread that calls pop() will wait here until either there is data in the queue or the program is stopped (for example, by Ctrl+C or system interrupt),
        m_cond_var.wait(lock, [this]() { return !m_shared_data.empty() || !g_keep_running_system; });

        // Graceful shutdown check
        if (!g_keep_running_system && m_shared_data.empty())
            return false;

        // Drain the entire queue into the provided vector in a single lock operation
        while (!m_shared_data.empty())
        {
            items.push_back(std::move(m_shared_data.front()));
            m_shared_data.pop();
        }

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