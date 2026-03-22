#pragma once
#include <cstdint>
#include <string>

struct TelemetryData
{
    std::string drone_id;
    double latitude;
    double longitude;
    double altitude;
    double speed;
    uint64_t timestamp;

    TelemetryData(){}
    TelemetryData(std::string id, double lat, double lon, double alt, double spd, uint64_t ts)
        : drone_id(id), latitude(lat), longitude(lon), altitude(alt), speed(spd), timestamp(ts) {}
};

struct TelemetryPacket
{
    uint16_t header;
    uint16_t payload_length;
    TelemetryData payload;
    uint16_t crc16;

    TelemetryPacket() : header(0), payload_length(0), crc16(0) {}
    TelemetryPacket(uint16_t h, uint16_t l, const TelemetryData& d, uint16_t crc)
        : header(h), payload_length(l), payload(d), crc16(crc) {}
};

inline uint8_t HEADER_SIZE_BYTES = 2;
inline uint8_t TYPE_SIZE_BYTES = 1;
inline uint8_t LENGTH_SIZE_BYTES = 2;
inline uint8_t CRC_SIZE_BYTES = 2;
inline uint8_t TYPE_STARTING_IDX = HEADER_SIZE_BYTES;
inline uint8_t LENGTH_STARTING_IDX = HEADER_SIZE_BYTES + TYPE_SIZE_BYTES;
inline std::vector<uint8_t> HEADER_BYTES = {0xAA, 0x55};