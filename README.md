# Thermit

## Overview
Thermit is a file transfer protocol, designed for embedded devices. In ISO-OSI model concept, it implements the *Transport Layer*. 
The protocol creates a reliable point-to-point connection between *master* and *slave* instances. Thermit works on top of any communication media, such as UART. By design, it does not require full duplex media.
Although the protocol uses concept of *files* as transferable units, the files can be understood as any kind of user data packets.
The project uses C language for portability reasons, but it allows dynamic construction/destruction of instances due to its object oriented design. The IO interface configuration allows mocking the interface functions when needed, especially for testing purposes.

## Features
- automatic negotiation for optimal parameter set
- fragmentation support
- resending of lost packets
- asynchronous data transfer: no waiting for ACKs after each data packet
- supports burst transfer
- automatic burst size adaptation (TODO)
- automatic re-synchronization after communication loss
- session keep-alive management
- uses 16bit CRC on both frame and file level

## Interfaces
The interface functions are configurable, i.e. there can be multiple Thermit instances using different communication devices independently.
- File IO: user data is accessed as files
- Device IO: generic communication device interface for accessing the communication line
- Time IO: generic millisecond timestamp must be readable from the system


## Limitations
### Maximum transferable size
The transferred file is fragmented into *chunks*. The current implementation uses 8 bit index for chunks, which means, it supports 250 of them. Therefore, the maximum supported file size is dependent on native transferable unit on lower layer. This applies especially on packet-based communication lines, but even on the non-packet based lines, the maximum Thermit payload is limited to 255 bytes. That defines the maximum transferable file size to 255*250 = 63750 bytes ~ 62kBytes.
This limitation could be worked away by defining the ChunkID and Length information as 16bit fields, but that would make the communication link less efficiently used.
### File name length
Due to the design goal set on embedded devices, the file name is not considered as an important part. Therefore, the name length is limited to 32 bytes and it supports only basic English characters. There is no concept of folders in Thermit.

## Usage
### Construction
### Stepping
### Destruction




