// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include "waveshare.hpp"

#include "wirehair.h" // wirehair subproject

#include <atomic>
#include <thread>
#include <memory>
#include <vector>

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
    WirehairCodec Decoder = nullptr;

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
    WirehairCodec Encoder = nullptr;

    std::atomic<bool> Terminated = ATOMIC_VAR_INIT(false);
    std::shared_ptr<std::thread> Thread;

    std::vector<uint8_t> CompressedFile;
    size_t CompressedFileBytes = 0;

    void Loop();

    bool BackchannelCheck();
};


} // namespace lora
