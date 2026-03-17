#include "ThreadsSharedDataManager.h"
#include "TelemetryPacket.h"
#include <map>
#include <string>

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp
constexpr int DRONE_ACTIVATION_TIMEOUT_SEC = 30; // change it to value ~5 to see how drone's activation status is swithed

class TelemetryDataProcessor
{
private:
    ThreadsSharedDataManager<TelemetryData>& m_shared_telemetry_packets_manager; // payload-data of parsed valid telemetry packets that found inside the raw bytes sequence
    std::map<std::string, std::pair<bool, TelemetryData>> m_drones_status; // For each drone (identified by its id string) save its communication status (active/not) + the last telemetry data that was received for it

public:

    explicit TelemetryDataProcessor(ThreadsSharedDataManager<TelemetryData>& manager);
    void process_loop();
    const std::map<std::string, std::pair<bool, TelemetryData>>& drones_status();
};
