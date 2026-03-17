#include "ThreadsSharedDataManager.h"
#include "TelemetryPacket.h"
#include "Generals.h"

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp

class SensorDataConsumer
{
private:
    ThreadsSharedDataManager<BytesArray>& m_raw_data_queue; // Pay Attention! this is a REFFERENCE type !
    ThreadsSharedDataManager<TelemetryPacket>& m_packets_queue; // Pay Attention! this is a REFFERENCE type !

public:
    SensorDataConsumer(
        ThreadsSharedDataManager<BytesArray>& raw_data_manager, 
        ThreadsSharedDataManager<TelemetryPacket>& packets_manager) 
        : m_raw_data_queue(raw_data_manager), m_packets_queue(packets_manager)
    {
    }
    
    void process_loop()
    {
        // while (g_keep_running_system)
        // {
        //     // every time that a new data is received on the TCP socket - read it and collect into bytes array,
        //     //   then push this bytes array to the shared queue (m_raw_data_queue.push_data(...)) 
        //     //   so that the data-consumer thread will consume it and process it as telemetry packet
        // }
    }
};