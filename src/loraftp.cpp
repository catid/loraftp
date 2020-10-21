// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "loraftp.hpp"

#include "zstd.h" // zstd_lib subproject

#include "Counter.h"

#include <cstring>
#include <cassert>
#include <sstream>
using namespace std;

#include <unistd.h> // usleep

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const int kRendezvousChannel = 42;

static const uint16_t kServerAddr = 0;
static const uint16_t kClientAddr = 1;

static const int kZstdCompressLevel = 1;

// We need size + crc + offset header on each frame, eating into the 240 max.
static const int kBlockBytes = 234;

// Time between switching send/receive roles and checking in on receiver.
static const int64_t kBackchannelIntervalUsec = 5 * 1000 * 1000;


//------------------------------------------------------------------------------
// Server

bool FileServer::Initialize()
{
    if (!Uplink.Initialize(kRendezvousChannel, kServerAddr)) {
        spdlog::error("Uplink.Initialize failed");
        return false;
    }

    Terminated = false;
    Thread = std::make_shared<std::thread>(&FileServer::Loop, this);
    return true;
}

void FileServer::Shutdown()
{
    Uplink.Shutdown();

    wirehair_free(Decoder);
    Decoder = nullptr;
}

void FileServer::Loop()
{
    spdlog::debug("FileServer::Loop started");

    bool in_transfer = false;
    uint64_t last_ambient_scan_usec = GetTimeMsec();

    Counter32 last_block_id = 0;

    while (!Terminated)
    {
        // Periodically rescan for ambient noise power
        if (!in_transfer)
        {
            uint64_t t1 = GetTimeMsec();
            int64_t dt_msec = t1 - last_ambient_scan_usec;
            if (dt_msec > 30 * 1000)
            {
                spdlog::info("RSSI ambient noise scan started...");
                if (!Uplink.ScanAmbientRssi()) {
                    spdlog::error("Uplink.ScanAmbientRssi failed");
                    break;
                }

                std::ostringstream oss;
                oss << "RSSI ambient noise scan completed:";
                for (int i = 0; i < kCheckedChannelCount; ++i) {
                    const int channel = kCheckedChannels[i];
                    oss << " ch" << channel << "=" << Uplink.ChannelRssi[channel];
                }
                oss << " (dBm noise)";
                spdlog::info("{}", oss.str());

                last_ambient_scan_usec = GetTimeMsec();
            }
        }

        if (!Uplink.Receive([&](const uint8_t* data, int bytes) {
            if (in_transfer) {
                if (bytes < 2) {
                    spdlog::warn("Truncated packet: bytes={}", bytes);
                    return;
                }

                Counter8 truncated = data[0];
                Counter32 block_id = Counter32::ExpandFromTruncated(last_block_id, truncated);
                last_block_id = block_id;

                WirehairResult r = wirehair_decode(Decoder, block_id.ToUnsigned(), data + 1, bytes - 1);
                if (r != Wirehair_Success) {
                    Terminated = true;
                    spdlog::error("wirehair_decode failed: {}", wirehair_result_string(r));
                    return;
                }
                if (r == Wirehair_NeedMore) {
                    // More needed
                    return;
                }

                spdlog::info("Enough file data has been received");

                r = wirehair_recover(Decoder, FileData.data(), FileData.size());
                if (r != Wirehair_Success) {
                    spdlog::error("wirehair_recover failed: {}", wirehair_result_string(r));
                    Terminated = true;
                    return;
                }

                if (!WriteBufferToFile(Filename.c_str(), FileData.data(), FileData.size())) {
                    spdlog::error("WriteBufferToFile failed: {}", Filename);
                    Terminated = true;
                    return;
                }

                spdlog::info("File transfer complete.");
                Terminated = true;
                return;
            } else {
                if (bytes < 4 + 4 + 4 + 2) {
                    spdlog::warn("Ignoring truncated LoRa packet: bytes={}", bytes);
                    return;
                }
                if (data[0] != 0 || data[1] != 0xfe || data[2] != 0xad || data[3] != 0x01) {
                    spdlog::warn("Ignoring wrong protocol LoRa packet: bytes={}", bytes);
                    return;
                }
            }
        })) {
            spdlog::error("Receive loop failed");
            break;
        }

        usleep(4000);
    }

    spdlog::debug("FileServer::Loop stopped");
}


//------------------------------------------------------------------------------
// Client

bool FileClient::Initialize(const char* filepath)
{
    MappedReadOnlySmallFile mmf;

    if (!mmf.Read(filepath)) {
        spdlog::error("Failed to open file: {}", filepath);
        return false;
    }

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

    const uint8_t* file_data = mmf.GetData();
    const uint32_t file_bytes = mmf.GetDataBytes();

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
    Thread = std::make_shared<std::thread>(&FileClient::Loop, this);
    return true;
}

void FileClient::Shutdown()
{
    Terminated = true;
    JoinThread(Thread);

    Uplink.Shutdown();

    wirehair_free(Encoder);
    Encoder = nullptr;
}

void FileClient::Loop()
{
    spdlog::debug("FileClient::Loop started");

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

    spdlog::debug("FileClient::Loop ended");
}

bool FileClient::MakeOffer(int& selected_channel)
{
    uint64_t t0 = GetTimeUsec();
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

    spdlog::warn("Aborted offer");
    return false;
}

bool FileClient::BackchannelCheck()
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
