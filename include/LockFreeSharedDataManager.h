#include <vector>
#include <atomic>

template<typename T>
class SPSCQueue 
{
private:
    std::vector<T> m_buffer;
    const size_t m_capacity;
    
    // alignas(64) prevents False Sharing by keeping these atomics on separate CPU cache lines
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};

public:
    explicit SPSCQueue(size_t capacity) : m_capacity(capacity + 1)
    {
        // Pre-allocate memory on the heap to avoid dynamic allocations during real-time processing
        m_buffer.resize(m_capacity);
    }

    bool push(const T& data) 
    {
        // load() with relaxed is enough because only this producer thread modifies m_tail
        size_t current_tail = m_tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % m_capacity;

        // acquire ensures we see the latest update of m_head from the consumer
        if (next_tail == m_head.load(std::memory_order_acquire)) 
        {
            return false; // Queue is full
        }

        // 1. First, write the data to the buffer
        m_buffer[current_tail] = data;

        // 2. Only then, advance the index.
        // release ensures that the data written in step 1 is fully visible before the index is updated
        m_tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& out_data) 
    {
        // load() with relaxed is enough because only this consumer thread modifies m_head
        size_t current_head = m_head.load(std::memory_order_relaxed);

        // acquire ensures we see the latest update of m_tail from the producer
        if (current_head == m_tail.load(std::memory_order_acquire)) 
        {
            return false; // Queue is empty
        }

        // 1. First, read the data
        out_data = m_buffer[current_head];

        // 2. Move forward and signal the producer that the cell can be safely overwritten
        m_head.store((current_head + 1) % m_capacity, std::memory_order_release);
        return true;
    }

    size_t pop_all(std::vector<T>& out_data) 
    {
        // 1. Read our head (relaxed, only we change it)
        size_t current_head = m_head.load(std::memory_order_relaxed);

        // 2. Read the tail from the producer (acquire, to ensure we see all its memory writes)
        size_t current_tail = m_tail.load(std::memory_order_acquire);

        // If the queue is empty, do nothing
        if (current_head == current_tail) 
        {
            return 0; 
        }


        // Calculate how many items are currently available to avoid multiple re-allocations
        // (Handling the wrap-around case of the ring buffer)
        size_t available_items = (current_tail >= current_head) ? 
                                 (current_tail - current_head) : 
                                 (m_capacity - current_head + current_tail);
        
        // Reserve space in the output vector to prevent dynamic allocations during the loop
        out_data.reserve(out_data.size() + available_items);

        // 3. Extract all available items until we catch up to the current tail
        size_t count = 0;
        while (current_head != current_tail) 
        {
            // Use std::move to avoid copying if T is a complex object (like BytesArray)
            out_data.push_back(std::move(m_buffer[current_head]));
            
            // Advance our local head pointer
            current_head = (current_head + 1) % m_capacity;
            count++;
        }

        // 4. Update the atomic head exactly ONCE for the entire batch!
        // release ensures that all extracted items are safely moved before the producer can overwrite them
        m_head.store(current_head, std::memory_order_release);
        
        return count;
    }

};


template<typename T>
class MPMCQueue
{
private:
    struct Cell
    {
        std::atomic<size_t> sequence;
        T data;
    };

    // The buffer now consists of cells, each with its own atomic sequence status
    std::vector<Cell> m_buffer;
    const size_t m_capacitymask_; // Must be a power of 2 (e.g., 1024) for bitwise optimization

    alignas(64) std::atomic<size_t> m_enqueue_pos{0};
    alignas(64) std::atomic<size_t> m_dequeue_pos{0};

public:
    explicit MPMCQueue(size_t capacity) : m_capacitymask_(capacity - 1)
    {
        // Important: capacity must be a power of 2 for the bitwise AND operator to work as modulo!
        m_buffer.resize(capacity);
        for (size_t i = 0; i < capacity; ++i)
        {
            // Initialize the queue: cell 'i' is ready to receive data in lap 'i'
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool push(const T& data)
    {
        Cell* cell = nullptr;
        size_t pos = m_enqueue_pos.load(std::memory_order_relaxed);

        while (true)
        {
            cell = &m_buffer[pos & m_capacitymask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)seq - (intptr_t)pos;

            if (dif == 0)
            {
                // This cell matches our lap, let's try to reserve it using CAS!
                if (m_enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    break; // Successfully reserved, we can break the loop and write
                }
            }
            else if (dif < 0)
            {
                return false; // Queue is full
            }
            else
            {
                // Another producer just modified the queue, reload the position and try again
                pos = m_enqueue_pos.load(std::memory_order_relaxed);
            }
        }

        // Reached here? The 'cell' is exclusively ours! No mutex, but the CAS protected us.
        cell->data = data;
        
        // Now signal the consumers that the cell is ready for reading (increment seq by 1)
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out_data)
    {
        Cell* cell = nullptr;
        size_t pos = m_dequeue_pos.load(std::memory_order_relaxed);

        while (true)
        {
            cell = &m_buffer[pos & m_capacitymask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);

            if (dif == 0)
            {
                // This cell is ready for reading in our lap! Let's try to reserve it using CAS
                if (m_dequeue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    break; 
                }
            } 
            else if (dif < 0) 
            {
                return false; // Queue is empty
            } 
            else
            {
                // Another consumer just modified the queue, reload the position and try again
                pos = m_dequeue_pos.load(std::memory_order_relaxed);
            }
        }

        // Cell reserved. We are the only consumer reading from it right now.
        out_data = cell->data;
        
        // Signal the upcoming producers that this cell is free for the next lap (pos + mask + 1)
        cell->sequence.store(pos + m_capacitymask_ + 1, std::memory_order_release);
        return true;
    }
};