// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include <stdint.h>
#include <functional>
#include <memory>
#include <thread>
#include <system_error>

#include <spdlog/spdlog.h>

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const int kVersion = 100; // 1.0.0


//------------------------------------------------------------------------------
// Tools

uint64_t GetTimeUsec();
uint64_t GetTimeMsec();

// Data needs to be aligned to 8 byte address in memory
uint32_t FastCrc32(const void* data, int bytes);

/// Calls the provided (lambda) function at the end of the current scope
class ScopedFunction
{
public:
    ScopedFunction(std::function<void()> func) {
        Func = func;
    }
    ~ScopedFunction() {
        if (Func) {
            Func();
        }
    }
    void Cancel() {
        Func = std::function<void()>();
    }
    std::function<void()> Func;
};

/// Join a std::shared_ptr<std::thread>
inline void JoinThread(std::shared_ptr<std::thread>& th)
{
    if (th) {
        try {
            if (th->joinable()) {
                th->join();
            }
        } catch (std::system_error& /*err*/) {}
        th = nullptr;
    }
}


//------------------------------------------------------------------------------
// Byte Order

// Swaps byte order in a 16-bit word
inline uint16_t ByteSwap16(uint16_t word)
{
    return (word >> 8) | (word << 8);
}

// Swaps byte order in a 32-bit word
inline uint32_t ByteSwap32(uint32_t word)
{
    const uint16_t swapped_old_hi = ByteSwap16(static_cast<uint16_t>(word >> 16));
    const uint16_t swapped_old_lo = ByteSwap16(static_cast<uint16_t>(word));
    return (static_cast<uint32_t>(swapped_old_lo) << 16) | swapped_old_hi;
}

// Swaps byte order in a 64-bit word
inline uint64_t ByteSwap64(uint64_t word)
{
    const uint32_t swapped_old_hi = ByteSwap32(static_cast<uint32_t>(word >> 32));
    const uint32_t swapped_old_lo = ByteSwap32(static_cast<uint32_t>(word));
    return (static_cast<uint64_t>(swapped_old_lo) << 32) | swapped_old_hi;
}


//------------------------------------------------------------------------------
// POD Serialization

/**
 * array[2] = { 0, 1 }
 *
 * Little Endian: word = 0x0100 <- first byte is least-significant
 * Big Endian:    word = 0x0001 <- first byte is  most-significant
**/

/**
 * word = 0x0102
 *
 * Little Endian: array[2] = { 0x02, 0x01 }
 * Big Endian:    array[2] = { 0x01, 0x02 }
**/

// Little-endian 16-bit read
inline uint16_t ReadU16_LE(const void* data)
{
    const uint8_t* u8p = reinterpret_cast<const uint8_t*>(data);
    return ((uint16_t)u8p[1] << 8) | u8p[0];
}

// Big-endian 16-bit read
inline uint16_t ReadU16_BE(const void* data)
{
    return ByteSwap16(ReadU16_LE(data));
}

// Little-endian 24-bit read
inline uint32_t ReadU24_LE(const void* data)
{
    const uint8_t* u8p = reinterpret_cast<const uint8_t*>(data);
    return ((uint32_t)u8p[2] << 16) | ((uint32_t)u8p[1] << 8) | u8p[0];
}

// Big-endian 24-bit read
inline uint32_t ReadU24_BE(const void* data)
{
    const uint8_t* u8p = reinterpret_cast<const uint8_t*>(data);
    return ((uint32_t)u8p[0] << 16) | ((uint32_t)u8p[1] << 8) | u8p[2];
}

// Little-endian 32-bit read
inline uint32_t ReadU32_LE(const void* data)
{
    const uint8_t* u8p = reinterpret_cast<const uint8_t*>(data);
    return ((uint32_t)u8p[3] << 24) | ((uint32_t)u8p[2] << 16) | ((uint32_t)u8p[1] << 8) | u8p[0];
}

// Big-endian 32-bit read
inline uint32_t ReadU32_BE(const void* data)
{
    const uint8_t* u8p = reinterpret_cast<const uint8_t*>(data);
    return ((uint32_t)u8p[0] << 24) | ((uint32_t)u8p[1] << 16) | ((uint32_t)u8p[2] << 8) | u8p[3];
}

// Little-endian 64-bit read
inline uint64_t ReadU64_LE(const void* data)
{
    const uint8_t* u8p = reinterpret_cast<const uint8_t*>(data);
    return ((uint64_t)u8p[7] << 56) | ((uint64_t)u8p[6] << 48) | ((uint64_t)u8p[5] << 40) |
           ((uint64_t)u8p[4] << 32) | ((uint64_t)u8p[3] << 24) | ((uint64_t)u8p[2] << 16) |
           ((uint64_t)u8p[1] << 8) | u8p[0];
}

// Little-endian 16-bit write
inline void WriteU16_LE(void* data, uint16_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[1] = static_cast<uint8_t>(value >> 8);
    u8p[0] = static_cast<uint8_t>(value);
}

