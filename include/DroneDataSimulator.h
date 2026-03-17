#include <cstdint>
#include <vector>
#include <atomic>
#include <random>
#include "TelemetryPacket.h"
#include "Generals.h"

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp

class DroneDataSimulator
{
private:
    std::mt19937 m_gen;
    const int DRONE_NUM{1};
    const int CORRUPTION_PERCENTS{10};

    void generate_valid_telemetry_data(const int drone_num, TelemetryData& data);
    uint16_t calculate_crc16(const std::vector<uint8_t>& data);
    BytesArray serialize_telemetry_data_to_bytes_sequence(const TelemetryData& data);
    BytesArray build_telemetry_packet(const TelemetryData& data);
    bool statistic_packet_corruption(BytesArray& packet, int corruption_percentage);

public:
    std::vector<BytesArray> valid_packets_sent;

    explicit DroneDataSimulator();
    void process_loop();
};