#include "SensorDataConsumer.h"
#include "Configuration.h"
#include <cstring>

SensorDataConsumer::SensorDataConsumer(
    ThreadsSharedDataManager<BytesArray>& raw_data_manager, 
    ThreadsSharedDataManager<TelemetryData>& packets_manager) 
    : m_shared_raw_data_manager(raw_data_manager), 
      m_shared_telemetry_packets_manager(packets_manager),
      m_state(ParserState::WAIT_FOR_SYNC), 
      m_expected_payload_length(0),
      m_crc_errors_count(0),
      m_invalid_structure_count(0)
{
}

const std::vector<BytesArray>& SensorDataConsumer::received_valid_packets() { return m_received_valid_packets; }
int SensorDataConsumer::crc_errors_amount() { return m_crc_errors_count; }
int SensorDataConsumer::invalid_structure_amount() { return m_invalid_structure_count; }
int SensorDataConsumer::telemetry_pkt_count() { return m_telemetry_pkt_count; }
int SensorDataConsumer::heart_beat_pkt_count() { return m_heart_beat_pkt_count; }
int SensorDataConsumer::cmd_pkt_count() { return m_cmd_pkt_count; }

void SensorDataConsumer::process_loop()
{
    while (g_keep_running_system)
    {
        // This will hold all the bytes chunks exist in the shared raw data queue
        std::vector<BytesArray> new_data_chunks;
        
        // wait (blocks) until there is some data in m_shared_raw_data_manager, and once it has - pull ALL currently available chunks togather.
        if (m_shared_raw_data_manager.pop_all(new_data_chunks))
        {
            size_t total_new_bytes = 0;

            // Concatenate all chunks into our single accumulated buffer (all bytes, flatted)
            for (auto& chunk : new_data_chunks)
            {
                total_new_bytes += chunk.size();
                m_accumulated_data.insert(m_accumulated_data.end(), chunk.begin(), chunk.end());
            }

            if (LOG_LEVEL & LogLevel::DEBUG_PACKETS_FILTERRING)
            {
                std::cout << "[SensorDataConsumer] Popped " << new_data_chunks.size() 
                          << " chunks, totaling " << total_new_bytes << " bytes of raw data\n";
            }

            // Process the newly batched data through the state machine
            process_accumulated_data();
        }
    }

    // Ensure cleanup & release other threads blocking, before finish this current thread
    m_shared_telemetry_packets_manager.wake_up_all();
    
    std::cout << "[SensorDataConsumer] Terminate thread\n";
}

void SensorDataConsumer::process_telemetry_payload(bool& state_changed, size_t total_packet_size)
{
    // Calculate CRC over Header + Length + Payload
    BytesArray package_data_without_crc(m_accumulated_data.begin(), m_accumulated_data.begin() + total_packet_size - 2);
    uint16_t calculated_crc = calculate_crc16(package_data_without_crc);
    
    // Extract the received CRC from the end of the packet
    uint16_t received_crc = (m_accumulated_data[total_packet_size - 2] << 8) | m_accumulated_data[total_packet_size - 1];

    if (calculated_crc == received_crc)
    {
        // Valid packet! Deserialize and push to the shared_telemetry_packets's queue
        // Extract strictly the bytes belonging to this valid packet for logging
        BytesArray current_valid_packet_bytes(
            m_accumulated_data.begin(), 
            m_accumulated_data.begin() + total_packet_size
        );
        m_received_valid_packets.push_back(current_valid_packet_bytes);
        TelemetryData data = deserialize_payload(m_accumulated_data, 5);
        m_shared_telemetry_packets_manager.push_data(data);
        if (LOG_LEVEL & LogLevel::DEBUG_PACKETS_FILTERRING)
        {
            std::cout << "[SensorDataConsumer] Valid package is found !!\n";
        }
        
        // Erase the processed packet from the buffer to advance to the next one
        m_accumulated_data.erase(m_accumulated_data.begin(), m_accumulated_data.begin() + total_packet_size);
    }
    else
    {
        // CRC mismatch (payload or CRC itself was corrupted)
        m_crc_errors_count++;
        if (LOG_LEVEL & LogLevel::DEBUG_PACKETS_FILTERRING)
        {
            std::cerr << "[SensorDataConsumer] CRC validation failed! Total CRC errors: " 
                    << m_crc_errors_count << ". Resyncing...\n";
        }
        
        // Resynchronization approach: discard the first byte (0xAA) and search again
        m_accumulated_data.erase(m_accumulated_data.begin());
    }

    // Return to sync state for the next packet
    m_state = ParserState::WAIT_FOR_SYNC;
    state_changed = true;
}

