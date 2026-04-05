#include <iostream>
#include <iomanip> // Required for output formatting (std::setw, std::fixed, etc.)
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include "ThreadsSharedDataManager.h"
#include "DroneDataSimulator.h"
#include "DroneDataSensor.h"
#include "SensorDataParser.h"
#include "TelemetryDataProcessor.h"

// a global variable to control the program's running state, used for graceful shutdown for clean & stop all the program's threads correctly
std::atomic<bool> g_keep_running_system{true};
int LOG_LEVEL = LogLevel::PRODUCTION /*| LogLevel::DEBUG_SIMULATOR | LogLevel::DEBUG_NETWORK | LogLevel::DEBUG_PACKETS_FILTERRING | LogLevel::DEBUG_VALID_PACKETS_BUSSINES_LOGIC*/;

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

    DroneDataSimulator drone_data_simulator;
    DroneDataSensor drone_data_sensor(m_shared_raw_data_manager);
    SensorDataParser sensor_data_consumer(m_shared_raw_data_manager, m_shared_telemetry_packets_manager);
    TelemetryDataProcessor valid_packets_processor(m_shared_telemetry_packets_manager);

    std::vector<std::thread> thread_pool;

    std::cout << "Start running threads...\n";

    thread_pool.emplace_back(&DroneDataSimulator::process_loop, &drone_data_simulator);
    thread_pool.emplace_back(&DroneDataSensor::process_loop, &drone_data_sensor);
    thread_pool.emplace_back(&SensorDataParser::process_loop, &sensor_data_consumer);
    thread_pool.emplace_back(&TelemetryDataProcessor::process_loop, &valid_packets_processor);
    
    while (g_keep_running_system) // to avoid terminate the main proccess of the Counter-Drones-System
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // to avoid polling the CPU
    }

    std::cout << "\n[System] Stopping threads safely...\n";

    for (auto& t : thread_pool)
        if (t.joinable())
            t.join();

    // Print running conclusions:
    std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // in order to prevent log printing collisions
    std::cout << "\n\n";
    std::cout << std::string(17, '*') << " Traffic Conclusion " << std::string(17, '*') << "\n";
    std::cout << "Sent :\n"
              << "  " << drone_data_simulator.sent_valid_packets().size() + drone_data_simulator.corrupted_packets_amount() << " packets sent, of which:\n"
              << "    " << drone_data_simulator.fragmented_packets_amount() << " valid but fragmented\n"
              << "    " << drone_data_simulator.corrupted_packets_amount() << " corrupted\n"
              << "+ " << drone_data_simulator.garbage_sequences_amount() << " garbage sequences in between those\n\n";
    std::cout << "Received :\n"
              << "  " << sensor_data_consumer.received_valid_packets().size() << " valid packets\n"
              << "  " << sensor_data_consumer.crc_errors_amount() << " packets with CRC errors\n"
              << "  " << sensor_data_consumer.invalid_structure_amount() << " malformed packets (structural)\n"
              << "  msg-types statistics:\n" 
              << "    " << sensor_data_consumer.telemetry_pkt_count() << " telemetry data packets\n"
              << "    " << sensor_data_consumer.heart_beat_pkt_count() << " heart-beat data packets\n"
              << "    " << sensor_data_consumer.cmd_pkt_count() << " command data packets\n";

    std::cout << "\n\n";
    std::cout << std::string(20, '*') << " Drones Status " << std::string(20, '*') << "\n";
    const auto& status_map = valid_packets_processor.drones_status();
    if (status_map.empty())
    {
        std::cout << "No drones were detected during this session.\n";
    }
    else
    {
        // Print the table header with aligned columns
        std::cout << std::left 
                << std::setw(15) << "Drone ID" 
                << std::setw(12) << "Status" 
                << std::setw(15) << "Latitude" 
                << std::setw(15) << "Longitude" 
                << std::setw(12) << "Alt (m)" 
                << std::setw(12) << "Speed (m/s)" 
                << "\n";
        
        // Print a separator line
        std::cout << std::string(80, '-') << "\n";

        // Iterate over the map and print each drone's final known data
        for (const auto& pair : status_map)
        {
            const std::string& drone_id = pair.first;
            bool is_active = pair.second.first;
            const TelemetryData& data = pair.second.second;

            std::cout << std::left 
                    << std::setw(15) << drone_id 
                    << std::setw(12) << (is_active ? "ACTIVE" : "INACTIVE")
                    << std::fixed // Ensure floating point numbers don't switch to scientific notation
                    << std::setprecision(4) // 4 decimal places for GPS coordinates
                    << std::setw(15) << data.latitude 
                    << std::setw(15) << data.longitude 
                    << std::setprecision(2) // 2 decimal places for altitude and speed
                    << std::setw(12) << data.altitude 
                    << std::setw(12) << data.speed 
                    << "\n";
        }
    }
    // Print a closing separator line
    std::cout << std::string(80, '-') << "\n";

 
    std::cout << "[System] Counter-Drone-System is stopped...\n";
    return 0;
}