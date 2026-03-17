#include "DroneDataSimulator.h"

#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>

DroneDataSimulator::DroneDataSimulator()
    : m_gen(std::random_device{}())
{
}

BytesArray DroneDataSimulator::serialize_telemetry_data_to_bytes_sequence(const TelemetryData& t)
{
    BytesArray payload;

    // First telemetry data is the drone_id string, so serialize it firstly.
    // In cpp, string variable is in fact a struct that contains a pointer-to-chars + size + capacity, such that the actual characters of the string are stored on the heap.
    // So, when we serialize a string to be explicit bytes array, we must take those characters and explicitly copy them to our bytes array,
    //      unless, on the other side of the communication, the string's actual data will not be available.
    // Therefore, here we serialize the drone_id string to bytes array such that the first byte is the length (number of characters) and the next bytes are the actual characters!
    payload.push_back(static_cast<uint8_t>(t.drone_id.length()));
    payload.insert(payload.end(), t.drone_id.begin(), t.drone_id.end());

    // Then serialize all the rest of the numeric fields, by their actual size
    auto append_numeric_data = [&payload](const auto& val) {
        // performs this conversion only to refer 'var' as a sequence of bytes, instead of its origin type (as defined at 'TelemetryData' struct).
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
        // append the actual bytes of data of 'var' to the tail of 'payload'.
        // the macro 'sizeof' for refference (like 'val') gives the exact amount of bytes that 'var' take, (rather than the size of the pointer) - for example, 8 bytes for double and uint64_t, and 2 bytes for uint16_t, etc.
        payload.insert(payload.end(), ptr, ptr + sizeof(val));
    };
    append_numeric_data(t.latitude);
    append_numeric_data(t.longitude);
    append_numeric_data(t.altitude);
    append_numeric_data(t.speed);
    append_numeric_data(t.timestamp);

    return payload;
}

BytesArray DroneDataSimulator::build_telemetry_packet(const TelemetryData& data)
{
    BytesArray packet;
    
    // Append the packet's Header 0xAA55 (Big-Endian network order)
    packet.push_back(0xAA);
    packet.push_back(0x55);

    BytesArray payload = serialize_telemetry_data_to_bytes_sequence(data);
    
    // Append the payload's actual length, as uint16_t
    uint16_t payload_size = static_cast<uint16_t>(payload.size());
    packet.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF)); // High byte
    packet.push_back(static_cast<uint8_t>(payload_size & 0xFF));        // Low byte

    // Append the payload data itself
    packet.insert(packet.end(), payload.begin(), payload.end());

    // Append the correct CRC for the entire packet (header & length & payload)
    uint16_t crc = calculate_crc16(packet);
    packet.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));  // High byte
    packet.push_back(static_cast<uint8_t>(crc & 0xFF));         // Low byte

    return packet;
}

