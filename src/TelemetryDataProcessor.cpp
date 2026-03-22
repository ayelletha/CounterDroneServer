#include "TelemetryDataProcessor.h"
#include "Configuration.h"
#include "Generals.h"
#include <iostream>

TelemetryDataProcessor::TelemetryDataProcessor(ThreadsSharedDataManager<TelemetryData>& manager)
    : m_shared_telemetry_packets_manager(manager)
{
}

const std::map<std::string, std::pair<bool, TelemetryData>>& TelemetryDataProcessor::drones_status()
{
    return m_drones_status;
}

void TelemetryDataProcessor::process_loop()
{
    /* This thread handles the following requirements :
        - Maintain in-memory table of active drones
        - Update drone state on valid packet
        - Print alert if altitude > 120m or speed > 50 m/s
    */
    while (g_keep_running_system)
    {
        TelemetryData valid_telemetry_data;
        // Try to pop new telemetry data from some drone, but with a timeout, to avoid forever-stuck if the communication is totally crashed and no drone sends msgs for a log long time!!
        if (m_shared_telemetry_packets_manager.pop_data_with_timeout(valid_telemetry_data, DRONE_ACTIVATION_TIMEOUT_SEC))
        {
            std::string drone_id = valid_telemetry_data.drone_id;

            // Update the status for the owner drone of the current (single) packet
            if (LOG_LEVEL & LogLevel::DEBUG_VALID_PACKETS_BUSSINES_LOGIC)
            {
                auto it = m_drones_status.find(drone_id);
                if ((it != m_drones_status.end()) && (it->second.first == false))
                    std::cout << "[TelemetryDataProcessor] Drone " << drone_id << " became ACTIVE again!!\n";
            }
            m_drones_status[drone_id] = std::make_pair(true, valid_telemetry_data); // update it if exist, or add newmap's entry if not exist

            // Print alert if current drone's telemetry is dangerous
            if (valid_telemetry_data.altitude > 120)
                std::cout << "[TelemetryDataProcessor] [WARNING ALERT] The altitude of drone " << drone_id << " is " << valid_telemetry_data.altitude << " > 120 m\n";
            if (valid_telemetry_data.speed > 50)
                std::cout << "[TelemetryDataProcessor] [WARNING ALERT] The speed of drone " << drone_id << " is " << valid_telemetry_data.speed << " > 50 m/s\n";
        }

        // Whether a new package was received or not - upadte the active status of all other drones, using the pre-defined activation timeout
        uint64_t current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        for (auto& pair : m_drones_status)
        {
            auto time_passed_since_last_msg_of_current_drone = (current_time_ms - pair.second.second.timestamp);
            // Check if the time difference exceeds the allowed timeout (multiplying by 1000 because the packet's timestamp is in milliseconds)
            if (time_passed_since_last_msg_of_current_drone > (DRONE_ACTIVATION_TIMEOUT_SEC * 1000))
            {
                if (pair.second.first && (LOG_LEVEL & LogLevel::DEBUG_VALID_PACKETS_BUSSINES_LOGIC))
                    std::cout << "[TelemetryDataProcessor] Drone " << pair.first << " is now IN-ACTIVE (" << time_passed_since_last_msg_of_current_drone << " milliseconds passed since last msg).\n";
                pair.second.first = false;
            }
            else
            {
                // Ensure drone is marked active if it recently sent data (the case that an inactive drone reconnects and sends a fresh timestamp)
                pair.second.first = true; 
            }
        }
    }
}