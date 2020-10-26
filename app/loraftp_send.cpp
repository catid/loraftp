// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

/*
    Puts the radio into transmit mode.
    Sends data until canceled.

    There is no feedback from the receiver.
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
    SetupAsyncDiskLog("sender.log", false/*enable debug logs?*/);

    spdlog::info("loraftp_send V{} starting...", kVersion);

    if (argc < 2) {
        spdlog::info("Usage: {} <file to send>", argv[0]);
        return -1;
    }

    const char* file_name = argv[1];

    MappedReadOnlySmallFile mmf;
    if (!mmf.Read(file_name)) {
        spdlog::error("Failed to open file: {}", file_name);
        return false;
    }

    FileSender sender;
    ScopedFunction sender_scope([&]() {
        sender.Shutdown();
    });

    if (!sender.Initialize(file_name, mmf.GetData(), mmf.GetDataBytes())) {
        spdlog::error("sender.Initialize failed");
        return -1;
    }

    signal(SIGINT, SignalHandler);

    while (!Terminated && !sender.IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
