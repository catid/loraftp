// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "loraftp.hpp"

#include "zstd.h" // zstd_lib subproject

#include <cstring>
#include <cassert>
#include <sstream>
using namespace std;

#include <unistd.h> // usleep

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const int kRendezvousChannel = 42;

static const uint16_t kSenderAddr = 1;

static const int kZstdCompressLevel = 1;

// We need size + crc + offset header on each frame, eating into the 240 max.
static const int kBlockBytes = 234;


//------------------------------------------------------------------------------
// FileReceiver

bool FileReceiver::Initialize(OnReceiveProgress on_recv)
{
    Shutdown();

    OnRecv = on_recv;

    if (!Uplink.Initialize(kRendezvousChannel, kMonitorAddress)) {
        spdlog::error("Uplink.Initialize failed");
        return false;
    }

    Terminated = false;
    Thread = std::make_shared<std::thread>(&FileReceiver::Loop, this);
    return true;
}

void FileReceiver::Shutdown()
{
    Uplink.Shutdown();

    wirehair_free(Decoder);
    Decoder = nullptr;
}

bool FileReceiver::InitDecoder(uint32_t file_bytes)
{
    Decoder = wirehair_decoder_create(Decoder, file_bytes, kFileBlockBytes);
    if (!Decoder) {
        spdlog::error("wirehair_decoder_create failed");
        return false;
    }
    return true;
}

void FileReceiver::OnFileInfo(uint32_t file_bytes, uint32_t hash, uint32_t next_block_id, uint32_t decompressed_bytes)
{
    if (file_bytes <= 0 || decompressed_bytes < 2) {
        spdlog::warn("Ignored invalid file info");
        return;
    }

    NextBlockId = next_block_id;

    // If file changed mid-transmit:
    if (FileBytes != file_bytes || FileHash != hash || DecompressedBytes != decompressed_bytes)
    {
        TransferComplete = false;

        spdlog::info("Detected new file transfer starting [{} bytes]", file_bytes);
        if (InitDecoder(file_bytes)) {
            FileBytes = file_bytes;
            FileHash = hash;
            DecompressedBytes = decompressed_bytes;
        }

        TotalBlockCount = (FileBytes + kFileBlockBytes - 1) / kFileBlockBytes;
        FileBlockCount = 0;

        OnRecv(0.f, nullptr, nullptr, 0);

        for (auto& block : BufferedBlocks) {
            OnBlock(block[0], block.data() + 1, kFileBlockBytes);
        }

        BufferedBlocks.clear();
    }
}

void FileReceiver::OnBlock(uint8_t truncated_id, const void* data, int bytes)
{
    if (TransferComplete) {
        return; // Ignore more data
    }

    // If we haven't gotten any file data yet:
    if (FileBytes == 0) {
        spdlog::debug("Buffering a block");
        std::vector<uint8_t> temp(1 + bytes);
        temp[0] = truncated_id;
        memcpy(temp.data() + 1, data, bytes);
        BufferedBlocks.push_back(temp);
        return;
    }

    NextBlockId = Counter32::ExpandFromTruncated(NextBlockId, truncated_id);

    WirehairResult r = wirehair_decode(Decoder, NextBlockId.ToUnsigned(), data + 1, bytes - 1);
    if (r == Wirehair_NeedMore) {
        ++FileBlockCount;
        const float progress = FileBlockCount / (float)TotalBlockCount;
        OnRecv(progress, nullptr, nullptr, 0);
        return;
    }

    // Point of no return for this file
    TransferComplete = true;

    if (r != Wirehair_Success) {
        spdlog::error("wirehair_decode failed: {}", wirehair_result_string(r));
        return;
    }

    spdlog::info("File transfer complete!  Recovering...");

    uint64_t t0 = GetTimeUsec();

    FileData.resize(FileBytes);
    r = wirehair_recover(Decoder, FileData.data(), FileData.size());
    if (r != Wirehair_Success) {
        spdlog::error("wirehair_recover failed: {}", wirehair_result_string(r));
        return;
    }

    uint64_t t1 = GetTimeUsec();

    spdlog::debug("Recovery complete in {} msec.  Decompressing...", (t1 - t0) / 1000.f);

    DecompressedData.resize(DecompressedBytes);
    size_t decompress_result = ZSTD_decompress(
        DecompressedData.data(), DecompressedBytes,
        FileData.data(), FileData.size());
    if (decompress_result != DecompressedBytes) {
        spdlog::error("ZSTD_decompress failed: {}", ZSTD_getErrorName(decompress_result));
        return;
    }

    uint64_t t2 = GetTimeUsec();

    spdlog::debug("Decompression complete in {} msec", (t2 - t1) / 1000.f);

    const int file_name_bytes = DecompressedData[0];
    const int header_bytes = 1 + file_name_bytes + 1;
    if (header_bytes > DecompressedBytes) {
        spdlog::error("Malformed decompressed data");
        return;
    }

    // Enforce null-terminated string
    DecompressedData[1 + file_name_bytes] = '\0';

    const char* file_name = (const char*)&DecompressedData[1];

    const uint8_t* file_data = DecompressedData.data() + header_bytes;
    const int file_bytes = DecompressedBytes - header_bytes;

    OnRecv(1.f, file_name, file_data, file_bytes);
}

