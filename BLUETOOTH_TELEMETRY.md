# Playback format telemetry (candidate, issue 95)

This work is not yet accepted on the final R1 image. It supplements the Bluetooth
crash and position-retention fixes; it does not replace their hardware checks.

## Values and ownership

- The decoder supplies the source bitrate. A declared bitrate takes precedence.
  When absent, a complete regular file uses its size and full duration once at
  open. Growing caches and unknown durations remain unknown. CUE slicing happens
  after this calculation. UI refresh never reads file metadata for this value.
- LDAC supplies the last processed frame's bitrate through `ldacBT_get_bitrate()`.
  Native stock-library probes confirm this getter uses kbit/s: at 44.1 kHz its
  fixed tiers are 909/606/303, rather than the nominal 990/660/330 mode labels.
- `org.bluealsa.PCM1.CodecBitrate` is an optional read-only `uint32` in **bit/s**.
  Zero means unknown or no active encoder. Existing clients may ignore it;
  existing daemons without it remain supported without a fabricated number.
- The additive BlueALSA 5.0 patch reads LDAC after a successful encoded transport
  write and before ABR chooses the following frame. Queue-pressure `errno` is
  consumed first. Notifications occur only when the value changes, including
  clearing it on encoder teardown. Libraries and ALSA plugins stay unchanged.
- Sonix caches values per active output peer. Endpoint removal/replacement,
  invalidated properties and service-owner retirement clear them. A property
  snapshot cannot overwrite a newer signal. Bitrate-only signals do not wake
  the routing worker; the UI performs no D-Bus calls.
- Wired and USB output formats come from the successful ALSA configuration:
  rate, significant bits (format width as fallback), channels and DoP state.
  The cache is tied to the open selected output. These are digital transport
  parameters, not a measurement of analog precision or sound quality.

## Presentation and verification

The existing full-player signal chain shows source bitrate and measured LDAC
bitrate. Detailed source depth/rate remain in its disclosure. A wired conversion
adds an output such as `24/96 -> PCM 16/48`; an unchanged ordinary wired format
does not add a redundant output. USB and DoP retain their identity. Unknown
values are omitted. Other codecs do not gain guessed bitrate telemetry.

Regression checks cover actual source functions and PCM messages: growing-file
exclusion, numeric bounds, unavailable output, format changes, DoP, old peers,
two headsets, late snapshots and owner/endpoint retirement. Run the standalone
`btstack_bitrate_test.c`, `audio_output_format_test.c`, and
`python3 tests/playback_format_test.py`. Backend source, complete linker/ABI,
native signal/CPU/idle checks and final image extraction must also pass.

Backend patch: `sonix-player/tools/bluealsa/bluealsa-5.0-codec-bitrate.patch`,
against BlueALSA commit `1935d6dcb8975f2d7a51aaafe61538d157224623` (5.0.0).
LTO archives require the matching `gcc-ar` and `gcc-ranlib`, not plain binutils
without its LTO plugin. No binary, key or stock image belongs in Git.
