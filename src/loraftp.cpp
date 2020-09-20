// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "loraftp.hpp"

#include "zstd.h" // zstd_lib subproject
#include "Counter.h"

#include <cstring>
#include <cassert>
#include <iostream>
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
        cerr << "Uplink.Initialize failed" << endl;
        return false;
    }

    Terminated = false;
    Thread = std::make_shared<std::thread>(&FileServer::Loop, this);
    return true;
}

void FileServer::Shutdown()
{
    Uplink.Shutdown();
}

void FileServer::Loop()
{
    cout << "FileServer: Loop started" << endl;

    bool in_transfer = false;
    uint64_t last_ambient_scan_usec = GetTimeMsec();

    Counter32 last_block_id = 0;

    while (!Terminated)
    {
        // Periodically rescan for ambient noise power
        if (!in_transfer) {
            uint64_t t1 = GetTimeMsec();
            int64_t dt_msec = t1 - last_ambient_scan_usec;
            if (dt_msec > 10 * 1000) {
                cout << "Time to re-scan for ambient noise!" << endl;
                if (!Uplink.ScanAmbientRssi()) {
                    cerr << "ScanAmbientRssi failed" << endl;
                    break;
                }
                last_ambient_scan_usec = GetTimeMsec();
            }
        }

        if (!Uplink.Receive([&](const uint8_t* data, int bytes) {
            if (in_transfer) {
                if (bytes < 2) {
                    cerr << "Truncated packet";
                    return;
                }
                Counter8 truncated = data[0];
                Counter32 block_id = Counter32::ExpandFromTruncated(last_block_id, truncated);
                last_block_id = block_id;
            } else {
                // FIXME
            }
        })) {
            cerr << "Receive loop failed" << endl;
            break;
        }

        usleep(4000);
    }

    cout << "FileServer: Loop terminated" << endl;
}


//------------------------------------------------------------------------------
// Client

bool FileClient::Initialize(const char* filepath)
{
    MappedReadOnlySmallFile mmf;

    if (!mmf.Read(filepath)) {
        cerr << "Failed to open file: " << filepath << endl;
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
        cerr << "Zstd failed" << endl;
        return false;
    }

    Encoder = wirehair_encoder_create(Encoder, CompressedFile.data(), CompressedFileBytes, kBlockBytes);
    if (!Encoder) {
        cerr << "wirehair_encoder_create failed" << endl;
        return false;
    }

    if (!Uplink.Initialize(kRendezvousChannel, kServerAddr)) {
        cerr << "Uplink.Initialize failed" << endl;
        return false;
    }

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
    ScopedFunction term_scope([&]() {
        // All function exit conditions flag terminated
        Terminated = true;
    });

    cout << "FileClient: Loop started" << endl;

    int selected_channel = 0;
    if (!MakeOffer(selected_channel)) {
        cerr << "Server unreachable" << endl;
        return;
    }

    uint64_t last_backchannel_usec = GetTimeUsec();

    if (!Uplink.SetChannel(selected_channel)) {
        cerr << "Failed to set channel" << endl;
        return;
    }

    uint8_t block[1 + kBlockBytes];
    unsigned block_id = 0;
    uint32_t block_bytes = 0;

    WirehairResult r = wirehair_encode(Encoder, block_id, block + 1, (uint32_t)kBlockBytes, &block_bytes);
    if (r != Wirehair_Success) {
        cerr << "wirehair_encode failed: r=" << wirehair_result_string(r) << endl;
        return;
    }

    uint64_t t0 = 0;

    while (!Terminated)
    {
        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - last_backchannel_usec;
        if (dt > kBackchannelIntervalUsec) {
            if (!BackchannelCheck()) {
                cerr << "BackchannelCheck failed" << endl;
                break;
            }
        }

        if (PercentageComplete >= 100) {
            cout << "Transfer completed successfully" << endl;
            break;
        }

        // Send another block
        dt = t1 - t0;
        const int64_t send_interval_usec = 100 * 1000;
        if (dt > send_interval_usec) {
            block[0] = (uint8_t)block_id;

            if (!Uplink.Send(block, 1 + block_bytes)) {
                cerr << "Uplink send failed" << endl;
                break;
            }

            block_id++;

            WirehairResult r = wirehair_encode(Encoder, block_id, block, (uint32_t)kBlockBytes, &block_bytes);
            if (r != Wirehair_Success) {
                cerr << "wirehair_encode failed: r=" << wirehair_result_string(r) << endl;
                break;
            }
        }

        usleep(4000);
    }

    cout << "FileClient: Loop terminated" << endl;
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

        cout << "Filename = " << Filename << endl;
        cout << "Length = " << Filename.length() << endl;

        Uplink.Send(offer, 4 + 4 + 4 + 1 + Filename.length());



        // Process incoming data from server
        if (!Uplink.Receive([&](const uint8_t* data, int bytes) {
            if (bytes != 2 || data[0] != 3) {
                cerr << "Invalid data received from server: bytes=" << bytes << " type=" << (int)data[0] << endl;
                Terminated = true;
            } else {
                PercentageComplete = data[1];
                got_ack = true;
            }
        })) {
            cerr << "Receive loop failed" << endl;
            return false;
        }

        if (got_ack) {
            cout << "Server received: " << PercentageComplete << "%" << endl;
            return true;
        }

        // Timeout?
        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - t0;
        const int64_t backchannel_timeout_usec = 2 * 1000 * 1000;
        if (dt > backchannel_timeout_usec) {
            cerr << "*** Peer disconnected (timeout)" << endl;
            return false;
        }

        usleep(4000);
    }

    return true;
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
                cerr << "Invalid data received from server: bytes=" << bytes << " type=" << (int)data[0] << endl;
                Terminated = true;
            } else {
                PercentageComplete = data[1];
                got_ack = true;
            }
        })) {
            cerr << "Receive loop failed" << endl;
            return false;
        }

        if (got_ack) {
            cout << "Server received: " << PercentageComplete << "%" << endl;
            return true;
        }

        // Timeout?
        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - t0;
        const int64_t backchannel_timeout_usec = 2 * 1000 * 1000;
        if (dt > backchannel_timeout_usec) {
            cerr << "*** Peer disconnected (timeout)" << endl;
            return false;
        }

        usleep(4000);
    }

    return true;
}


} // namespace lora