void FileReceiver::Loop()
{
    spdlog::debug("FileReceiver::Loop started");

    uint64_t LastReceiveUsec = 0;

    while (!Terminated)
    {
        if (!Uplink.Receive([&](const uint8_t* data, int bytes)
        {
            /*
                To decode the file we need to know its total length ahead of time.
                Otherwise we just need a truncated 8-bit block identifier on each block.

                Occasionally the sender will send the length and file hash and full
                32-bit block identifier.
                We can buffer up data for a while until this is received.
            */

            if (bytes == 4 + 4 + 4 + 4) {
                OnFileInfo(ReadU32_LE(data), ReadU32_LE(data + 4), ReadU32_LE(data + 8), ReadU32_LE(data + 12));
            } else if (bytes == kPacketMaxBytes) {
                OnBlock(data[0], data + 1, bytes - 1);
            }

            LastReceiveUsec = GetTimeUsec();
        })) {
            spdlog::error("Receive loop failed");
            break;
        }

        const int64_t dt = GetTimeUsec() - LastReceiveUsec;
        const int64_t timeout_usec = 20 * 1000 * 1000;
        if (dt > timeout_usec) {
            if (FileBytes != 0) {
                spdlog::info("Timeout while receiving file from sender.  Resetting and waiting for next file...");
                FileBytes = 0;
                FileHash = 0;
                NextBlockId = 0;
                BufferedBlocks.clear();
            }
        }

        usleep(4000);
    }

    spdlog::debug("FileReceiver::Loop stopped");
}


//------------------------------------------------------------------------------
// FileSender

bool FileSender::Initialize(const char* filepath, const uint8_t* file_data, int file_bytes)
{
    const char* last_slash0 = strrchr(filepath, '/');
    const char* last_slash1 = strrchr(filepath, '\\');
    const char* last_slash = last_slash0;
    if (last_slash < last_slash1) {
        last_slash = last_slash1;
    }
    if (last_slash) {
        Filename = last_slash + 1;
    } else {
        Filename = filepath;
    }

    const size_t file_bound = ZSTD_compressBound(file_bytes);
    CompressedFile.resize(file_bound);

    CompressedFileBytes = ZSTD_compress(
        CompressedFile.data(), file_bound,
        file_data, file_bytes, kZstdCompressLevel);

    if (ZSTD_isError(CompressedFileBytes)) {
        spdlog::error("Zstd failed: {} file_bytes={}", ZSTD_getErrorName(CompressedFileBytes), file_bytes);
        return false;
    }

    WirehairResult wr = wirehair_init();
    if (wr != Wirehair_Success) {
        spdlog::error("wirehair_init failed: {}", wirehair_result_string(wr));
        return false;
    }

    Encoder = wirehair_encoder_create(Encoder, CompressedFile.data(), CompressedFileBytes, kBlockBytes);
    if (!Encoder) {
        spdlog::error("wirehair_encoder_create failed: File size may be too large!");
        return false;
    }

    spdlog::info("Compressed {} to {} bytes.  Starting LoRa uplink...", filepath, CompressedFileBytes);

    if (!Uplink.Initialize(kRendezvousChannel, kServerAddr)) {
        spdlog::error("Uplink.Initialize failed");
        return false;
    }

    spdlog::info("Connecting to server...");

    Terminated = false;
    Thread = std::make_shared<std::thread>(&FileSender::Loop, this);
    return true;
}

void FileSender::Shutdown()
{
    Terminated = true;
    JoinThread(Thread);

    Uplink.Shutdown();

    wirehair_free(Encoder);
    Encoder = nullptr;
}

