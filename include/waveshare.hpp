// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#pragma once

#include <stdint.h>

/*
    M0 = 22
    M1 = 27
    MODE = ["BROADCAST_AND_MONITOR","P2P"]

    CFG_REG = [b'\xC2\x00\x09\xFF\xFF\x00\x62\x00\x17\x03\x00\x00',
            b'\xC2\x00\x09\x00\x00\x00\x62\x00\x17\x03\x00\x00']
    RET_REG = [b'\xC1\x00\x09\xFF\xFF\x00\x62\x00\x17\x03\x00\x00',
            b'\xC1\x00\x09\x00\x00\x00\x62\x00\x17\x03\x00\x00']

    GPIO.setmode(GPIO.BCM)
    GPIO.setwarnings(False)
    GPIO.setup(M0,GPIO.OUT)
    GPIO.setup(M1,GPIO.OUT)

    GPIO.output(M0,GPIO.LOW)
    GPIO.output(M1,GPIO.HIGH)

    ser = serial.Serial("/dev/ttyS0",9600)
    ser.flushInput()

    if ser.isOpen() :
        print("It's setting BROADCAST and MONITOR mode")
        ser.write(CFG_REG[0])
    while True :
        if ser.inWaiting() > 0 :
            time.sleep(0.1)
            r_buff = ser.read(ser.inWaiting())
            if r_buff == RET_REG[0] :
                print("BROADCAST and MONITOR mode was actived")
                GPIO.output(M1,GPIO.LOW)
                time.sleep(0.01)
                r_buff = ""
            if r_buff != "" :
                print("monitor message:")
                print(r_buff)
                r_buff = ""
*/

namespace lora {


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

protected:
    int fd = -1;
};


} // namespace lora
