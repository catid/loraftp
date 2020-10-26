# This is a work in progress (doesn't work yet)

# LoRa FTP

File transfer between two Raspberry Pis using the LoRa Pi HAT from Waveshare.


## Why Is This Useful?

LoRa is mostly a unidirectional protocol designed for sensors, which are periodically reporting measurements.

It takes about a second to switch from send to receive mode on this hardware, without canceling the last outgoing message.  This means that traditional file transfer protocols involving message acknowledgements are not suitable.

Instead, this project uses the http://wirehairfec.com/ library as a fountain code to stream data without a backchannel.


## Ingredients

2x Waveshare Pi HATs
https://www.waveshare.com/wiki/SX1262_915M_LoRa_HAT

2x Raspberry Pi 4B:
https://www.raspberrypi.org/


## Quick Start

Set up the Pis and connect the hats.

Configure the DIP switches as shown here:

![alt text](https://github.com/catid/loraftp/raw/master/docs/waveshare_dips.jpg "DIP switch settings for LoRa HAT")

Clone the repo on both devices and build it:

```
    sudo apt install g++ cmake pigpio
    git clone https://github.com/catid/loraftp
    cd loraftp
    mkdir build
    cmake ..
    make -j
```

One of the devices receives the file:

```
    ./loraftp_get
```

The other device runs the file sender:

```
    ./loraftp_send document.txt
```

This will place the file in the same folder as loraftp_get.


## Credits

Software by Christopher A. Taylor mrcatid@gmail.com

Please reach out if you need support or would like to collaborate on a project.