void FileSender::Loop()
{
    spdlog::debug("FileSender::Loop started");

    ScopedFunction term_scope([&]() {
        // All function exit conditions flag terminated
        Terminated = true;
    });

    int selected_channel = 0;
    if (!MakeOffer(selected_channel)) {
        spdlog::error("Server unreachable");
        return;
    }

    uint64_t last_backchannel_usec = GetTimeUsec();

    if (!Uplink.SetChannel(selected_channel)) {
        spdlog::error("Failed to set channel");
        return;
    }

    uint8_t block[1 + kBlockBytes];
    unsigned block_id = 0;
    uint32_t block_bytes = 0;

    WirehairResult wr = wirehair_encode(Encoder, block_id, block + 1, (uint32_t)kBlockBytes, &block_bytes);
    if (wr != Wirehair_Success) {
        spdlog::error("wirehair_encode failed: {}", wirehair_result_string(wr));
        return;
    }

    uint64_t t0 = 0;

    while (!Terminated)
    {
        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - last_backchannel_usec;
        if (dt > kBackchannelIntervalUsec) {
            if (!BackchannelCheck()) {
                spdlog::error("BackchannelCheck failed");
                break;
            }
        }

        if (PercentageComplete >= 100) {
            spdlog::info("Transfer completed successfully");
            break;
        }

        // Send another block
        dt = t1 - t0;
        const int64_t send_interval_usec = 100 * 1000;
        if (dt > send_interval_usec) {
            block[0] = (uint8_t)block_id;

            if (!Uplink.Send(block, 1 + block_bytes)) {
                spdlog::error("Uplink.Send failed");
                break;
            }

            block_id++;

            WirehairResult wr = wirehair_encode(Encoder, block_id, block, (uint32_t)kBlockBytes, &block_bytes);
            if (wr != Wirehair_Success) {
                spdlog::error("wirehair_encode failed: {}", wirehair_result_string(wr));
                break;
            }
        }

        usleep(4000);
    }

    spdlog::debug("FileSender::Loop ended");
}

bool FileSender::MakeOffer(int& selected_channel)
{
    const uint64_t t0 = GetTimeUsec();
    bool got_ack = false;

    while (!Terminated)
    {
        uint8_t offer[235] = {
            0x00, 0xfe, 0xad, 0x01,
        };
        memcpy(offer + 4, Uplink.ChannelRssiRaw, 4);
        WriteU32_LE(offer + 8, (uint32_t)CompressedFileBytes);
        assert(CompressedFileBytes <= 0xffffffff);
        assert(Filename.length() < 235 - 4 - 4 - 4 - 1);
        offer[12] = (uint8_t)Filename.length();
        memcpy(offer + 13, Filename.c_str(), Filename.length());

        Uplink.Send(offer, 4 + 4 + 4 + 1 + Filename.length());

        // We need to wait for the send to complete before going into receive mode or it will not send
        usleep(500 * 1000);

        // Wait for response:
        const uint64_t wait0 = GetTimeUsec();
        while (!Terminated)
        {
            usleep(10 * 1000); // 10 msec

            // Process incoming data from server
            if (!Uplink.Receive([&](const uint8_t* data, int bytes) {
                if (bytes != 2 || data[0] != 3) {
                    spdlog::error("Invalid data received from server: bytes={} type={}", bytes, (int)data[0]);
                    Terminated = true;
                } else {
                    got_ack = true;
                }
            })) {
                spdlog::error("Receive loop failed");
                return false;
            }

            if (got_ack) {
                spdlog::info("Server acknowledged transmission request");
                return true;
            }

            // Wait a quarter second to hear a response back
            const uint64_t wait1 = GetTimeUsec();
            const int64_t dt = wait1 - wait0;
            const int64_t response_timeout_usec = 250 * 1000;
            if (dt >= response_timeout_usec) {
                // Send again or give up
                break;
            }
        }

        // Timeout?
        const uint64_t t1 = GetTimeUsec();
        const int64_t dt = t1 - t0;
        const int64_t backchannel_timeout_usec = 15 * 1000 * 1000;
        if (dt > backchannel_timeout_usec) {
            spdlog::error("Peer disconnected (timeout)");
            return false;
        }
    }

    spdlog::warn("Aborted offer");
    return false;
}

bool FileSender::BackchannelCheck()
{
    uint64_t t0 = GetTimeUsec();
    bool got_ack = false;

    while (!Terminated)
    {
        // Process incoming data from server
        if (!Uplink.Receive([&](const uint8_t* data, int bytes) {
            if (bytes != 2 || data[0] != 3) {
                spdlog::error("Invalid data received from server: bytes={} type={}", bytes, (int)data[0]);
                Terminated = true;
            } else {
                PercentageComplete = data[1];
                got_ack = true;
            }
        })) {
            spdlog::error("Receive loop failed");
            return false;
        }

        if (got_ack) {
            spdlog::error("Server received: {}%", PercentageComplete);
            return true;
        }

        // Timeout?
        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - t0;
        const int64_t backchannel_timeout_usec = 15 * 1000 * 1000;
        if (dt > backchannel_timeout_usec) {
            spdlog::error("Peer disconnected (timeout)");
            return false;
        }

        usleep(4000);
    }

    return true;
}


} // namespace lora
