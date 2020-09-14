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

    if (!waveshare.Initialize(channel, (uint16_t)id, true)) {
        cerr << "Failed to initialize" << endl;
        return -1;
    }

    signal(SIGINT, SignalHandler);

    uint64_t t0 = GetTimeMsec();

    cout << "Listening..." << endl;

    while (!Terminated)
    {
        usleep(100000);

        uint64_t t1 = GetTimeMsec();
        int64_t dt = t1 - t0;
        if (dt > 1000) {
            cout << "Sent: Ping" << endl;
            uint8_t data[kPacketMaxBytes] = {
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10
            };
            waveshare.Send(data, 240);
            t0 = t1;
        }

        uint8_t buffer[kPacketMaxBytes + 1];
        int bytes = waveshare.Receive(buffer, kPacketMaxBytes + 1, 241);
        if (bytes < 0) {
            cerr << "Link broken" << endl;
            break;
        }
        if (bytes > 0) {
            cout << "Got bytes:";
            for (int i = 0; i < bytes - 1; ++i) {
                cout << " " << (int)buffer[i];
            }
            cout << " at RSSI = " << buffer[bytes - 1] * 0.5f << " dBm" << endl;
        }
    }

    cout << "Clean shutdown" << endl;
    return 0;
}
