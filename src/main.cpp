#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include "ThreadsSharedDataManager.h"
#include "DroneDataSimulator.h"
#include "DroneDataSensor.h"
#include "SensorDataConsumer.h"
#include "TelemetryDataProcessor.h"

// a global variable to control the program's running state, used for graceful shutdown for clean & stop all the program's threads correctly
std::atomic<bool> g_keep_running_system{true};
int LOG_LEVEL = LogLevel::PRODUCTION | LogLevel::DEBUG_PACKETS_FILTERRING | LogLevel::DEBUG_SIMULATOR;

void signal_handler(int signal)
{
    std::cout << "\n[System] Signal " << signal << " received. Shutting down gracefully...\n";
    g_keep_running_system = false;
}

int main()
{
    std::signal(SIGINT, signal_handler); // for user's interruption by Ctrl+C
    std::signal(SIGTERM, signal_handler); // for system's interruption by kill command or system shutdown

    std::cout << "Uploading Counter-Drone-System...\n";

    ThreadsSharedDataManager<BytesArray> m_shared_raw_data_manager;
    ThreadsSharedDataManager<TelemetryData> m_shared_telemetry_packets_manager;
    
    std::vector<BytesArray> valid_packets_sent;

    DroneDataSimulator drondata_simulator;
    DroneDataSensor drone_data_sensor(m_shared_raw_data_manager);
    SensorDataConsumer sensor_data_consumer(m_shared_raw_data_manager, m_shared_telemetry_packets_manager);
    TelemetryDataProcessor packets_processor(m_shared_telemetry_packets_manager);

    std::vector<std::thread> thread_pool;

    std::cout << "Start running threads...\n";

    thread_pool.emplace_back(&DroneDataSimulator::process_loop, &drondata_simulator);
    thread_pool.emplace_back(&DroneDataSensor::process_loop, &drone_data_sensor);
    thread_pool.emplace_back(&SensorDataConsumer::process_loop, &sensor_data_consumer);
    thread_pool.emplace_back(&TelemetryDataProcessor::process_loop, &packets_processor);
    
    while (g_keep_running_system)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n[System] Stopping threads safely...\n";

    for (auto& t : thread_pool)
        if (t.joinable())
            t.join();

    std::cout << "[System] Counter-Drone-System is stopped...\n";
    return 0;
}