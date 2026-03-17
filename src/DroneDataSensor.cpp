#include "DroneDataSensor.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>

DroneDataSensor::DroneDataSensor(ThreadsSharedDataManager<BytesArray>& manager)
    : m_shared_raw_data_manager(manager)
{
}

DroneDataSensor::~DroneDataSensor()
{
    if (m_sensor_connections_socket_num >= 0)
    {
        close(m_sensor_connections_socket_num);
        std::cout << "[DroneDataSensor] Distruct DroneDataSensor: main listening socket " << m_sensor_connections_socket_num << " was closed successfully.\n";
        m_sensor_connections_socket_num = -1;
    }
}

bool DroneDataSensor::init_tcp_communication_pipe()
{
    // Create a new socket for the communication with the drone
    m_sensor_connections_socket_num = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sensor_connections_socket_num == -1)
    {
        std::cerr << "[DroneDataSensor] Error: Cannot create socket\n";
        return false;
    }

    // Set the socket options to avoid "Address already in use" error when restarting the server quickly after stopping it.
    int opt = 1;
    setsockopt(m_sensor_connections_socket_num, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // Set a timeout for listening to new data on the socket, to avoid blocking later, on function "accept", when trying to terminate the server's thread safely.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(m_sensor_connections_socket_num, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    // Bind the socket to a specific port
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    if (bind(m_sensor_connections_socket_num, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        std::cerr << "[DroneDataSensor] Error: Bind failed\n";
        close(m_sensor_connections_socket_num);
        m_sensor_connections_socket_num = -1;
        return false;
    }

    // Define the current thread to start listenning on the socket
    if (listen(m_sensor_connections_socket_num, MAX_DRONES_CONNECTIONS) < 0)
    {
        std::cerr << "[DroneDataSensor] Error: Listen failed\n";
        close(m_sensor_connections_socket_num);
        m_sensor_connections_socket_num = -1;
        return false;
    }

    std::cout << "[DroneDataSensor] Listening for drone connections on port 8080...\n";
    return true;
}

void DroneDataSensor::process_loop()
{
    if (!init_tcp_communication_pipe())
    {
        std::cerr << "[DroneDataSensor] Failed to initialize TCP communication pipe. Stop processing.\n";
        return;
    }

    auto delete_inactive_threads = [&]()
    {
        // Clean threads of drones that were disconnected and their threads were finished, to avoid memory leak and too many "zombee" threads in the system
        for (auto it = m_drones_data_rcv_threads.begin(); it != m_drones_data_rcv_threads.end(); )
        {
            // check if the specific drone's communication thread is finished
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                it = m_drones_data_rcv_threads.erase(it);
            else
                ++it;
        }
    };

    // Loop for establish connection pipe(s) for listening to drone(s)
    while (g_keep_running_system)
    {
        // Wait (blocks) until some drone will connect to the socket defined by m_sensor_connections_socket_num.
        //  * if there is some connected drone, then define a new socket (new_data_socket) and launch a new thread for handling the connection with this drone.
        //  * otherwise, go again to the while loop, for avoiding infinite waiting that blocks the system to terminate if should.
        // Important point: the "accept" function blocks the current thread for maximum half second, since we defined a timeout for the m_sensor_connections_socket_num socket (at function "init_tcp_communication_pipe")
        int new_data_socket = accept(m_sensor_connections_socket_num, nullptr, nullptr);
        if (new_data_socket >= 0)
        {
            std::cout << "[DroneDataSensor] New Drone connected!\n";
            m_drones_data_rcv_threads.push_back(
                std::async(std::launch::async, &DroneDataSensor::listen_on_spesific_socket, this, new_data_socket)
            );
        }

        delete_inactive_threads();
    }
    
    // Ensure cleanup & release other threads blocking, before finish this current thread
    m_shared_raw_data_manager.wake_up_all();
    close(m_sensor_connections_socket_num);
    auto closed_socket_num = m_sensor_connections_socket_num;
    m_sensor_connections_socket_num = -1;

    std::cout << "[DroneDataSensor] Close socket " << closed_socket_num << " of waiting to accept a new connections & Terminate thread\n";
}

void DroneDataSensor::listen_on_spesific_socket(int socket_num)
{
    // Set a timeout for this drone's socket, for avoiding infinite blocking on 'recv' function when should terminate the entire system (what can be triggered by user/system interrupt)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // half second
    setsockopt(socket_num, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)); 
    
    std::array<uint8_t, RECV_BUFFER_SIZE> buffer;

    std::cout << "[DroneDataSensor] Start wait to drone's data on socket " << socket_num << "...\n";

    // Loop for listenning data that receive from a specific drone on the specific port
    while (g_keep_running_system) 
    {
        int bytes_read = recv(socket_num, buffer.data(), buffer.size(), 0);
        if (bytes_read > 0)
        {
            BytesArray data_chunk(buffer.data(), buffer.data() + bytes_read);
            if (LOG_LEVEL & LogLevel::DEBUG_NETWORK)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // TODO Delete this!! only for ordered printings..
                std::cout << "[DroneDataSensor] Receive " << bytes_read << " bytes of data : ";
                print_bytes_array_c_style(data_chunk);
            }
            m_shared_raw_data_manager.push_data(std::move(data_chunk));
        }
    }

    // Ensure safely clean the connection to this client on this specific socket
    close(socket_num);

    std::cout << "[DroneDataSensor] System shut-down event -> Close socket " << socket_num << " of listening to drone's packets & Terminate thread\n";
}
