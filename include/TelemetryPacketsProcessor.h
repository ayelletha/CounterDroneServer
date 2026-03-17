#include "ThreadsSharedDataManager.h"
#include "TelemetryPacket.h"

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp

class TelemetryPacketsProcessor
{
private:
        ThreadsSharedDataManager<TelemetryPacket>& m_packets_manager; // Pay Attention! this is a REFFERENCE type !

public:

    explicit TelemetryPacketsProcessor(ThreadsSharedDataManager<TelemetryPacket>& manager)
        : m_packets_manager(manager)
    {
    }
    
    void process_loop()
    {
        // while (g_keep_running_system)
        // {
        //      // every time that a new telemetry packet can be popped from the shared queue (m_packets_manager.pop(...)) 
        //     //  process it: check its validity and if valid do something
        //     TelemetryPacket packet;
        //     if (m_packets_manager.pop(packet))
        //     {
        //         // processs packet
        //     }
        // }
    }
};
