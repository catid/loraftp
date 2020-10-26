// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"

#include <pigpio.h> // sudo apt install pigpio

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

bool Waveshare::Initialize(int channel, uint16_t transmit_addr, bool lbt)
{
    Shutdown();

    memset(ChannelRssi, 0, sizeof(ChannelRssi));
    memset(ChannelRssiRaw, 0, sizeof(ChannelRssiRaw));
    InConfigMode = false;
    Baudrate = 9600;
    RecvOffsetBytes = 0;
    CurrentAddress = TransmitAddress = transmit_addr;

    spdlog::debug("Entering config mode...");

    /*
        Getting the GPIOs to work was a pain on Raspberry Pi 4B because
        WiringPi is silently end-of-life and no longer works.

        The bcm2835 and pigpio libraries both seem to work,
        but bcm2835 you have to download and manually install,
        while pigpio is installed by default.
    */

    if (gpioInitialise() < 0) {
        spdlog::error("pigpio::gpioInitialise failed");
        return false;
    }

    gpioSetMode(kM0, PI_OUTPUT);
    gpioSetMode(kM1, PI_OUTPUT);
    gpioWrite(kM0, 0);
    gpioWrite(kM1, 0);

    // Wait longer on startup because it takes a bit to boot on first mode switch
    usleep(1000 * 1000);

    if (!EnterConfigMode()) {
        spdlog::error("EnterConfigMode failed");
        return false;
    }

    spdlog::debug("Configuring Waveshare HAT...");

    // Documentation here: https://www.waveshare.com/wiki/SX1262_915M_LoRa_HAT
    const int config_bytes = 9;
    const uint8_t config[config_bytes] = {
        /*
            Node address or 0xffff for monitor mode
        */
        (uint8_t)(transmit_addr >> 8), (uint8_t)transmit_addr,

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
        spdlog::error("WriteConfig failed");
        return false;
    }

    Baudrate = 115200;

    if (!ScanAmbientRssi()) {
        spdlog::error("ScanAmbientRssi failed");
        return false;
    }

    for (int i = 0; i < kCheckedChannelCount; ++i) {
        const int channel = kCheckedChannels[i];
        spdlog::debug("Channel {} ambient noise RSSI: {} dBm", channel, ChannelRssi[channel]);
    }

    // Configure back to initial channel without ambient RSSI measurement enabled
    if (!SetChannel(channel)) {
        spdlog::error("SetChannel failed");
        return false;
    }

    spdlog::debug("LoRa radio ready");
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

    spdlog::debug("Closing serial port...");

    Serial.Flush();
    Serial.Shutdown();

    spdlog::debug("Entering config mode...");

    gpioWrite(kM1, 1);

    usleep(kModeSwitchWaitUsec);

    spdlog::debug("Opening serial port...");

    if (!Serial.Initialize(kSerialDevice, 9600)) {
        spdlog::error("Failed to open serial port: {}", kSerialDevice);
        return false;
    }

    spdlog::debug("Now in config mode");
    InConfigMode = true;
    return true;
}

bool Waveshare::EnterTransmitMode()
{
    if (!InConfigMode) {
        return true;
    }

    spdlog::debug("Closing serial port...");

    Serial.Flush();
    Serial.Shutdown();

    spdlog::debug("Entering transmit mode...");

    gpioWrite(kM1, 0);

    usleep(kModeSwitchWaitUsec);

    spdlog::debug("Opening serial port...");

    if (!Serial.Initialize(kSerialDevice, Baudrate)) {
        spdlog::error("Failed to open serial port: {} baudrate={}", kSerialDevice, Baudrate);
        return false;
    }

    spdlog::debug("Now in transmit mode");
    InConfigMode = false;
    return true;
}

bool Waveshare::SetAddress(uint16_t addr)
{
    if (CurrentAddress == addr) {
        return true;
    }

    if (!EnterConfigMode()) {
        spdlog::error("SetAddress: EnterConfigMode failed");
        return false;
    }
    
    spdlog::debug("Configuring address {}...", addr);

    uint8_t config[2] = {
        (uint8_t)(addr >> 8), (uint8_t)addr
    };

    if (!WriteConfig(0, config, 2)) {
        spdlog::error("SetAddress: WriteConfig failed");
        return false;
    }

    if (!EnterTransmitMode()) {
        spdlog::error("SetAddress: EnterTransmitMode failed");
        return false;
    }

    CurrentAddress = addr;
    return true;
}

void Waveshare::DrainReceiveBuffer()
{
    // Receive state must be reset if we drain the buffer
    RecvOffsetBytes = 0;

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
        spdlog::error("SetChannel: EnterConfigMode failed");
        return false;
    }
    
    spdlog::debug("Configuring channel {}...", channel);

    uint8_t config[2] = {
        (uint8_t)(enable_ambient_rssi ? 0x20 : 0),
        (uint8_t)channel
    };

    if (!WriteConfig(4, config, 2)) {
        spdlog::error("SetChannel: WriteConfig failed");
        return false;
    }

    if (!EnterTransmitMode()) {
        spdlog::error("SetChannel: EnterTransmitMode failed");
        return false;
    }

    return true;
}