// Big-endian 16-bit write
inline void WriteU16_BE(void* data, uint16_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[0] = static_cast<uint8_t>(value >> 8);
    u8p[1] = static_cast<uint8_t>(value);
}

// Little-endian 24-bit write
inline void WriteU24_LE(void* data, uint32_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[2] = static_cast<uint8_t>(value >> 16);
    WriteU16_LE(u8p, static_cast<uint16_t>(value));
}

// Big-endian 24-bit write
inline void WriteU24_BE(void* data, uint32_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[0] = static_cast<uint8_t>(value >> 16);
    WriteU16_BE(u8p + 1, static_cast<uint16_t>(value));
}

// Little-endian 32-bit write
inline void WriteU32_LE(void* data, uint32_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[3] = (uint8_t)(value >> 24);
    u8p[2] = static_cast<uint8_t>(value >> 16);
    u8p[1] = static_cast<uint8_t>(value >> 8);
    u8p[0] = static_cast<uint8_t>(value);
}

// Big-endian 32-bit write
inline void WriteU32_BE(void* data, uint32_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[0] = (uint8_t)(value >> 24);
    u8p[1] = static_cast<uint8_t>(value >> 16);
    u8p[2] = static_cast<uint8_t>(value >> 8);
    u8p[3] = static_cast<uint8_t>(value);
}

// Little-endian 64-bit write
inline void WriteU64_LE(void* data, uint64_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[7] = static_cast<uint8_t>(value >> 56);
    u8p[6] = static_cast<uint8_t>(value >> 48);
    u8p[5] = static_cast<uint8_t>(value >> 40);
    u8p[4] = static_cast<uint8_t>(value >> 32);
    u8p[3] = static_cast<uint8_t>(value >> 24);
    u8p[2] = static_cast<uint8_t>(value >> 16);
    u8p[1] = static_cast<uint8_t>(value >> 8);
    u8p[0] = static_cast<uint8_t>(value);
}

// Big-endian 64-bit write
inline void WriteU64_BE(void* data, uint64_t value)
{
    uint8_t* u8p = reinterpret_cast<uint8_t*>(data);
    u8p[0] = static_cast<uint8_t>(value >> 56);
    u8p[1] = static_cast<uint8_t>(value >> 48);
    u8p[2] = static_cast<uint8_t>(value >> 40);
    u8p[3] = static_cast<uint8_t>(value >> 32);
    u8p[4] = static_cast<uint8_t>(value >> 24);
    u8p[5] = static_cast<uint8_t>(value >> 16);
    u8p[6] = static_cast<uint8_t>(value >> 8);
    u8p[7] = static_cast<uint8_t>(value);
}


//------------------------------------------------------------------------------
// Memory-mapped file

struct MappedView;

/// This represents a file on disk that will be mapped
struct MappedFile
{
    friend struct MappedView;

#if defined(_WIN32)
    /*HANDLE*/ void* File = nullptr;
#else
    int File = -1;
#endif

    bool ReadOnly = true;
    uint64_t Length = 0;

    inline bool IsValid() const { return Length != 0; }

    // Opens the file for shared read-only access with other applications
    // Returns false on error (file not found, etc)
    bool OpenRead(
        const char* path,
        bool read_ahead = false,
        bool no_cache = false);

    // Creates and opens the file for exclusive read/write access
    bool OpenWrite(
        const char* path,
        uint64_t size);

    // Resizes a file
    bool Resize(uint64_t size);

    void Close();

    MappedFile();
    ~MappedFile();
};


//------------------------------------------------------------------------------
// MappedView

/// View of a portion of the memory mapped file
struct MappedView
{
    void* Map = nullptr;
    MappedFile* File = nullptr;
    uint8_t* Data = nullptr;
    uint64_t Offset = 0;
    uint32_t Length = 0;

    // Returns false on error
    bool Open(MappedFile* file);

    // Returns 0 on error, 0 length means whole file
    uint8_t* MapView(uint64_t offset = 0, uint32_t length = 0);

    void Close();

    MappedView();
    ~MappedView();
};


//------------------------------------------------------------------------------
// MappedReadOnlySmallFile

/// Convenience wrapper around MappedFile/MappedView for reading small files
struct MappedReadOnlySmallFile
{
    /// Returns true if the file could be read, or false.
    /// File will be kept open until this object goes out of scope or Close()
    bool Read(const char* path);

    /// Release the file early
    void Close();


    const uint8_t* GetData()
    {
        return View.Data;
    }
    uint32_t GetDataBytes()
    {
        return View.Length;
    }

    // Ordered so that View goes out of scope first:

    MappedFile File;
    MappedView View;
};


//------------------------------------------------------------------------------
// File Helpers

/// Write the provided buffer to the file at the given path
bool WriteBufferToFile(const char* path, const void* data, uint64_t bytes);


//------------------------------------------------------------------------------
// Logging

// Set up color console and rotated disk logging from a background thread
void SetupAsyncDiskLog(const std::string& filename, bool print_debug_logs);


} // namespace lora
