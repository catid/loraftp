// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include "tools.hpp"
#include "linux_serial.hpp"

namespace lora {


//------------------------------------------------------------------------------
// Constants

// Maximum Send() size
static const int kPacketMaxBytes = 240;

// Number of channels
static const int kChannelCount = 84;

static const int kCheckedChannelCount = 4;
static const int kCheckedChannels[kCheckedChannelCount] = {
    16, 32, 64, 82
};


//------------------------------------------------------------------------------
// Waveshare HAT API

class Waveshare
{
public:
    ~Waveshare()
    {
        Shutdown();
    }

    /*
        Channel = Initial channel to configure 0...kChannelCount-1
        Address is the node address or 0xffff for monitor mode
        LBT = Listen Before Transmit
    */
    bool Initialize(
        int channel, // initial channel
        uint16_t addr = 0xffff/*broadcast*/,
        bool lbt = false);
    void Shutdown();

    // Read and ignore data until we stop receiving input.
    // This might be helpful to resynchronize with the input stream.
    void DrainReceiveBuffer();

    // 0..83
    bool SetChannel(int channel, bool enable_ambient_rssi = false);

    // Send up to 240 bytes at a time
    bool Send(const uint8_t* data, int bytes);

    // Returns -1 on error.
    // Returns 0 if min_bytes not satisfied.
    // Otherwise returns number of bytes written
    int Receive(uint8_t* buffer, int buffer_bytes, int min_bytes);

    // Scan all channels and read ambient RSSI.
    // After this you must call SetChannel() again because it changes the channel
    bool ScanAmbientRssi(int retries = 10);

    // Updated by ScanAmbientRssi()
    // Note only the ones in kCheckedChannels are actually updated.
    float ChannelRssi[kChannelCount]; // dBm

protected:
    RawSerialPort Serial;
    bool InConfigMode = false;
    int Baudrate = 9600;

    bool EnterConfigMode();
    bool EnterTransmitMode();

    bool WriteConfig(int offset, const uint8_t* data, int bytes);

    bool ReadAmbientRssi(uint8_t& rssi);

    bool WaitForResponse(int minbytes);
};


} // namespace lora
