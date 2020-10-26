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

// Monitor address can receive but not transmit.
// Other addresses can transmit but not receive.
static const uint16_t kMonitorAddress = UINT16_C(0xffff);


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
        LBT = Listen Before Transmit (adds ~2 seconds of latency).
    */
    bool Initialize(
        int channel, // Initial channel
        uint16_t transmit_addr, // Address to use when transmitting
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

    // Calls callback for each packet received.
    // Returns false if pipe breaks.
    bool Receive(std::function<void(const uint8_t* data, int bytes)> callback);

    // Scan all channels and read ambient RSSI.
    // After this you must call SetChannel() again because it changes the channel
    bool ScanAmbientRssi(int retries = 10);

    // Updated by ScanAmbientRssi()
    // Note only the ones in kCheckedChannels are actually updated.
    uint8_t ChannelRssiRaw[kChannelCount]; // units: dBm * 2
    float ChannelRssi[kChannelCount]; // dBm

protected:
    RawSerialPort Serial;
    bool InConfigMode = false;
    int Baudrate = 9600;
    uint16_t TransmitAddress = kMonitorAddress;
    uint16_t CurrentAddress = kMonitorAddress;

    // Receive() data goes here
    static const int kRecvBufferBytes = 240;
    uint8_t RecvBuffer[kRecvBufferBytes];
    //uint64_t RecvStartUsec = 0; // Used to avoid blocking short messages
    int RecvOffsetBytes = 0;

    bool EnterConfigMode();
    bool EnterTransmitMode();

    bool SetAddress(uint16_t addr);

    bool WriteConfig(int offset, const uint8_t* data, int bytes);

    bool ReadAmbientRssi(uint8_t& rssi);

    bool WaitForResponse(int minbytes);

    bool FillRecvBuffer();
};


} // namespace lora
