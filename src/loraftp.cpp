// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "loraftp.hpp"

#include "zstd.h" // zstd_lib subproject

#include <iostream>
using namespace std;

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const int kRendezvousChannel = 42;

static const uint16_t kServerAddr = 0;
static const uint16_t kClientAddr = 1;

static const int kZstdCompressLevel = 1;


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

    const uint8_t* file_data = mmf.GetData();
    const uint32_t file_bytes = mmf.GetDataBytes();

    const size_t file_bound = ZSTD_compressBound(file_bytes);
    CompressedFile.resize(file_bound);

    const size_t compressed_bytes = ZSTD_compress(
        CompressedFile.data(), file_bound,
        file_data, file_bytes, kZstdCompressLevel);

    if (ZSTD_isError(compressed_bytes)) {
        cerr << "Zstd failed" << endl;
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
    Uplink.Shutdown();
}

void FileClient::Loop()
{
    cout << "FileClient: Loop started" << endl;

    while (!Terminated)
    {
        Uplink.Send();
    }

    while (!Terminated)
    {
        if (!Uplink.Receive([&](const uint8_t* data, int bytes) {
            
        })) {
            cerr << "Receive loop failed" << endl;
            break;
        }

        usleep(4000);
    }

    cout << "FileClient: Loop terminated" << endl;
}


} // namespace lora
