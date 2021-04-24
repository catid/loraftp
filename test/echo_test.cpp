// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

/*
    This stress-tests the basic features of the waveshare C++ library.

    To use the echo_test app:

    On Pi 1 that receives data:

        sudo ./echo_test 2

    On Pi 2 that sends data:

        sudo ./echo_test 0
*/

#include "waveshare.hpp"
using namespace lora;

#include <unistd.h>

#include <sstream>
#include <fstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
using namespace std;


//------------------------------------------------------------------------------
// Entrypoint

atomic<bool> Terminated = ATOMIC_VAR_INIT(false);

void SignalHandler(int)
{
    Terminated = true;
}

int main(int argc, char* argv[])
{
    SetupAsyncDiskLog("echo_test.log", true/*enable debug logs?*/);

    int id;
    if (argc != 2) {
        spdlog::warn("No ID argument provided.  Using ID=-1 for receiver side");
        id = -1;
    } else {
        id = atoi(argv[1]);
    }

    Waveshare waveshare;

    const int channel = 0;

    // Note: LBT disabled because it appears to be too sensitive to noise
    // and constantly waits 2 seconds before bursting out a few messages.
    // Seems broken.
    if (!waveshare.Initialize(channel, (uint16_t)id, false/*LBT*/)) {
        spdlog::error("Failed to initialize");
        return -1;
    }

    signal(SIGINT, SignalHandler);

    uint64_t t0 = GetTimeMsec();

    spdlog::info("Listening...");

    uint32_t counter = 0;

    while (!Terminated)
    {
        usleep(2000); // 2 msec

        const int send_interval_msec = 100;

        const int packet_bytes = kPacketMaxBytes;

        if (id >= 2)
        {
            uint64_t t1 = GetTimeMsec();
            int64_t dt = t1 - t0;
            if (dt > send_interval_msec && waveshare.GetSendQueueBytes() == 0) {
                uint8_t data[packet_bytes] = {
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10
                };
                *(uint32_t*)data = counter++;
                if (!waveshare.Send(data, packet_bytes)) {
                    spdlog::error("waveshare.Send failed");
                    return -1;
                }
                spdlog::info("Sent: Ping SendQueueBytes={}", waveshare.GetSendQueueBytes());
                t0 = t1;
            }
        }
        else
        {
            if (!waveshare.Receive([&](const uint8_t* data, int bytes) {
                std::ostringstream oss;
                oss << "Got bytes:";
                for (int i = 0; i < bytes; ++i) {
                    oss << " " << (int)data[i];
                }
                spdlog::info("{}", oss.str());
            })) {
                spdlog::error("Link broken");
                break;
            }
        }
    }

    return 0;
}
