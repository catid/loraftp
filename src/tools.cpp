// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "tools.hpp"

#if !defined(_WIN32)
    #include <pthread.h>
    #include <unistd.h>
#endif // _WIN32

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif __MACH__
    #include <mach/mach_time.h>
    #include <mach/mach.h>
    #include <mach/clock.h>

    extern mach_port_t clock_port;
#else
    #include <time.h>
    #include <sys/time.h>
#endif

#include <arm_acle.h>
#include <arm_neon.h>

namespace lora {


//------------------------------------------------------------------------------
// Tools

#ifdef _WIN32
static double PerfFrequencyInverse = 0.;

static void InitPerfFrequencyInverse()
{
    LARGE_INTEGER freq = {};
    if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
        return;
    PerfFrequencyInverse = 1000000. / (double)freq.QuadPart;
}
#elif __MACH__
static bool m_clock_serv_init = false;
static clock_serv_t m_clock_serv = 0;

static void InitClockServ()
{
    m_clock_serv_init = true;
    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &m_clock_serv);
}
#endif // _WIN32

uint64_t GetTimeUsec()
{
#ifdef _WIN32
    LARGE_INTEGER timeStamp = {};
    if (!::QueryPerformanceCounter(&timeStamp))
        return 0;
    if (PerfFrequencyInverse == 0.)
        InitPerfFrequencyInverse();
    return (uint64_t)(PerfFrequencyInverse * timeStamp.QuadPart);
#elif __MACH__
    if (!m_clock_serv_init)
        InitClockServ();

    mach_timespec_t tv;
    clock_get_time(m_clock_serv, &tv);

    return 1000000 * tv.tv_sec + tv.tv_nsec / 1000;
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif
}

uint64_t GetTimeMsec()
{
    return GetTimeUsec() / 1000;
}

uint32_t FastCrc32(const void* vdata, int bytes)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>( vdata );

    uint32_t crc = 0xdeadbeef;

    while (bytes >= 8) {
        crc = __crc32cd(crc, ReadU64_LE(data));
        data += 8;
        bytes -= 8;
    }

    if (bytes & 4) {
        crc = __crc32cw(crc, ReadU32_LE(data));
        data += 4;
    }

    if (bytes & 2) {
        crc = __crc32ch(crc, ReadU16_LE(data));
        data += 2;
    }

    if (bytes & 1) {
        crc = __crc32cb(crc, *data);
    }

    return crc;
}


} // namespace lora
