# Local integration changes

Upstream: chmorgan/esp-audio-player 1.0.7 (Apache-2.0; see LICENSE).

- Skip validated ID3v2 tag lengths before decoding, including cover art.
- Advance at least one byte after a false sync / invalid frame header.
- Preserve split sync words when refilling the input buffer.
- Yield when no audio was decoded to keep malformed files from starving tasks.
- Stop playback on an output-driver error instead of ignoring it.

These fixes were motivated by the MP3 already present on the user's SD card.
