// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "loraftp.hpp"
using namespace lora;

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
    SetupAsyncDiskLog("client.log", false/*enable debug logs?*/);

    spdlog::info("loraftp_client V{} starting...", kVersion);

    if (argc < 2) {
        spdlog::info("Usage: {} <file to send>", argv[0]);
        return -1;
    }

    FileClient client;
    ScopedFunction client_scope([&]() {
        client.Shutdown();
    });

    const char* file_path = argv[1];
    if (!client.Initialize(file_path)) {
        spdlog::error("client.Initialize failed");
        return -1;
    }

    signal(SIGINT, SignalHandler);

    while (!Terminated && !client.IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
