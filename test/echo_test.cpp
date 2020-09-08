// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"
using namespace lora;

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

    const int channel = 0x17;

    if (!waveshare.Initialize(channel, (uint16_t)id)) {
        cerr << "Failed to initialize";
        return -1;
    }

    signal(SIGINT, SignalHandler);

    uint64_t t0 = GetTimeUsec();

    cout << "Listening...";

    while (!Terminated)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - t0;

        if (dt > 500) {
            uint8_t data[kPacketMaxBytes] = {
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10
            };
            waveshare.Send(data, 10);
            t0 = t1;
        }

        uint8_t buffer[kPacketMaxBytes];
        int bytes = waveshare.Receive(buffer, kPacketMaxBytes);
        if (bytes < 0) {
            cerr << "Link broken" << endl;
            break;
        }
        if (bytes > 0) {
            cout << "Got bytes:";
            for (int i = 0; i < bytes; ++i) {
                cout << " " << (int)buffer[i];
            }
            cout << endl;
        }
    }

    cout << "Clean shutdown" << endl;
    return 0;
}
