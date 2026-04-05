#include "DroneDataSimulator.h"
#include "Configuration.h"
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

const std::vector<BytesArray>& DroneDataSimulator::sent_valid_packets() { return m_sent_valid_packets; }
int DroneDataSimulator::fragmented_packets_amount() { return m_fragmented_packets_count; }
int DroneDataSimulator::corrupted_packets_amount() { return m_corrupted_packets_count; }
int DroneDataSimulator::garbage_sequences_amount() { return m_garbage_sequences_count; }

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
    
    // Append the packet's Header (Big-Endian network order)
    packet.push_back(HEADER_BYTES[0]);
    packet.push_back(HEADER_BYTES[1]);

    packet.push_back(static_cast<uint8_t>(TypeMsg::TELEMETRY)); // Telemetry

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

void DroneDataSimulator::generate_valid_telemetry_data(TelemetryData& data)
{
    std::uniform_int_distribution<uint16_t> drone_num_dist(1, 5);
    uint16_t drone_num = drone_num_dist(m_gen);
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
        // When the corrupting is at the third/forth bytes of the packet (i.e. at the length)
        //     - it may cause to a severe(!!) loss of synchronization with packet boundaries.
        // Otherwise, it simulates the scenario of corruption in the payload's data & CRC ...
        std::uniform_int_distribution<size_t> byte_dist(3, packet.size() - 1);
        size_t target_byte = byte_dist(m_gen);
        
        std::uniform_int_distribution<> bit_dist(0, 7);
        packet[target_byte] ^= (1 << bit_dist(m_gen)); 

        return true;
    }
    return false;
}

void DroneDataSimulator::process_loop()
{
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

    int delay_ms = 0;
    int batch_size = 1;
    if (PKGS_RATE <= 1000.0) 
    {
        // Slow rate: Send 1 packet, sleep for X ms
        delay_ms = static_cast<int>(1000.0 / PKGS_RATE);
        batch_size = 1;
    } 
    else 
    {
        // High rate: Send X packets, sleep for 1 ms
        delay_ms = 1;
        batch_size = static_cast<int>(PKGS_RATE / 1000.0);
    }

    int sent_packets_amount = 0;
    std::uniform_int_distribution<int> scenario_dist(0, 4);
    while (g_keep_running_system && (MAX_PKGS_AMOUNT == -1 || sent_packets_amount < MAX_PKGS_AMOUNT))
    {
        for (int i = 0; i < batch_size && (MAX_PKGS_AMOUNT == -1 || sent_packets_amount < MAX_PKGS_AMOUNT); )
        {
            TelemetryData random_data;
            int scenario = scenario_dist(m_gen);
            if (scenario == 4 && (MAX_PKGS_AMOUNT - sent_packets_amount) == 1)
                scenario = 1;
            switch (scenario)
            {
            case 0:
            {
                // Generate & Send a complete valid telemetry data
                generate_valid_telemetry_data(random_data);
                BytesArray packet_to_send = build_telemetry_packet(random_data);
                send(sock, packet_to_send.data(), packet_to_send.size(), 0);
                m_sent_valid_packets.push_back(packet_to_send);
                if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                {
                    std::cout << "[DroneDataSimulator] Send a full valid packet : ";
                    print_bytes_array_c_style(packet_to_send);
                }

                sent_packets_amount++;
                i++;

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
                
                if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                {
                    std::cout << "[DroneDataSimulator] Send " << garbage_len << " bytes of garbage : ";
                    print_bytes_array_c_style(garbage_packet);
                }

                m_garbage_sequences_count++;

                break;
            }
            case 2:
            {
                // Generate a valid random packet and send it fragmented
                generate_valid_telemetry_data(random_data);
                BytesArray packet_to_send = build_telemetry_packet(random_data);
                std::uniform_int_distribution<size_t> split_dist(1, packet_to_send.size() - 1);
                size_t split_idx = split_dist(m_gen);
                if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                {
                    std::cout << "[DroneDataSimulator] Send a valid packet, fragmented to " << split_idx << " & " << (packet_to_send.size() - split_idx) << " bytes : ";
                    print_bytes_array_c_style(packet_to_send);
                }
                // send the first part, wait a bit, then send the rest
                send(sock, packet_to_send.data(), split_idx, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                send(sock, packet_to_send.data() + split_idx, packet_to_send.size() - split_idx, 0);
                
                m_sent_valid_packets.push_back(packet_to_send);
                sent_packets_amount++;
                i++;
                m_fragmented_packets_count++;

                break;
            }
            case 3:
            {
                // Generate a valid random packet, but corrupt its payload data, to simulate cases of incorrent CRC
                generate_valid_telemetry_data(random_data);
                BytesArray packet_to_send = build_telemetry_packet(random_data);
                bool corrupted = statistic_packet_corruption(packet_to_send, 50);
                if (!corrupted)
                {
                    m_sent_valid_packets.push_back(packet_to_send);
                    if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                    {
                        std::cout << "[DroneDataSimulator] Send a full valid packet : ";
                        print_bytes_array_c_style(packet_to_send);
                    }
                }
                else
                {
                    m_corrupted_packets_count++;
                    if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                    {   
                        std::cout << "[DroneDataSimulator] Send a packet with corrupted payload : ";
                        print_bytes_array_c_style(packet_to_send);
                    }
                }
                    
                send(sock, packet_to_send.data(), packet_to_send.size(), 0);
                sent_packets_amount++;
                i++;

                break;
            }
            case 4:
            {
                // Generate a sequence of multiple packets arriving in a single buffer (without a delay between them)
                std::uniform_int_distribution<int> amount_dist(2, std::min(5, MAX_PKGS_AMOUNT-sent_packets_amount+1));
                int amount = amount_dist(m_gen);
                if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                    std::cout << "[DroneDataSimulator] Send sequence of " << amount << " full valid packets : ";
                std::vector<BytesArray> packets_to_send;
                for (int j = 0; j < amount; j++)
                {
                    generate_valid_telemetry_data(random_data);
                    BytesArray packet = build_telemetry_packet(random_data);
                    packets_to_send.push_back(build_telemetry_packet(random_data));
                    if ((LOG_LEVEL & LogLevel::DEBUG_SIMULATOR) && (sent_packets_amount % 10 == 0))
                        print_bytes_array_c_style(packet);
                }
                
                for (auto packet : packets_to_send)
                {
                    send(sock, packet.data(), packet.size(), 0);
                    sent_packets_amount++;
                    i++;
                    m_sent_valid_packets.push_back(packet);
                }
                
                break;
            }
            default:
                break;
            }
        }

        // Sleep once per batch
        if (delay_ms > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    // Ensure cleanup before finish this current thread
    close(sock);
    
    if (MAX_PKGS_AMOUNT >= 0 && sent_packets_amount >= MAX_PKGS_AMOUNT)
    {
        std::cout << "[DroneDataSimulator] All the " << sent_packets_amount << " required packets were sent!\n";
    }

    std::cout << "[DroneDataSimulator] Terminate thread\n";
}