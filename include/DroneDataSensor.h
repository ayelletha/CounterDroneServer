#include "ThreadsSharedDataManager.h"
#include <future>
#include "Generals.h"

extern std::atomic<bool> g_keep_running_system; // the same global variable defined in main.cpp
const int MAX_DRONES_CONNECTIONS = 3; // the maximum number of drones that can connect to the server at the same time
constexpr size_t RECV_BUFFER_SIZE = 4096;

class DroneDataSensor
{
private:
    int m_sensor_connections_socket_num = -1;
    std::vector<std::future<void>> m_drones_data_rcv_threads;
    ThreadsSharedDataManager<BytesArray>& m_raw_data_queue; // Pay Attention! this is a REFFERENCE type !

public:
    explicit DroneDataSensor(ThreadsSharedDataManager<BytesArray>& manager);
    ~DroneDataSensor();

    bool init_tcp_communication_pipe();
    void process_loop();
    void listen_for_single_drone(int drone_specific_socket_num);
};