bool Waveshare::ScanAmbientRssi(int retries)
{
    spdlog::debug("*** Detecting ambient RSSI:");

    for (int i = 0; i < kCheckedChannelCount; ++i)
    {
        const int channel = kCheckedChannels[i];

        spdlog::debug("Setting channel {}...", channel);
        if (!SetChannel(channel, true)) {
            spdlog::error("ScanAmbientRssi: SetChannel failed");
            return false;
        }

        spdlog::debug("Reading RSSI for channel {}...", channel);

        uint8_t largest_rssi = 0;
        for (int j = 0; j < retries; ++j) {
            uint8_t rssi;
            if (!ReadAmbientRssi(rssi)) {
                spdlog::error("ScanAmbientRssi: ReadAmbientRssi failed on channel {}", channel);
                return false;
            }
            if (largest_rssi < rssi) {
                largest_rssi = rssi;
            }
        }

        const float dbm = largest_rssi * 0.5f;
        ChannelRssi[channel] = dbm;
        ChannelRssiRaw[channel] = largest_rssi;
    }

    return true;
}

bool Waveshare::ReadAmbientRssi(uint8_t& rssi)
{
    uint8_t read_rssi_command[6] = {
        0xc0, 0xc1, 0xc2, 0xc3, 0x00, 0x01
    };

    if (!Serial.Write(read_rssi_command, 6)) {
        spdlog::error("ReadAmbientRssi: Serial.Write failed");
        return false;
    }

    if (!WaitForResponse(4)) {
        spdlog::warn("ReadAmbientRssi: WaitForResponse timeout");
    }

    uint8_t readback[16];
    int r = Serial.Read(readback, 4);
    if (r != 4) {
        spdlog::error("ReadAmbientRssi: Serial.Read failed: r=", r);
        return false;
    }

    rssi = readback[3];
    return true;
}

bool Waveshare::WriteConfig(int offset, const uint8_t* data, int bytes)
{
    if (bytes >= 240) {
        spdlog::error("WriteConfig: invalid config len={}", bytes);
        return false;
    }

    uint8_t buffer[256];
    buffer[0] = 0xc2;
    buffer[1] = (uint8_t)offset;
    buffer[2] = (uint8_t)bytes;
    memcpy(buffer + 3, data, bytes);

    if (!Serial.Write(buffer, 3 + bytes)) {
        spdlog::error("WriteConfig: Serial.Write failed");
        return false;
    }

    if (!WaitForResponse(3 + bytes)) {
        spdlog::warn("WriteConfig: WaitForResponse timeout");
    }

    uint8_t readback[256];
    int r = Serial.Read(readback, 3 + bytes);
    if (r != 3 + bytes) {
        spdlog::error("WriteConfig: Serial.Read failed: r={} bytes={}", r, bytes);
        return false;
    }

    if (readback[0] != 0xc1) {
        spdlog::error("WriteConfig: Response not 0xc1 actual={}", r);
        return false;
    }

    if (0 != memcmp(readback + 1, buffer + 1, 2 + bytes)) {
        spdlog::error("WriteConfig: Readback did not match config bytes=", bytes);
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
            spdlog::error("Timeout waiting for config result avail={}", Serial.GetAvailable());
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
        spdlog::error("FIXME: Send() parameter too large! bytes={}", bytes);
        return false;
    }

    if (!SetAddress(TransmitAddress)) {
        spdlog::error("Send: SetAddress failed");
        return false;
    }

    uint8_t frame[240];
    frame[0] = static_cast<uint8_t>( bytes );
    WriteU32_LE(frame + 1, FastCrc32(data, bytes));
    memcpy(frame + 1 + 4, data, bytes);

    return Serial.Write(frame, 1 + 4 + bytes);
}

bool Waveshare::FillRecvBuffer()
{
    const int remaining_buffer_bytes = kRecvBufferBytes - RecvOffsetBytes;
    if (remaining_buffer_bytes <= 0) {
        return true;
    }

    const int available = Serial.GetAvailable();
    if (available < 0) {
        RecvOffsetBytes = 0;
        return false;
    }
    if (available == 0) {
        return true;
    }

    int read_bytes = available;
    if (read_bytes > remaining_buffer_bytes) {
        read_bytes = remaining_buffer_bytes;
    }

    int r = Serial.Read(RecvBuffer + RecvOffsetBytes, read_bytes);
    if (r != read_bytes) {
        RecvOffsetBytes = 0;
        return false;
    }

    RecvOffsetBytes += read_bytes;
    return true;
}

bool Waveshare::Receive(std::function<void(const uint8_t* data, int bytes)> callback)
{
    if (!SetAddress(kMonitorAddress)) {
        spdlog::error("Send: SetAddress failed");
        return false;
    }

    if (!FillRecvBuffer()) {
        return false;
    }

    const int buffer_bytes = RecvOffsetBytes;

    int start_offset;
    for (start_offset = 0; start_offset + 5 < buffer_bytes; ++start_offset)
    {
        const int packet_bytes = RecvBuffer[start_offset];
        if (packet_bytes <= 0 || packet_bytes > kPacketMaxBytes) {
            // Not the start of a packet
            continue;
        }

        const int available_bytes = buffer_bytes - start_offset;
        if (available_bytes < 5 + packet_bytes) {
            // Not enough data arrived yet
            break;
        }

        const uint32_t expected_crc = ReadU32_LE(RecvBuffer + start_offset + 1);
        const uint32_t crc = FastCrc32(RecvBuffer + start_offset + 5, packet_bytes);
        if (expected_crc != crc) {
            // Not the start of a packet
            continue;
        }

        callback(RecvBuffer + start_offset + 5, packet_bytes);

        // Skip ahead to next potential start point
        start_offset += 4 + packet_bytes;
    }

    // If we need to eliminate data from the start:
    if (start_offset > 0) {
        RecvOffsetBytes = buffer_bytes - start_offset;
        memmove(RecvBuffer, RecvBuffer + start_offset, RecvOffsetBytes);
    }

    return true;
}


} // namespace lora
