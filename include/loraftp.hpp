// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include "waveshare.hpp"
#include "Counter.h"

#include "wirehair.h" // wirehair subproject

#include <atomic>
#include <vector>

namespace lora {


//------------------------------------------------------------------------------
// Constants

// Block size for error correction code
static const int kFileBlockBytes = kPacketMaxBytes - 1; // 1 byte for block id


//------------------------------------------------------------------------------
// FileReceiver

// Progress from 0..1 based on how much data is received.
// The receive is complete when file_name and file_data are not null.
using OnReceiveProgress = std::function<void(float progress, const char* file_name, const void* file_data, int file_bytes)>;

class FileReceiver
{
public:
    ~FileReceiver()
    {
        Shutdown();
    }
    bool Initialize(OnReceiveProgress on_recv);
    void Shutdown();

    bool IsTerminated() const
    {
        return Terminated;
    }

protected:
    OnReceiveProgress OnRecv;

    bool TransferComplete = false;
    uint32_t FileBytes = 0;
    uint32_t DecompressedBytes = 0;
    uint32_t FileHash = 0;
    Counter32 NextBlockId = 0;

    uint32_t TotalBlockCount = 0;
    uint32_t FileBlockCount = 0;

    Waveshare Uplink;
    WirehairCodec Decoder = nullptr;

    std::atomic<bool> Terminated = ATOMIC_VAR_INIT(false);
    std::shared_ptr<std::thread> Thread;

    // Blocks buffered up before we receive the file length and hash
    std::vector<std::vector<uint8_t>> BufferedBlocks;

    std::string Filename;
    std::vector<uint8_t> FileData;
    std::vector<uint8_t> DecompressedData;

    void Loop();
    void OnFileInfo(uint32_t file_bytes, uint32_t hash, uint32_t next_block_id, uint32_t decompressed_bytes);
    void OnBlock(uint8_t truncated_id, const void* data, int bytes);
    bool InitDecoder(uint32_t file_bytes);
};


//------------------------------------------------------------------------------
// FileSender

class FileSender
{
public:
    ~FileSender()
    {
        Shutdown();
    }
    bool Initialize(const char* file_name, const uint8_t* file_data, int file_bytes);
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

    std::string Filename;
    uint32_t FileHash = 0;

    std::vector<uint8_t> CompressedFile;
    size_t CompressedFileBytes = 0;
    uint32_t DecompressedBytes = 0;

    void Loop();
};


} // namespace lora
