#pragma once
#include <stdio.h>
#include <cstdint>
#include <vector>

typedef std::vector<uint8_t> BytesArray;

enum LogLevel
{
    PRODUCTION = 0,
    DEBUG_SIMULATOR = 1,
    DEBUG_NETWORK = 2,
    DEBUG_PACKETS_FILTERRING = 4,
    DEBUG_VALID_PACKETS_BUSSINES_LOGIC = 8
};
extern int LOG_LEVEL;

enum class TypeMsg : uint8_t
{
    UNKNOW = 0,
    TELEMETRY = 1,
    HEART_BEAT = 2,
    COMMAND = 4
};

inline void print_bytes_array_c_style(const BytesArray& arr)
{
    for (uint8_t byte : arr)
    {
        printf("%02X ", byte);
    }
    printf("\n");
}

inline uint16_t calculate_crc16(const std::vector<uint8_t>& data)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= (uint16_t)(byte << 8);
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}