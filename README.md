# LoRa FTP

File transfer between two Raspberry Pis using the LoRa Pi HAT from Waveshare.


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
    sudo apt install g++ cmake
    git clone https://github.com/catid/loraftp
    cd loraftp
    mkdir build
    cmake ..
    make -j
```

One of the devices hosts the file server:

```
    ./loraftp_server
```

The other device runs the file client.  To upload a file to the server:

```
    ./loraftp_client ../docs/waveshare_dips.jpg
```

This will place the file in the same folder as the server.


## Protocol

The client first compresses the file using Zstd to make sure that it is as small as possible.

The client and server both configure the radio for the highest performance bandwidth mode.

Before starting, both server and client check all 84 channels for noise power.
The HAT supports 84 channels: 850.125 + (channel * 1MHz).  Recall that LoRa uses 500 kHz spectrum bandwidth.


#### Client => Server OFFER (Packet # 0, 90+X bytes):

The client and server rendezvous on channel 0, by the server listening for an OFFER from the client.
The client transmits its 84 byte noise power data to the server in this OFFER.

```
    0x00 0xfe 0xad 0x01 <Channel RSSI (84 bytes)> <File length (4 bytes)> <Filename Length(1 byte)> <Filename (X bytes)>
```


#### Server => Client ACCEPT (Packet # 1, 86 bytes):

The server replies with the channel selection, and then both radios are configured for the lowest noise channel.
The server enables Listen Before Transmit (LBT) mode, and the client disables LBT.

```
    0x01 0xad 0xfe <Channel Selection 1..83 (83 byte)>
```


#### Client => Server DATA (2..240 bytes):

```
    <Chunk Bit (1 bit)> <ID (7 bits)> <Data (1..239 bytes)>
```

Chunk Bit starts at 0 and flips by 1 for each chunk.
Chunk ID increments from 0..255.
When the receiver indicates it has received all of Chunk Bit 0, then
Chunk Bit 0 will be reused as next chunk ahead of Chunk Bit 1.

Each packet has a 16-bit CRC being checked by the radio, and there's some kind of encryption going on I didn't look into.
But for long files this might fail and allow bad data through.  So to combat this, each ~30 KB chunk of the file is
preceeded by a 64-bit hash of the chunk and a sync code.

The first 12 bytes of each Chunk: `0xfe 0xad 0x00 0x00 <Hash (8 bytes)>`

The final chunk and DATA packet in a file can be shorter and is implied by the initial file length in OFFER packet.


#### Server => Client ACK (Packet # 3, 240 bytes):

The fastest we can send data is about 60 KB/s with LoRa, so every 1 second the server checks
to see what data was received, and what data was lost on the wireless link.

After 1 second the server side starts sending an ACK to the client letting it know what was received.
The client resumes sending data once it receives an ACK, which causes the server to stop sending its ACKs.

```
    0x03 <Base Chunk ID (4 bytes)> <ID Count (8 bits)> Repeated: [ <Chunk Bit (1 bit)> <ID (7 bits)> ]
```


When the final chunk is acknowledged, the client sends a DISCO packet twice to indicate that it has stopped sending.

#### Client => Server DISCO (Packet #4, 6 bytes):

```
    0x04 0xff 0xff 0xff 0xff 0x00
```

The server will wait for 1 second for this message, and return to OFFER mode if nothing is received in case DISCO messages were dropped.


## Future Work

This is using the simplified API exposed by the Waveshare HAT to the SX1262 LoRa chip, which cannot configure all the features of the chip.

It does not support data security (key management) or time-division channel sharing or multi-hop relaying.


## Credits

Software by Christopher A. Taylor mrcatid@gmail.com

Please reach out if you need support or would like to collaborate on a project.
