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
    uint16_t header; // 2 bytes
    uint16_t payload_length; // 2 bytes
    TelemetryData payload;
    uint16_t crc16; // 2 bytes

    TelemetryPacket() : header(0), payload_length(0), crc16(0) {}
    TelemetryPacket(uint16_t h, uint16_t l, const TelemetryData& d, uint16_t crc)
        : header(h), payload_length(l), payload(d), crc16(crc) {}
};
