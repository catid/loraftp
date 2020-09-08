// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"
using namespace lora;

#include <iostream>
#include <fstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
using namespace std;

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


//------------------------------------------------------------------------------
// Entrypoint

atomic<bool> Terminated = ATOMIC_VAR_INIT(false);

void SignalHandler(int)
{
    Terminated = true;
}

int main(int argc, char* argv[])
{
    cout << "echo_test" << endl;

    Waveshare waveshare;

    const int channel = 0x17;

    if (!waveshare.Initialize(channel)) {
        cerr << "Failed to initialize";
        return -1;
    }

    signal(SIGINT, SignalHandler);

    uint64_t t0 = GetTimeUsec();

    while (!Terminated)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t t1 = GetTimeUsec();
        int64_t dt = t1 - t0;

        if (dt > 500) {
            uint8_t data[kPacketMaxBytes] = {
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10
            };
            waveshare.Send(data, 10);
            t0 = t1;
        }

        uint8_t buffer[kPacketMaxBytes];
        int bytes = waveshare.Receive(buffer, kPacketMaxBytes);
        if (bytes < 0) {
            cerr << "Link broken" << endl;
            break;
        }
        if (bytes > 0) {
            cout << "Got bytes:";
            for (int i = 0; i < bytes; ++i) {
                cout << " " << (int)buffer[i];
            }
            cout << endl;
        }
    }

    cout << "Initialize succeeded" << endl;
    return 0;
}
