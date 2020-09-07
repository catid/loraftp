// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include <stdint.h>

namespace lora {


//------------------------------------------------------------------------------
// Constants

// Maximum Send() size
static const int kPacketMaxBytes = 240;


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
        Channel 0..83 = 850.125 + CH * 1MHz
        Address is the node address or 0xffff for monitor mode
        LBT = Listen Before Transmit
    */
    bool Initialize(
        int channel,
        uint16_t addr = 0xffff/*broadcast*/,
        bool lbt = false);
    void Shutdown();

    // Send up to 240 bytes at a time
    void Send(const uint8_t* data, int bytes);

    // Returns -1 on error
    // Returns 0 if no data
    // Otherwise returns number of bytes written
    int Receive(uint8_t* buffer, int buffer_bytes);

protected:
    int fd = -1;
};


} // namespace lora
