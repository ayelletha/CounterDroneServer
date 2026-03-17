#pragma once
#include <stdio.h>
#include <cstdint>
#include <vector>

enum LogLevel
{
    PRODUCTION = 0,
    DEBUG_NETWORK = 1,
    DEBUG_PACKETS_FILTERRING = 2,
    DEBUG_VALID_PACKETS_BUSSINES_LOGIC = 4
};
extern int LOG_LEVEL;

typedef std::vector<uint8_t> BytesArray;

inline void print_bytes_array_c_style(const BytesArray& arr)
{
    for (uint8_t byte : arr)
    {
        printf("%02X ", byte);
    }
    printf("\n");
}