# LoRa FTP : Broadcast File Transfer

File transfer between Raspberry Pis using the LoRa Pi HAT from Waveshare.


## Why Is This Useful?

LoRa is mostly a unidirectional protocol designed for sensors, which are periodically reporting measurements.

It takes about a second to switch from send to receive mode on this hardware, without canceling the last outgoing message.  This means that traditional file transfer protocols involving message acknowledgements are not suitable.

Instead, this project uses the http://wirehairfec.com/ library to stream data efficiently.  The file sender continously sends.  Multiple receivers can join the stream at any time and start receiving the offered file.


## Ingredients

2x Waveshare Pi HATs
https://www.waveshare.com/wiki/SX1262_915M_LoRa_HAT

2x Raspberry Pi 4B:
https://www.raspberrypi.org/


## Quick Start

Set up the Pis and connect the hats.

Configure the jumpers as shown here:

![alt text](https://github.com/catid/loraftp/raw/master/docs/waveshare_dips.jpg "DIP switch settings for LoRa HAT")

Clone the repo on both devices and build it:

```
    sudo apt install g++ cmake pigpio
    git clone https://github.com/catid/loraftp
    cd loraftp
    mkdir build
    cd build
    cmake ..
    make -j4
```

One of the devices receives the file:

```
    sudo ./loraftp_get
```

The other device runs the file sender:

```
    sudo ./loraftp_send document.txt
```

This will place the file in the same folder as `loraftp_get`.


## Credits

Software by Christopher A. Taylor mrcatid@gmail.com

Please reach out if you need support or would like to collaborate on a project.
