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
    const int DATA_SENDING_FREQUENCY_MS = 1; // send something on the TCP pipe every X milliseconds. For convenience of checking system's action, I set it to send only 1 data chunk every second

    std::mt19937 m_gen;
    
    std::vector<BytesArray> m_sent_valid_packets;
    int m_fragmented_packets_count = 0;
    int m_corrupted_packets_count = 0;
    int m_garbage_sequences_count = 0;

    void generate_valid_telemetry_data(TelemetryData& data);
    BytesArray serialize_telemetry_data_to_bytes_sequence(const TelemetryData& data);
    BytesArray build_telemetry_packet(const TelemetryData& data);
    bool statistic_packet_corruption(BytesArray& packet, int corruption_percentage);

public:

    explicit DroneDataSimulator();
    void process_loop();

    const std::vector<BytesArray>& sent_valid_packets();
    int fragmented_packets_amount();
    int corrupted_packets_amount();
    int garbage_sequences_amount();
};