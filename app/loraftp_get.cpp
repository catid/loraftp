// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

/*
    Puts the radio into monitor mode.
    Receives data until enough is received to complete the transfer.
*/

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
    (void)argc;
    (void)argv;

    SetupAsyncDiskLog("getter.log", false/*enable debug logs?*/);

    spdlog::info("loraftp_get V{} starting...", kVersion);

    FileReceiver receiver;
    ScopedFunction client_scope([&]() {
        receiver.Shutdown();
    });

    if (!receiver.Initialize([&](float progress, const char* file_name, const void* file_data, int file_bytes) {
        if (file_name && file_data) {
            if (!WriteBufferToFile(file_name, file_data, file_bytes)) {
                spdlog::error("Failed to write file: {} [{} bytes]", file_name, file_bytes);
            } else {
                spdlog::info("Completed file transfer: {} [{} bytes]", file_name, file_bytes);
            }
            Terminated = true;
        } else {
            spdlog::info("Progress: {}%", progress * 100.f);
        }
    })) {
        spdlog::error("receiver.Initialize failed");
        return -1;
    }

    signal(SIGINT, SignalHandler);

    spdlog::info("Ready. Waiting for files...");

    while (!Terminated && !receiver.IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
