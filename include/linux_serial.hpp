// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

/*
    Serial port library for Linux

    RawSerialPort: Binary interface to serial connected devices.
*/

#pragma once

#include <stdint.h>

namespace lora {


//------------------------------------------------------------------------------
// Tools

// Converts from a real speed like 9600 bps to the constant B9600.
int BaudrateToBaud(int baudrate);


//------------------------------------------------------------------------------
// RawSerialPort

class RawSerialPort
{
public:
    ~RawSerialPort()
    {
        Shutdown();
    }

    // Returns true for success and false for failure.
    bool Initialize(const char* port_file, int baudrate);
    void Shutdown();

    void Flush();

    // Returns number of bytes in send queue
    int GetSendQueueBytes();

    // Returns true for success and false for failure.
    bool Write(const void* data, int bytes);

    // Returns the number of bytes available to read.
    // Returns -1 on error.
    int GetAvailable();

    // Returns number of bytes read (may be 0).
    // Returns -1 on error.
    int Read(void* data, int bytes);

protected:
    int fd = -1;
};


} // namespace lora
