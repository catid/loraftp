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
    (void)argc;
    (void)argv;

    spdlog::info("loraftp_server V{}", kVersion);

    FileServer server;
    ScopedFunction client_scope([&]() {
        server.Shutdown();
    });

    if (!server.Initialize()) {
        spdlog::error("server.Initialize failed");
        return -1;
    }

    signal(SIGINT, SignalHandler);

    spdlog::info("Waiting for client...");

    while (!Terminated && !server.IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
