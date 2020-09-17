// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"

#include <pigpio.h> // sudo apt install pigpio

#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <cstring>
using namespace std;

namespace lora {


//------------------------------------------------------------------------------
// Constants

static const char* kSerialDevice = "/dev/ttyS0";

static const int kModeSwitchWaitUsec = 100 * 1000; // 100 msec

static const uint8_t kM0 = 22;
static const uint8_t kM1 = 27;

// FIXME: Pick these
static const uint8_t kNetId = 0x00;

static const uint8_t kKeyHi = 0x00;
static const uint8_t kKeyLo = 0x00;


//------------------------------------------------------------------------------
// Waveshare HAT API

bool Waveshare::Initialize(int channel, uint16_t addr, bool lbt)
{
    Shutdown();

    memset(ChannelRssi, 0, sizeof(ChannelRssi));
    InConfigMode = false;
    Baudrate = 9600;
    RecvState = ReceiveState::Header;

    cout << "Entering config mode..." << endl;

    /*
        Getting the GPIOs to work was a pain on Raspberry Pi 4B because
        WiringPi is silently end-of-life and no longer works.

        The bcm2835 and pigpio libraries both seem to work,
        but bcm2835 you have to download and manually install,
        while pigpio is installed by default.
    */

    if (gpioInitialise() < 0) {
        cerr << "pigpio init failed" << endl;
        return false;
    }

    gpioSetMode(kM0, PI_OUTPUT);
    gpioSetMode(kM1, PI_OUTPUT);
    gpioWrite(kM0, 0);
    gpioWrite(kM1, 0);

    // Wait longer on startup because it takes a bit to boot on first mode switch
    usleep(1000 * 1000);

    if (!EnterConfigMode()) {
        cerr << "EnterConfigMode failed" << endl;
        return false;
    }

    cout << "Configuring Waveshare HAT..." << endl;

    // Documentation here: https://www.waveshare.com/wiki/SX1262_915M_LoRa_HAT
    const int config_bytes = 9;
    const uint8_t config[config_bytes] = {
        /*
            Node address or 0xffff for monitor mode
        */
        (uint8_t)(addr >> 8), (uint8_t)addr,

        /*
            Network id for all nodes
        */
        kNetId,

        /*
            111 00 111
            ^^^-------- Baudrate = 115200 (for transmit mode)
                ^^----- 8N1 (no parity bit)
                   ^^^- Airspeed = 62.5 Kilobits/second
        */
        0xe7,

        /*
            00 0 000 00
            ^^---------- 240 bytes per packet
               ^-------- Enable ambient noise
                 ^^^---- Reserved (0)
                     ^^- 22 dBm transmit power
        */
        0,

        /*
            Channel control (CH) 0-83. 84 channels in total
            Actually frequency is 850.125 + CH * 1MHz. Default 868.125MHz

            We start from channel 0 for ambient RSSI detection to be thorough.
        */
        (uint8_t)channel,

        /*
            0 0 0 L 0 011
            ^------------- Enable RSSI on receive
              ^----------- Transparent transmitting
                ^--------- Relay disabled
                  ^------- LBT enabled?
                    ^----- WOR transmit mode
                      ^^^- WOR period = 2000 msec
        */
        (uint8_t)(0x03 | (lbt ? 0x10 : 0)),

        kKeyHi, kKeyLo
    };

    if (!WriteConfig(0, config, config_bytes)) {
        cerr << "WriteConfig failed" << endl;
        return false;
    }

    Baudrate = 115200;

    if (!ScanAmbientRssi()) {
        cerr << "ScanAmbientRssi failed" << endl;
        return false;
    }

    for (int i = 0; i < kCheckedChannelCount; ++i) {
        const int channel = kCheckedChannels[i];
        cout << "Ambient noise RSSI (channel " << channel << "): " << ChannelRssi[channel] << " dBm" << endl;
    }

    // Configure back to initial channel without ambient RSSI measurement enabled
    if (!SetChannel(channel)) {
        cerr << "SetChannel failed" << endl;
        return false;
    }

    cout << "LoRa radio ready." << endl;

    return true;
}

void Waveshare::Shutdown()
{
    Serial.Shutdown();
    gpioTerminate();
}

bool Waveshare::EnterConfigMode()
{
    if (InConfigMode) {
        return true;
    }

    cout << "Closing serial port..." << endl;

    Serial.Flush();
    Serial.Shutdown();

    cout << "Entering config mode..." << endl;

    gpioWrite(kM1, 1);

    usleep(kModeSwitchWaitUsec);

    cout << "Opening serial port..." << endl;

    if (!Serial.Initialize(kSerialDevice, 9600)) {
        cerr << "Failed to open serial port" << endl;
        return false;
    }

    InConfigMode = true;

    return true;
}

bool Waveshare::EnterTransmitMode()
{
    if (!InConfigMode) {
        return true;
    }

    cout << "Closing serial port..." << endl;

    Serial.Flush();
    Serial.Shutdown();

    cout << "Entering transmit mode..." << endl;

    gpioWrite(kM1, 0);

    usleep(kModeSwitchWaitUsec);

    cout << "Opening serial port..." << endl;

    if (!Serial.Initialize(kSerialDevice, Baudrate)) {
        cerr << "Failed to open serial port" << endl;
        return false;
    }

    InConfigMode = false;

    // We may have received part of a packet in the buffer so drain the buffer
    DrainReceiveBuffer();

    return true;
}

void Waveshare::DrainReceiveBuffer()
{
    // Receive state must be reset if we drain the buffer
    RecvState = ReceiveState::Header;

    int available_bytes = Serial.GetAvailable();

    if (available_bytes <= 0) {
        return; // Done!
    }

    uint8_t buffer[256];
    while (available_bytes > 0) {
        int read_bytes = available_bytes;
        if (read_bytes > 256) {
            read_bytes = 256;
        }
        read_bytes = Serial.Read(buffer, read_bytes);
        if (read_bytes <= 0) {
            return; // Done!
        }
        available_bytes -= read_bytes;
    }
}

bool Waveshare::SetChannel(int channel, bool enable_ambient_rssi)
{
    if (!EnterConfigMode()) {
        cerr << "SetChannel: EnterConfigMode failed" << endl;
        return false;
    }
    
    cout << "Configuring channel " << channel << "..." << endl;

    uint8_t config[2] = {
        (uint8_t)(enable_ambient_rssi ? 0x20 : 0),
        (uint8_t)channel
    };

    if (!WriteConfig(4, config, 2)) {
        cerr << "SetChannel: WriteConfig failed" << endl;
        return false;
    }

    if (!EnterTransmitMode()) {
        cerr << "SetChannel: EnterTransmitMode failed" << endl;
        return false;
    }

    return true;
}

bool Waveshare::ScanAmbientRssi(int retries)
{
    cout << "*** Detecting ambient RSSI:" << endl;

    for (int i = 0; i < kCheckedChannelCount; ++i)
    {
        const int channel = kCheckedChannels[i];

        cout << "Setting channel " << channel << "..." << endl;
        if (!SetChannel(channel, true)) {
            cerr << "ScanAmbientRssi: SetChannel failed" << endl;
            return false;
        }

        cout << "Reading RSSI for channel " << channel << "..." << endl;

        uint8_t largest_rssi = 0;
        for (int j = 0; j < retries; ++j) {
            uint8_t rssi;
            if (!ReadAmbientRssi(rssi)) {
                cerr << "ScanAmbientRssi: ReadAmbientRssi failed on channel " << channel << endl;
                return false;
            }
            if (largest_rssi < rssi) {
                largest_rssi = rssi;
            }
        }

        const float dbm = largest_rssi * 0.5f;
        ChannelRssi[channel] = dbm;
    }

    return true;
}

bool Waveshare::ReadAmbientRssi(uint8_t& rssi)
{
    uint8_t read_rssi_command[6] = {
        0xc0, 0xc1, 0xc2, 0xc3, 0x00, 0x01
    };

    if (!Serial.Write(read_rssi_command, 6)) {
        cerr << "Failed to write RSSI request" << endl;
        return false;
    }

    if (!WaitForResponse(4)) {
        cerr << "ReadAmbientRssi: Timeout" << endl;
    }

    uint8_t readback[16];
    int r = Serial.Read(readback, 4);
    if (r != 4) {
        cerr << "ReadAmbientRssi: read failed: r=" << r << endl;
        return false;
    }

    rssi = readback[3];
    return true;
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

    if (!WaitForResponse(3 + bytes)) {
        cerr << "WriteConfig: Timeout" << endl;
    }

    uint8_t readback[256];
    int r = Serial.Read(readback, 3 + bytes);
    if (r != 3 + bytes) {
        cerr << "read failed: r=" << r << endl;
        for (int i = 0; i < r; ++i) {
            cerr << "readback[" << i << "] = " << (int)readback[i] << endl;
        }
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

bool Waveshare::WaitForResponse(int minbytes)
{
    uint64_t t0 = GetTimeMsec();

    while (Serial.GetAvailable() < minbytes)
    {
        uint64_t t1 = GetTimeMsec();
        int64_t dt = t1 - t0;

        if (dt > 5000) {
            cerr << "timeout waiting for config result avail=" << Serial.GetAvailable() << endl;
            return false;
        }

        // Avoid hard spins while waiting
        usleep(10000);
    }

    return true;
}

bool Waveshare::Send(const uint8_t* data, int bytes)
{
    if (bytes > kPacketMaxBytes) {
        cerr << "FIXME: Send() parameter too large! bytes=" << bytes << endl;
        return false;
    }

    uint8_t frame[240];
    frame[0] = static_cast<uint8_t>( bytes );
    WriteU32_LE(frame + 1, FastCrc32(data, bytes));
    memcpy(frame + 1 + 4, data, bytes);

    return Serial.Write(frame, 1 + 4 + bytes);
}

int Waveshare::Receive()
{
    if (RecvState == ReceiveState::Header) {
        int bytes = Serial.GetAvailable();
        if (bytes < 5) {
            return 0; // Wait for more to arrive
        }

        int r = Serial.Read(RecvBuffer, 5);
        if (r != 5) {
            cerr << "Receive: Read failed" << endl;
            return -1;
        }

        RecvExpectedBytes = RecvBuffer[0];
        if (RecvExpectedBytes > kPacketMaxBytes) {
            // Desynched!
            DrainReceiveBuffer();
            return 0;
        }
        RecvExpectedCrc32 = ReadU32_LE(RecvBuffer + 1);

        RecvState = ReceiveState::Body;
        RecvStartUsec = GetTimeUsec();
        // fall-thru
    }

    if (Serial.GetAvailable() < RecvExpectedBytes) {
        const uint64_t t1 = GetTimeUsec();
        const int64_t dt = t1 - RecvStartUsec;
        const int64_t timeout_usec = 10000;
        if (dt > timeout_usec) {
            // Desynched!
            DrainReceiveBuffer();
        }
        return 0;
    }

    int r = Serial.Read(RecvBuffer, RecvExpectedBytes);
    if (r != RecvExpectedBytes) {
        // Desynched!
        DrainReceiveBuffer();
        if (r < 0) {
            cerr << "Receive: Read failed (2)" << endl;
            return -1;
        }
        return 0;
    }

    const uint32_t crc = FastCrc32(RecvBuffer, RecvExpectedBytes);
    if (crc != RecvExpectedCrc32) {
        // Desynched!
        DrainReceiveBuffer();
        return 0;
    }

    return RecvExpectedBytes;
}


} // namespace lora
