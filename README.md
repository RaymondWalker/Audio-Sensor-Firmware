# ESP32 Raw I2S Sensor
The esp will stream raw uncompressed audio form an I2S Microphone ( SPH0645 ) over wifi through a TCP Socket. 

The data is sent as a raw 32 bit signed integer.
The server must read exactly 32 bytes at a time and unpack them as 8 signed integers.

Packet size: 32 Bytes
Sample Depth: int32_t
Samples per packet: 8 samples
Endianness: Little Endian

Byte Layout:
| Byte 0-3  | Byte 4-7  | Byte 8-11 | ... | Byte 28-31 |
|-----------|-----------|-----------|-----|------------|
| Sample 1  | Sample 2  | Sample 3  | ... | Sample 8   |
| (int32)   | (int32)   | (int32)   | ... | (int32)    |

https://www.digikey.com/en/maker/tutorials/2023/what-is-the-i2s-communication-protocol

https://www.adafruit.com/product/3421
