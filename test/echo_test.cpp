// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"
using namespace lora;

#include <unistd.h>

#include <iostream>
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
    if (argc != 2) {
        cerr << "Must provide an argument: The ID for this device" << endl;
        return -1;
    }
    const int id = atoi(argv[1]);

    Waveshare waveshare;

    const int channel = 0;

    // Note: LBT disabled because it appears to be too sensitive to noise
    // and constantly waits 2 seconds before bursting out a few messages.
    // Seems broken.
    if (!waveshare.Initialize(channel, (uint16_t)id, false/*LBT*/)) {
        cerr << "Failed to initialize" << endl;
        return -1;
    }

    signal(SIGINT, SignalHandler);

    uint64_t t0 = GetTimeMsec();

    cout << "Listening..." << endl;

    uint32_t counter = 0;

    while (!Terminated)
    {
        usleep(2000); // 2 msec

        const int send_interval_msec = 50;

        const int packet_bytes = 200;

        uint64_t t1 = GetTimeMsec();
        int64_t dt = t1 - t0;
        if (id != -1 && dt > send_interval_msec && waveshare.GetSendQueueBytes() == 0) {
            uint8_t data[packet_bytes] = {
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10
            };
            *(uint32_t*)data = counter++;
            if (!waveshare.Send(data, packet_bytes)) {
                cerr << "waveshare.Send failed" << endl;
                return -1;
            }
            cout << "Sent: Ping SendQueueBytes=" << waveshare.GetSendQueueBytes() << endl;
            t0 = t1;
        }

        uint8_t buffer[packet_bytes];
        int bytes = waveshare.Receive(buffer, packet_bytes, packet_bytes);
        if (bytes < 0) {
            cerr << "Link broken" << endl;
            break;
        }
        if (bytes > 0) {
            cout << "Got bytes:";
            for (int i = 0; i < bytes; ++i) {
                cout << " " << (int)buffer[i];
            }
            //cout << " at RSSI = " << buffer[bytes - 1] * 0.5f << " dBm" << endl;
            cout << endl;
        }
    }

    cout << "Clean shutdown" << endl;
    return 0;
}
