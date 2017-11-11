# .rip stream server

## Opcodes specification

### Packet
    [[opcode number: 1 byte] 
     [packet length: 2 bytes]
     [packet data: packet length]]

### ClientHello
    0x00: [] no-length

### TrackMetadata
    0x01: [[total time: 4 bytes]
           [[length: 2 bytes] [track name: length]]
           [[length: 2 bytes] [artist: length]]
           [[length: 2 bytes] [album: length]]]

### TrackData
    0x02: [[4 bytes: playback time] [dfpwm data: packet length - 4 bytes]]

## `.rip` format specification
    [[0x72 0x69 0x70]
     [[[length: 2 bytes] [track name: length]]
      [[length: 2 bytes] [artist: length]]
      [[length: 2 bytes] [album: length]]]
     [[length: 4 bytes] [dfpwm data: length]]]