void SensorDataConsumer::process_heart_beat_payload(bool& state_changed)
{

}

void SensorDataConsumer::process_command_payload(bool& state_changed)
{

}

void SensorDataConsumer::process_accumulated_data()
{
    if (LOG_LEVEL & LogLevel::DEBUG_PACKETS_FILTERRING)
    {
        std::cout << "[SensorDataConsumer] Start processing this accumulated raw data (" << m_accumulated_data.size() << " bytes): ";
        print_bytes_array_c_style(m_accumulated_data);
    }
    bool state_changed = true;

    // Keep processing as long as we have data and the state machine is advancing
    while (state_changed && !m_accumulated_data.empty())
    {
        state_changed = false; 

        switch (m_state)
        {
            case ParserState::WAIT_FOR_SYNC:
            {
                /*  At this state the parser trying to find a legal header (0xAA55), by passing over the m_accumulated_data byte after byte, starting from the first byte of the sequence.
                    When find, it means that all the bytes before this header are garbage so the parser erases them from the sequence, and change the machine-state to READ_LENGTH. 
                */
                size_t sync_index = 0;
                bool sync_found = false;
                
                // Search for the valid header signature: 0xAA followed by 0x55
                for (; sync_index < m_accumulated_data.size() - 1; ++sync_index)
                {
                    if (m_accumulated_data[sync_index] == HEADER_BYTES[0] && m_accumulated_data[sync_index + 1] == HEADER_BYTES[1])
                    {
                        sync_found = true;
                        break;
                    }
                }

                if (sync_found)
                {
                    // Erase any garbage bytes that arrived before the header
                    if (sync_index > 0)
                        m_accumulated_data.erase(m_accumulated_data.begin(), m_accumulated_data.begin() + sync_index);
                    
                    // Header found, advance to the next state
                    m_state = ParserState::READ_TYPE;
                    state_changed = true; 
                }
                else
                {
                    // No complete header found in the current buffer.
                    // Keep the last byte ONLY if it's 0xAA, as 0x55 might arrive in the next chunk.
                    if (m_accumulated_data.back() == HEADER_BYTES[0])
                        m_accumulated_data.erase(m_accumulated_data.begin(), m_accumulated_data.end() - 1);
                    else
                        m_accumulated_data.clear();
                }
                break;
            }

            case ParserState::READ_TYPE:
            {
                if (m_accumulated_data.size() >= (HEADER_SIZE_BYTES + TYPE_SIZE_BYTES))
                {
                    TypeMsg type_val = static_cast<TypeMsg>(m_accumulated_data[TYPE_STARTING_IDX]);
                    // std::cout << m_accumulated_data[2] << "\n";
                    switch (type_val)
                    {
                    case TypeMsg::TELEMETRY:
                    {
                        m_telemetry_pkt_count++;
                        m_type = TypeMsg::TELEMETRY;
                        m_state = ParserState::READ_LENGTH;
                        state_changed = true;
                        break;
                    }
                    case TypeMsg::HEART_BEAT:
                    {
                        m_heart_beat_pkt_count++;
                        m_type = TypeMsg::HEART_BEAT;
                        m_state = ParserState::READ_LENGTH;
                        state_changed = true;
                        break;
                    }
                    case TypeMsg::COMMAND:
                    {
                        m_cmd_pkt_count++;
                        m_type = TypeMsg::COMMAND;
                        m_state = ParserState::READ_LENGTH;
                        state_changed = true;
                        break;
                    }
                    case TypeMsg::UNKNOW:
                    default:
                    {
                        //std::cout<<
                        m_crc_errors_count++;
                        // Erase only the header, because maybe at current byte (of the expected msg-type) will be a new header
                        m_accumulated_data.erase(m_accumulated_data.begin(), m_accumulated_data.begin()+HEADER_SIZE_BYTES);
                        m_state = ParserState::WAIT_FOR_SYNC;
                        state_changed = true;
                        break;
                    }
                    }
                }
                break;
            }
            
            case ParserState::READ_LENGTH:
            {
                // Wait until we have at enough bytes before and including the length field
                if (m_accumulated_data.size() >= (HEADER_SIZE_BYTES + TYPE_SIZE_BYTES + LENGTH_SIZE_BYTES))
                {
                    // Extract the payload length (Big-Endian network order)
                    m_expected_payload_length = (m_accumulated_data[LENGTH_STARTING_IDX] << 8) | m_accumulated_data[LENGTH_STARTING_IDX + 1];

                    // Heuristic filter: check if the length makes sense for our telemetry packet (between 30 and 80 bytes)
                    if (m_expected_payload_length < MIN_PAYLOAD_EXP_LENGTH || m_expected_payload_length > MAX_PAYLOAD_EXP_LENGTH)
                    {
                        m_invalid_structure_count++;
                        if (LOG_LEVEL & LogLevel::DEBUG_PACKETS_FILTERRING)
                        {
                            std::cerr << "[SensorDataConsumer] Invalid structure detected (Length: " 
                                    << m_expected_payload_length << "). Total structure errors: " 
                                    << m_invalid_structure_count << ". Resyncing...\n";
                        }
                            
                        // Resynchronization approach (Sliding Window): 
                        // Erase only the bytes before the length field, to resume sync search from the next byte
                        m_accumulated_data.erase(m_accumulated_data.begin(), m_accumulated_data.begin() + HEADER_SIZE_BYTES); 
                        m_state = ParserState::WAIT_FOR_SYNC;
                    }
                    else
                    {
                        // Length is valid, proceed to read the actual payload
                        m_state = ParserState::READ_PAYLOAD;
                    }
                    state_changed = true;
                }
                break;
            }

            case ParserState::READ_PAYLOAD:
            {
                // Total expected size: Header(2) + Type(1) + Length(2) + Payload + CRC(2)
                size_t total_packet_size = HEADER_SIZE_BYTES + TYPE_SIZE_BYTES + LENGTH_SIZE_BYTES + m_expected_payload_length + CRC_SIZE_BYTES;

                // If current data is a fragmented packet - then do not process it yet,
                //  but wait to the rest of the packet will be appended to 'm_accumulated_data' at the next chunk
                //  (i.e. waits on call 'm_shared_raw_data_manager.pop_all' at 'process_loop' function).
                // The m_state remains READ_PAYLOAD, so once additional data will arrive - machine tries to execute this case again.
                if (m_accumulated_data.size() >= total_packet_size)
                {
                    switch (m_type)
                    {
                        case TypeMsg::TELEMETRY:
                        {
                            process_telemetry_payload(state_changed, total_packet_size);
                            break;
                        }
                        case TypeMsg::HEART_BEAT:
                        {
                            process_heart_beat_payload(state_changed);
                            break;
                        }
                        case TypeMsg::COMMAND:
                        {
                            process_command_payload(state_changed);
                            break;
                        }
                        default:
                        {
                            std::cout << "[ERROR IN STATE MACHINE LOGIC]\n";
                            break;
                        }
                    }
                }
                else
                {
                    if (LOG_LEVEL & LogLevel::DEBUG_PACKETS_FILTERRING)
                    {
                        std::cout << "[SensorDataConsumer] Current packet is a fragmented, wait to the rest of the packet will arrie at the next chunk\n";
                    }
                }
                break;
            }
        }
    }
}

TelemetryData SensorDataConsumer::deserialize_payload(const BytesArray& buffer, size_t start_idx)
{
    size_t idx = start_idx;

    // Read the drone_id string (first byte is length, followed by characters)
    uint8_t id_len = buffer[idx++];
    std::string drone_id(buffer.begin() + idx, buffer.begin() + idx + id_len);
    idx += id_len;

    // Helper lambda to safely read numeric primitives from the buffer
    auto read_primitive = [&buffer, &idx](auto& val) {
        std::memcpy(&val, &buffer[idx], sizeof(val));
        idx += sizeof(val);
    };

    double lat, lon, alt, spd;
    uint64_t ts;
    
    read_primitive(lat);
    read_primitive(lon);
    read_primitive(alt);
    read_primitive(spd);
    read_primitive(ts);

    return TelemetryData(drone_id, lat, lon, alt, spd, ts);
}
