// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"

// If this header is missing, download, build and install from:
// https://www.airspayce.com/mikem/bcm2835/
// Detailed instructions in the README.md
#include <bcm2835.h> // Must be installed system-wide

#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <cstring>
using namespace std;

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const uint8_t kNetId = 0x00;

static const uint8_t kKeyHi = 0x00;
static const uint8_t kKeyLo = 0x00;


//------------------------------------------------------------------------------
// Waveshare HAT API

bool Waveshare::Initialize(int channel, uint16_t addr, bool lbt)
{
    cout << "Entering config mode..." << endl;

    if (!bcm2835_init()) {
        cerr << "bcm2835_init failed" << endl;
        return false;
    }

    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_13, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_15, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_write(RPI_V2_GPIO_P1_13, LOW);
    bcm2835_gpio_write(RPI_V2_GPIO_P1_15, HIGH);

    this_thread::sleep_for(chrono::milliseconds(1000));

    cout << "Opening serial port..." << endl;

    if (!Serial.Initialize("/dev/ttyS0", 9600)) {
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
        cerr << "Serial.Config failed" << endl;
        return false;
    }

    cout << "Entering transmit mode..." << endl;

    bcm2835_gpio_write(RPI_V2_GPIO_P1_15, LOW);

    Serial.Shutdown();

    this_thread::sleep_for(chrono::milliseconds(1000));

    if (!Serial.Initialize("/dev/ttyS0", 9600)) {
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

        this_thread::sleep_for(chrono::milliseconds(10));
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
