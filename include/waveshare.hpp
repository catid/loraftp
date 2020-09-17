// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include "tools.hpp"
#include "linux_serial.hpp"

namespace lora {


//------------------------------------------------------------------------------
// Constants

// Maximum Send() size
// HACK: We add a header to fix truncation problem with this HAT
static const int kPacketMaxBytes = 235;

// Number of channels
static const int kChannelCount = 84;

static const int kCheckedChannelCount = 4;
static const int kCheckedChannels[kCheckedChannelCount] = {
    16, 32, 48, 64
};

enum class ReceiveState
{
    Header,
    Body
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

    int GetSendQueueBytes()
    {
        return Serial.GetSendQueueBytes();
    }

    // Poll for incoming data, written to RecvBuffer member.
    // Returns number of bytes received.
    // Returns 0 if no data has arrived.
    // Returns -1 on read error.
    int Receive();

    // Scan all channels and read ambient RSSI.
    // After this you must call SetChannel() again because it changes the channel
    bool ScanAmbientRssi(int retries = 10);

    // Receive() data goes here
    uint8_t RecvBuffer[240];

    // Updated by ScanAmbientRssi()
    // Note only the ones in kCheckedChannels are actually updated.
    float ChannelRssi[kChannelCount]; // dBm

protected:
    RawSerialPort Serial;
    bool InConfigMode = false;
    int Baudrate = 9600;

    ReceiveState RecvState = ReceiveState::Header;
    int RecvExpectedBytes = 0;
    uint32_t RecvExpectedCrc32 = 0;
    uint64_t RecvStartUsec = 0;

    bool EnterConfigMode();
    bool EnterTransmitMode();

    bool WriteConfig(int offset, const uint8_t* data, int bytes);

    bool ReadAmbientRssi(uint8_t& rssi);

    bool WaitForResponse(int minbytes);
};


} // namespace lora
