// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "loraftp.hpp"
using namespace lora;

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
using namespace std;


//------------------------------------------------------------------------------
// Signal

#include <csignal>
#include <atomic>

atomic<bool> Terminated = ATOMIC_VAR_INIT(false);

void SignalHandler(int)
{
    Terminated = true;
}


//------------------------------------------------------------------------------
// Entrypoint

int main(int argc, char* argv[])
{
    cout << "loraftp_client V" << kVersion << endl;

    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <file to send>" << endl;
        return -1;
    }

    FileClient client;
    ScopedFunction client_scope([&]() {
        client.Shutdown();
    });

    const char* file_path = argv[1];
    if (!client.Initialize(file_path)) {
        cerr << "client.Initialize failed" << endl;
        return -1;
    }

    signal(SIGINT, SignalHandler);

    while (!Terminated && !client.IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    cout << "Shutting down..." << endl;
    return 0;
}
