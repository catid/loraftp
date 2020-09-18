// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include "wireshare.hpp"

#include "wirehair.h" // wirehair subproject

#include <atomic>
#include <thread>
#include <memory>

namespace lora {


//------------------------------------------------------------------------------
// Server

class FileServer
{
public:
    ~FileServer()
    {
        Shutdown();
    }
    bool Initialize();
    void Shutdown();

    bool IsTerminated() const
    {
        return Terminated;
    }

protected:
    Waveshare Uplink;

    std::atomic<bool> Terminated = ATOMIC_VAR_INIT(false);
    std::shared_ptr<std::thread> Thread;

    void Loop();
};


//------------------------------------------------------------------------------
// Client

class FileClient
{
public:
    ~FileClient()
    {
        Shutdown();
    }
    bool Initialize(const char* filepath);
    void Shutdown();

    bool IsTerminated() const
    {
        return Terminated;
    }

protected:
    Waveshare Uplink;

    std::atomic<bool> Terminated = ATOMIC_VAR_INIT(false);
    std::shared_ptr<std::thread> Thread;

    std::vector<uint8_t> CompressedFile;

    void Loop();
};


} // namespace lora
