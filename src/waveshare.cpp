// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"

#include "bcm2835.h"

#include <termios.h>

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

static bool waveshareConfig(int fd, int offset, const uint8_t* data, int bytes)
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

    ssize_t r = write(fd, buffer, 3 + bytes);
    if (r < 0) {
        cerr << "write failed: r=" << r << endl;
        return false;
    }

    uint64_t t0 = GetTimeMsec();

    while (serialDataAvail(fd) < 3 + bytes) {
        uint64_t t1 = GetTimeMsec();
        int64_t dt = t1 - t0;

        if (dt > 5000) {
            cerr << "timeout waiting for config result avail=" << serialDataAvail(fd) << endl;
            return false;
        }

        this_thread::sleep_for(chrono::milliseconds(10));
    }

    uint8_t readback[256];
    r = read(fd, readback, 3 + bytes);
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

bool Waveshare::Initialize(int channel, uint16_t addr, bool lbt)
{
    cout << "Entering config mode..." << endl;

    if (-1 == wiringPiSetup()) {
        cerr << "wiringPiSetup failed.  May need to run as root" << endl;
        return false;
    }

    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_13, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_15, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_write(RPI_V2_GPIO_P1_13, LOW);
    bcm2835_gpio_write(RPI_V2_GPIO_P1_15, HIGH);

    this_thread::sleep_for(chrono::milliseconds(1000));

    cout << "Opening serial port..." << endl;

    fd = serialOpen("/dev/ttyS0", 9600);
    if (-1 == fd) {
        cerr << "serialOpen 9600 failed" << endl;
        return false;
    }
    serialFlush(fd);

    struct termios options{};
    tcgetattr(fd, &options);

    //cfmakeraw(&options);
    //cfsetspeed(&options, B9600);

    //options.c_cflag |= IGNPAR;

    options.c_oflag &= ~(PARENB | PARODD | CMSPAR);
    options.c_oflag &= ~(OPOST | ONLCR | OCRNL);

    options.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK);
    options.c_iflag &= ~(IUCLC);
    options.c_iflag &= ~(PARMRK);
    options.c_iflag &= ~(INPCK | ISTRIP);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_iflag &= ~(CRTSCTS);

    options.c_lflag = ICANON;

    options.c_cc[VTIME] = 10; // 1 second timeout

    serialFlush(fd);
    tcsetattr(fd, TCSANOW, &options);

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

    if (!waveshareConfig(fd, 0, config, config_bytes)) {
        cerr << "waveshareConfig failed" << endl;
        return false;
    }

    cout << "Entering transmit mode..." << endl;

    digitalWrite(2, LOW);

    this_thread::sleep_for(chrono::milliseconds(1000));

#if 0
    serialClose(fd);

    fd = serialOpen("/dev/ttyS0", 115200);
    if (-1 == fd) {
        cerr << "serialOpen 115200 failed" << endl;
        return false;
    }
#endif

    cout << "LoRa radio ready." << endl;

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
    int r = write(fd, data, bytes);
    if (r != bytes) {
        cerr << "write error: r=" << r << endl;
    }
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

    int r = read(fd, buffer, bytes);
    if (r != bytes) {
        cerr << "read error: r=" << r << endl;
        return -1;
    }

    return bytes;
}


} // namespace lora
