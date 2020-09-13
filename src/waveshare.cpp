// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"

/*
    Getting the GPIOs to work was a pain on Raspberry Pi 4B.

    WiringPi is silently deprecated and no longer works with Raspberry Pi 4.
    The bcm2835 and pigpio libraries both seem to work,
    but bcm2835 you have to download and manually install,
    while pigpio is installed by default on RPi.
*/

#include <pigpio.h> // sudo apt install pigpio

#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <cstring>
#include <functional>
using namespace std;

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const char* kSerialDevice = "/dev/ttyS0";

static const uint8_t kM0 = 22;
static const uint8_t kM1 = 27;

static const uint8_t kNetId = 0x00;

static const uint8_t kKeyHi = 0x00;
static const uint8_t kKeyLo = 0x00;


//------------------------------------------------------------------------------
// Tools

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


//------------------------------------------------------------------------------
// Waveshare HAT API

bool Waveshare::Initialize(int channel, uint16_t addr, bool lbt)
{
    cout << "Entering config mode..." << endl;

    if (gpioInitialise() < 0) {
        cerr << "pigpio init failed" << endl;
        return false;
    }
    ScopedFunction gpio_scope([]() {
        gpioTerminate();
    });

    gpioSetMode(kM0, PI_OUTPUT);
    gpioSetMode(kM1, PI_OUTPUT);
    gpioWrite(kM0, 0);
    gpioWrite(kM1, 0);

    usleep(1000000);

    gpioWrite(kM1, 1);

    usleep(1000000);

    cout << "Opening serial port..." << endl;

    if (!Serial.Initialize(kSerialDevice, 9600)) {
        cerr << "Failed to open serial port" << endl;
        return false;
    }

    cout << "Configuring Waveshare HAT..." << endl;

    // Documentation here: https://www.waveshare.com/wiki/SX1262_915M_LoRa_HAT
    const int config_bytes = 9;
    const uint8_t config[config_bytes] = {
        (uint8_t)(addr >> 8), (uint8_t)addr,
        kNetId,
        0x62, /* 011 00 111 : Baud rate 9600, 8N1 Parity, Air speed 62.5K */
        0x00, /* 00 0 000 00 : 240 Bytes, ambient noise, 22 dBm power */
        (uint8_t)channel,
        (uint8_t)(0x03 | (lbt ? 0x10 : 0)), /* 0 0 0 L 0 000 : Enable RSSI byte... */
        kKeyHi,
        kKeyLo
    };

    if (!WriteConfig(0, config, config_bytes)) {
        cerr << "WriteConfig failed" << endl;
        return false;
    }

    cout << "Closing serial port..." << endl;

    Serial.Flush();
    Serial.Shutdown();

    cout << "Entering transmit mode..." << endl;

    gpioWrite(kM1, 0);

    usleep(1000000);

    cout << "Opening serial port..." << endl;

    if (!Serial.Initialize(kSerialDevice, 9600)) {
        cerr << "Failed to open serial port" << endl;
        return false;
    }

    cout << "LoRa radio ready." << endl;

    return true;
}

void Waveshare::Shutdown()
{
    Serial.Shutdown();
}

bool Waveshare::WriteConfig(int offset, const uint8_t* data, int bytes)
{
    if (bytes >= 240) {
        cerr << "invalid config len" << endl;
        return false;
    }

    uint8_t buffer[256];
    buffer[0] = 0xc2;
    buffer[1] = (uint8_t)offset;
    buffer[2] = (uint8_t)bytes;
    memcpy(buffer + 3, data, bytes);

    if (!Serial.Write(buffer, 3 + bytes)) {
        cerr << "Serial.Write failed" << endl;
        return false;
    }

    uint64_t t0 = GetTimeMsec();

    while (Serial.GetAvailable() < 3 + bytes)
    {
        uint64_t t1 = GetTimeMsec();
        int64_t dt = t1 - t0;

        if (dt > 5000) {
            cerr << "timeout waiting for config result avail=" << Serial.GetAvailable() << endl;
            return false;
        }

        usleep(10000);
    }

    uint8_t readback[256];
    int r = Serial.Read(readback, 3 + bytes);
    if (r != 3 + bytes) {
        cerr << "read failed: r=" << r << endl;
        return false;
    }

    if (readback[0] != 0xc1) {
        cerr << "failed response not 0xc1 actual=" << r << endl;
        return false;
    }

    if (0 != memcmp(readback + 1, buffer + 1, 2 + bytes)) {
        cerr << "readback did not match config" << endl;
        return false;
    }

    return true;
}

bool Waveshare::Send(const uint8_t* data, int bytes)
{
    return Serial.Write(data, bytes);
}

int Waveshare::Receive(uint8_t* buffer, int buffer_bytes)
{
    int bytes = Serial.GetAvailable();
    if (bytes <= 0) {
        return bytes;
    }

    if (bytes > buffer_bytes) {
        bytes = buffer_bytes;
    }

    int r = Serial.Read(buffer, bytes);
    if (r != bytes) {
        return -1;
    }

    return bytes;
}


} // namespace lora
