# .rip stream server

## Packets specification

### ClientHello
    [0x00]

### TrackMetadata
    [0x01]
    [[total time: 4 bytes]
     [[length: 2 bytes] [track name: length]]
     [[length: 2 bytes] [artist: length]]
     [[length: 2 bytes] [album: length]]]

### TrackData
    [0x02]
    [playback time: 4 bytes]
    [[length: 4 bytes] [dfpwm data: length]]

## `.rip` format specification
    [0x72 0x69 0x70]
    [[[length: 2 bytes] [track name: length]]
     [[length: 2 bytes] [artist: length]]
     [[length: 2 bytes] [album: length]]]
    [[length: 4 bytes] [dfpwm data: length]]

