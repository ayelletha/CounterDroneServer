# Counter Drone System

## Overview
This project implements a high-performance, multithreaded TCP server in C++ designed to receive, parse, and monitor real-time telemetry data from a fleet of drones. 
The system is built to handle fragmented network streams, gracefully recover from data corruption using a state-machine parser, and maintain a real-time tracking table of active drones while issuing alerts for irregular behaviors.

## Architecture
* The system follows a strict Producer-Consumer architecture over network traffic.
* The producer is a simulator class that simulates drone-data packets using statistic random corruptions, garbage(noise) generation, splitting packages, and sending multiple packages at once.
* The consumer is a simple sensor class that gets connection requests (in our case, from our simulator), opens aimed socket(s) for these connections, collect all the raw data received on these socket(s) and save it on a thread-safe-shared-queue for the Parser's usage.
* The system also opperates a Parser for extracting valid packages from the raw-data that collected & shared by the consumer, while counting and logging the found corrupted data. 
  All valid packages are saved in a thread-safe-shared-queue for the Processor's usage.
* For implementing the system's business logic - the system opperates a Processor, to proccess the telemetry data received from the dromes, while managing the system's drones' status.

## Key Features
* Byte-Level State Machine: Safely parses continuous and fragmented streams (WAIT_FOR_SYNC -> READ_LENGTH -> READ_PAYLOAD).
* Heuristic Filtering: Drops malformed packets early based on expected payload sizes, preventing memory exhaustion.
* Loss of Sync Recovery: Uses a sliding-window approach upon CRC failure to locate hidden valid packets within corrupted streams.
* Graceful Shutdown: Safe thread termination and resource cleanup using condition variable timeouts and RAII.

## Prerequisites
* Linux/Unix-based OS
* GCC/G++ Compiler with C++17 support
* CMake (Version 3.10 or higher)

## Download, Build and Run Instructions

1. In terminal, create some folder wherever you want, and navigate into it:
$ mkdir folder-name & cd folder-name/

2. Clone the repository there:
$ git clone https://github.com/ayelletha/CounterDroneServer.git

3. The project includes a unified shell script to handle building and executing the server, so you can simply do:
$ cd folder-name/CounterDroneServer/
$ chmod +x run.sh (required only at the first time)
$ ./run.sh
