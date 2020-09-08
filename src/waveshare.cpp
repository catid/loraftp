// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"

#include <wiringPi.h>
#include <wiringSerial.h>

#include <iostream>
using namespace std;

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const int kM0 = 22; // BCM GPIO
static const int kM1 = 27; // BCM GPIO

static const uint8_t kNetId = 0xad;

static const uint8_t kKeyHi = 0x06;
static const uint8_t kKeyLo = 0x07;


//------------------------------------------------------------------------------
// Waveshare HAT API

static void serialWrite(int fd, const uint8_t* data, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        serialPutchar(fd, data[i]);
    }
}

static bool waveshareConfig(int fd, int offset, const uint8_t* data, int bytes)
{
    serialPutchar(fd, 0xc0);
    serialPutchar(fd, (uint8_t)offset);
    serialPutchar(fd, (uint8_t)bytes);

    serialWrite(fd, data, bytes);

    int r = serialGetchar(fd);
    if (r != 0xc1) {
        cerr << "serialGetchar: failed response not 0xc1 actual=" << r << endl;
        return false;
    }
    r = serialGetchar(fd);
    if (r != (uint8_t)offset) {
        cerr << "serialGetchar: incorrect at offset 1" << endl;
        return false;
    }
    r = serialGetchar(fd);
    if (r != (uint8_t)bytes) {
        cerr << "serialGetchar: incorrect at offset 2" << endl;
        return false;
    }

    for (int i = 0; i < bytes; ++i) {
        r = serialGetchar(fd);

        if (r != data[i]) {
            cerr << "serialGetchar: incorrect at offset " << i <<
                " expected=" << (int)data[i] << " actual=" << r << endl;
            return false;
        }
    }

    return true;
}

bool Waveshare::Initialize(int channel, uint16_t addr, bool lbt)
{
    if (-1 == wiringPiSetupGpio()) {
        cerr << "wiringPiSetupGpio failed.  May need to run as root" << endl;
        return false;
    }

    pinMode(kM0, OUTPUT);
    pinMode(kM1, OUTPUT);
    digitalWrite(kM0, LOW);
    digitalWrite(kM1, HIGH);

    fd = serialOpen("/dev/ttyS0", 9600);
    if (-1 == fd) {
        cerr << "serialOpen 9600 failed" << endl;
        return false;
    }

    // Documentation here: https://www.waveshare.com/wiki/SX1262_915M_LoRa_HAT
    const int config_bytes = 9;
    const uint8_t config[config_bytes] = {
        (uint8_t)(addr >> 8), (uint8_t)addr,
        kNetId,
        0x67, /* 011 00 111 : Baud rate 9600, 8N1 Parity, Air speed 62.5K */
        0x00, /* 00 0 000 00 : 240 Bytes, ambient noise, 22 dBm power */
        (uint8_t)channel,
        (uint8_t)(0x80 | (lbt ? 0x10 : 0)), /* 1 0 0 L 0 000 : Enable RSSI byte... */
        kKeyHi,
        kKeyLo
    };

    if (!waveshareConfig(fd, 0, config, config_bytes)) {
        cerr << "waveshareConfig failed" << endl;
        return false;
    }

    digitalWrite(kM1, LOW);

#if 0
    serialClose(fd);

    fd = serialOpen("/dev/ttyS0", 115200);
    if (-1 == fd) {
        cerr << "serialOpen 115200 failed" << endl;
        return false;
    }
#endif

    return true;
}

void Waveshare::Shutdown()
{
    if (fd != -1) {
        serialClose(fd);
        fd = -1;
    }
}

void Waveshare::Send(const uint8_t* data, int bytes)
{
    serialWrite(fd, data, bytes);
}

int Waveshare::Receive(uint8_t* buffer, int buffer_bytes)
{
    int bytes = serialDataAvail(fd);
    if (bytes <= 0) {
        return bytes;
    }

    if (bytes > buffer_bytes) {
        bytes = buffer_bytes;
    }

    for (int i = 0; i < bytes; ++i) {
        const int r = serialGetchar(fd);
        if (r < 0) {
            return r;
        }
        buffer[i] = (uint8_t)r;
    }

    return bytes;
}


} // namespace lora
