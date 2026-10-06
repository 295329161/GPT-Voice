# Local integration changes

Upstream: chmorgan/esp-audio-player 1.0.7 (Apache-2.0; see LICENSE).

- Skip validated ID3v2 tag lengths before decoding, including cover art.
- Advance at least one byte after a false sync / invalid frame header.
- Preserve split sync words when refilling the input buffer.
- Yield when no audio was decoded to keep malformed files from starving tasks.
- Stop playback on an output-driver error instead of ignoring it.

These fixes were motivated by the MP3 already present on the user's SD card.

1.0.0 project audit additions:
- STOP from PAUSE transitions to IDLE after closing the file.
- PCM16 RIFF parser bounds every chunk, handles metadata padding and extended fmt, validates rates/channels/alignment, and stops at the data boundary.
- Detect MPEG 1/2/2.5 Layer III with or without CRC.
- Host regression tests cover malformed WAV inputs and MP3 signature variants.
