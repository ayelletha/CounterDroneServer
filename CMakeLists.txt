cmake_minimum_required(VERSION 3.16)
project(CounterDroneServer VERSION 1.0.0 LANGUAGES CXX)

# Define C++17 standard as required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define compiler flags for better warnings (recommended)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Export the Header files to be used by other projects
include_directories(include)

# Create the project's executable file
add_executable(drone_server
    src/main.cpp
    src/DroneDataSimulator.cpp
    src/DroneDataSensor.cpp
    src/SensorDataConsumer.cpp
    src/TelemetryDataProcessor.cpp)

target_include_directories(drone_server PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Support using threads
find_package(Threads REQUIRED)
target_link_libraries(drone_server PRIVATE Threads::Threads)