void DroneDataSimulator::generate_valid_telemetry_data(const int drone_num, TelemetryData& data)
{
    data.drone_id = "DRN-"+std::to_string(drone_num);

    std::uniform_real_distribution<double> lat_dist(32.0500, 32.1500);
    std::uniform_real_distribution<double> lon_dist(34.7500, 34.8500);
    std::uniform_real_distribution<double> alt_dist(10.0, 300.0);
    std::uniform_real_distribution<double> speed_dist(0.0, 25.0);
    data.latitude = lat_dist(m_gen);
    data.longitude = lon_dist(m_gen);
    data.altitude = alt_dist(m_gen);
    data.speed = speed_dist(m_gen);
    
    data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool DroneDataSimulator::statistic_packet_corruption(BytesArray& packet, int corruption_chance)
{
    std::uniform_int_distribution<> chance_range(1, 100);

    if (chance_range(m_gen) <= corruption_chance)
    {
        // When the corrupting is at the first 4 bytes of the packet (i.e. at the header/length)
        //     - it may cause to a severe(!!) loss of synchronization with packet boundaries.
        // Otherwise, it simulates the scenario of corruption in the payload's data & CRC ...
        std::uniform_int_distribution<size_t> byte_dist(0, packet.size() - 1);
        size_t target_byte = byte_dist(m_gen);
        
        std::uniform_int_distribution<> bit_dist(0, 7);
        packet[target_byte] ^= (1 << bit_dist(m_gen)); 

        return true;
    }
    return false;
}

void DroneDataSimulator::process_loop()
{
    /*
    We can expand this logic to simulate multiple drones by creating multiple threads of this process_loop, each with a different drone_num, 
    but for simplicity we will simulate only one drone here (DRONE_NUM set to 1)
    */

    // Wait a bit for letting 'DroneDataSensor' to start and be ready for accepting connections, before we try to connect to it.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cerr << "[DroneDataSimulator] Socket creation error\n";
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        std::cerr << "[DroneDataSimulator] Invalid address\n";
        close(sock);
        return;
    }

    std::cout << "[DroneDataSimulator] Attempting to connect to Sensor...\n";
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 && g_keep_running_system)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!g_keep_running_system) // handle case of shutdown signal received while trying to connect
    {
        close(sock);
        return;
    }

    std::cout << "[DroneDataSimulator] Connected successfully. Sending telemetry data...\n";

    std::uniform_int_distribution<int> scenario_dist(0, 4);
    while (g_keep_running_system)
    {
        TelemetryData random_data;
        BytesArray packet_to_send;
        int scenario = scenario_dist(m_gen);
        switch (scenario)
        {
        case 0:
        {
            // Generate & Send a complete valid telemetry data
            generate_valid_telemetry_data(DRONE_NUM, random_data);
            packet_to_send = build_telemetry_packet(random_data);
            send(sock, packet_to_send.data(), packet_to_send.size(), 0);
            valid_packets_sent.push_back(packet_to_send);
            if (LOG_LEVEL & LogLevel::DEBUG_NETWORK)
            {
                std::cout << "[DroneDataSimulator] Send a full valid packet : ";
                print_bytes_array_c_style(packet_to_send);
            }
            packet_to_send.clear();
            break;
        }
        case 1:
        {
            // Generate & Send a completely random packet (garbage data) that does not follow the telemetry packet structure, to simulate extreme corruption or noise in the communication channel.
            std::uniform_int_distribution<size_t> len_dist(5, 50);
            size_t garbage_len = len_dist(m_gen);
            BytesArray garbage_packet(garbage_len);
            std::uniform_int_distribution<uint16_t> byte_dist(0, 255); 
            for (size_t i = 0; i < garbage_len; ++i)
                garbage_packet[i] = static_cast<uint8_t>(byte_dist(m_gen)); // "uniform_int_distribution" must get type of minimum 16-bit, so cannot random nmber type of 1-byte. Instead, random 2-bytes integers in range of 1-byte-values (0-255) and convert them to 1 byte
            send(sock, garbage_packet.data(), garbage_packet.size(), 0);
            if (LOG_LEVEL & LogLevel::DEBUG_NETWORK)
            {
                std::cout << "[DroneDataSimulator] Send " << garbage_len << " bytes of garbage : ";
                print_bytes_array_c_style(garbage_packet);
            }
            garbage_packet.clear();
            break;
        }
        case 2:
        {
            // Generate a valid random packet and send it fragmented
            generate_valid_telemetry_data(DRONE_NUM, random_data);
            packet_to_send = build_telemetry_packet(random_data);
            if (packet_to_send.size() < 4) // there is nothing to split, because packet is only header and length (this case should not happen...)
            {
                send(sock, packet_to_send.data(), packet_to_send.size(), 0);
            }
            else
            {
                // choose the splitting point somewhere in the middle of the packet
                std::uniform_int_distribution<size_t> split_dist(1, packet_to_send.size() - 1);
                size_t split_idx = split_dist(m_gen);
                // send the first part, wait a bit, then send the rest
                send(sock, packet_to_send.data(), split_idx, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                send(sock, packet_to_send.data() + split_idx, packet_to_send.size() - split_idx, 0);
                if (LOG_LEVEL & LogLevel::DEBUG_NETWORK)
                {
                    std::cout << "[DroneDataSimulator] Send a valid packet, fragmented to " << split_idx << " & " << (packet_to_send.size() - split_idx) << " bytes : ";
                    print_bytes_array_c_style(packet_to_send);
                }
            }
            valid_packets_sent.push_back(packet_to_send);
            packet_to_send.clear();
            break;
        }
        case 3:
        {
            // Generate a valid random packet, but corrupt its payload data, to simulate cases of incorrent CRC
            generate_valid_telemetry_data(DRONE_NUM, random_data);
            packet_to_send = build_telemetry_packet(random_data);
            bool corrupted = statistic_packet_corruption(packet_to_send, 50);
            send(sock, packet_to_send.data(), packet_to_send.size(), 0);
            if (!corrupted)
            {
                valid_packets_sent.push_back(packet_to_send);
                if (LOG_LEVEL & LogLevel::DEBUG_NETWORK)
                {
                    std::cout << "[DroneDataSimulator] Send a full valid packet : ";
                    print_bytes_array_c_style(packet_to_send);
                }
            }
            else
            {
                if (LOG_LEVEL & LogLevel::DEBUG_NETWORK)
                {   
                    std::cout << "[DroneDataSimulator] Send a packet with corrupted payload : ";
                    print_bytes_array_c_style(packet_to_send);
                }
            }
            packet_to_send.clear();
            break;
        }
        case 4:
        {
            // Generate a sequence of multiple packets arriving in a single buffer (without a delay between them)
            std::uniform_int_distribution<int> amount_dist(2, 5);
            int amount = amount_dist(m_gen);
            if (LOG_LEVEL & LogLevel::DEBUG_NETWORK) std::cout << "[DroneDataSimulator] Send sequence of " << amount << " full valid packets : ";
            for (int i = 0; i < amount; i++)
            {
                generate_valid_telemetry_data(DRONE_NUM, random_data);
                packet_to_send = build_telemetry_packet(random_data);
                send(sock, packet_to_send.data(), packet_to_send.size(), 0);
                valid_packets_sent.push_back(packet_to_send);
                if (LOG_LEVEL & LogLevel::DEBUG_NETWORK) print_bytes_array_c_style(packet_to_send);
                packet_to_send.clear();
            }
            break;
        }
        default:
            break;
        }

        // Waiting to avoid flooding the sensor with packets at too high a rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Ensure cleanup before finish this current thread
    close(sock);
    
    std::cout << "[DroneDataSimulator] Terminate thread\n";
}