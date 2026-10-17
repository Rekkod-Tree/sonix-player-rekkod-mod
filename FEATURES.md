# Features

## R1 USB storage handoff

- Kernel USB events wake the existing watcher so brief enumeration windows are
  not missed. The current controller, host and card ownership are checked again
  before export; an event alone never authorizes access to the card.
- A stalled connection gets one guarded restart of the empty USB reader. Card
  services and ordinary unmount checks remain mandatory; no forced or lazy
  unmount is used.
- Spawned helpers do not inherit open music files or sockets across exec.
  Bluetooth services can therefore outlive the player without retaining an old
  track and indefinitely blocking card release.

## Sonix 1.3 adaptations for Graphite 2

- Persistent listening history in the queue menu, including stored names for
  lazy streaming entries and a bounded row pool.
- Queue selection/removal with Undo and grip-based reordering of upcoming
  tracks. The current track stays fixed. The add policy can ask each time,
  play next or append; explicit swipe actions retain their meanings.
- Direct RSS podcast feeds and OPML fallback with listening state retained;
  native and Ogg FLAC internet radio.
- LRCLIB lookup and explicit local `.lrc` saving; JPEG/PNG/GIF viewing in Files.
  Searches and image decoding run outside the GUI thread.
- Numeric PEQ Gain/Q entry and a metronome under More, using Graphite controls.
- Automatic network time with optional HTTPS zone discovery (off by default),
  Czech keyboard/translation, and 1% quick-panel brightness minimum.
- R1 recovery preparation wizard with verified backups and separate repair
  confirmation. Startup never automatically patches recovery.

The existing R1 audio-driver pair is retained: upstream's new PCM stop fade
does not distinguish DoP payload. S24 support is preserved. Charge-only startup,
RTC wake and gauge changes require model-specific hardware acceptance; host
fixtures alone do not establish charging or analog-output correctness.

---

## Supported audio formats



`.wav` `.flac` `.mp3` `.ogg` `.opus` `.m4a` `.m4b` `.mp4` `.aac` `.wv` `.wvc` `.ape` `.dsf` `.dff` `.aif` `.aiff` `.aifc` `.caf`

**CUE sheets** split a single ripped disc into up to 99 tracks, taking the format from the file the sheet points at. WAVs carrying their own RIFF markers work the same way.

**WAV and AIFF tags** are read from the ID3 chunk taggers add to them, so titles in any script and the cover come through; a WAV's own INFO list fills in whatever that leaves empty.

**Metadata compatibility** includes chained, compressed and data-length-indicator
ID3 frames, UTF-8 text from older taggers, APEv2 tags on MP3, AIFF NAME/AUTH and
CAF info. Native FLAC skips cover blocks while scanning, retains STREAMINFO and
uses leading ID3/trailing ID3v1 only to fill missing fields. Opus R128 and explicit
ReplayGain keep their priority. For supported named formats, the file signature
can correct a misleading extension; ALAC inside MP4 keeps its original bit depth.
A CUE referencing a converted WAV can find a neighboring FLAC, WavPack or APE.

## Audio

- **Volume** - The same volume curve as the stock player is used.
- **Remember Volume** Four levels remembered separately, one per output: 3.5 mm, 4.4 mm, USB-C, Bluetooth,
  and over Bluetooth one per pair of headphones.
- **Graphic EQ** - 10 bands at 31 Hz to 16 kHz, ±12 dB. Ten built-in presets and
  as many of your own as you like.
- **Parametric EQ** - 10 bands, each with frequency 20 Hz to 20 kHz, gain
  ±15 dB in tenths, Q from 0.10 to 10.00, and five shapes: peak, low shelf,
  high shelf, low-pass, high-pass. Preamp −15 to +6 dB. The page draws the real
  response, computed from the coefficients that are running. Presets are
  Equalizer APO files in the `PEQ` folder of the card: the `ParametricEQ.txt`
  that AutoEq publishes for your headphones loads as it is, and a preset saved
  on the player opens in APO.
- **MSEB** - HiBy's MageSound tuning, matched to the stock player: 10
  characteristics driven by 13 filters, with the slider travel selectable
  between 20, 40 and 100.
- **Soundfield** - mid/side width from mono to twice natural.
- **Channel balance** - ±20 dB in half-decibel steps.
- **Crossfeed** - level, cutoff and delay, all adjustable.
- **ReplayGain** - off, track or album, read from the tags.
- **Fade** - one to twelve seconds at the track boundary.
- **Gapless** - the PCM device stays open between tracks of the same shape.
- **DSD** - DoP by default, with nothing in the chain allowed to touch the
  samples; or converted here to 176.4 kHz PCM. DoP falls back to conversion by
  itself when the device refuses the rate.
- **DAC controls** - four digital filters, DRE, NOS, high gain, and DSD gain
  compensation from 0 to 6 dB.
- **Line out** - drives the jack at a fixed level for an amplifier, refuses
  volume changes while on, and puts your level back on the way out.
- **USB DAC** - the player becomes a USB sound card, 32 kHz to 384 kHz.
- **USB audio out** - a DAC or USB-C headphones driven with the player as host.
- **In-line remote** - the three-button cable remote: one click play/pause, two
  next, three previous.

## Library

- **Sorting across alphabets** - a collation that groups by script, folds case and accents, and skips leading articles unless you turn that off.
- **A–Z strip** - shown when scrolling a list.
- **Search** across tracks, albums and artists.
- Search results reflow around the on-screen keyboard. A compact horizontal row switches between All, Tracks, Albums and Artists, including categories with no matches. The six recent queries remember their category, can be repeated or removed individually, and ignore blank submissions and duplicate queries.
- **Albums with the same name stay apart** - an album is its name and whose it is: the album artist, the artist when there is none, and the folder when there is neither.
- **Quality badges** under a title: lossy, CD, Hi-Fi, DSD.
- **Playlists** - create, rename, reorder, delete. A long press on a row of All tracks, Albums, Artists or Album artists starts a selection, and the chosen tracks, albums or artists go into the queue, the favourites or a playlist together. Inside a playlist or the favourites the selection also takes the chosen tracks out, after asking. In the file browser the same works on tracks and folders, a folder bringing everything under it. A backup writes an extended M3U to the card that other players can read; import reads one back, resolves missing entries by file name, and reports what it found.
- **Playback modes** - normal, repeat all, repeat one, shuffle, and a continuous shuffle that deals a fresh order each round.
- **Album and folder chaining** - the next album starts when one ends, or the next folder in a depth-first walk when you came in through the browser.
- **The queue survives a reboot**, with positions, and so does the volume.

## Now playing

- Artwork edge to edge across the top, and the same picture flipped, blurred and dimmed behind the controls.
- **Alternative layout** - the progress bar becomes a **waveform**
- **Cover Flow** - an album carousel, swiped up from the bottom of the Music page.
- **Queue**, **Details** and, while an audiobook is loaded, **Chapters**.

## Streaming

- **Offline spoken audio** - Yandex Music audiobooks and Zvuk books download
  into the configured Audiobooks folder as complete, ordered books. A book is
  published only after every chapter and its required metadata are present;
  a failed download leaves no partial book in the shelf. The local library
  keeps chapter position, bookmarks and finished state across rescans.
- **Service podcasts** - podcast shows use album-style lists with a Podcast
  badge. Episodes can be saved individually into `Podcast/`, with service IDs
  separating equal titles. Local listening progress survives repeat downloads
  and filename changes. An entire podcast archive is never downloaded by a
  show-level action; streaming progress is not yet shared with local progress.
- **Lazy queue metadata** - Yandex Music and Zvuk resolve the playing track,
  Up next and queue labels from loaded service details before the audio file
  exists. Missing current/next details are fetched in the background; late
  titles update without restarting playback or resetting mini-player progress.
- **List artwork (opt-in)** - disabled by default: Yandex Music and Zvuk keep
  colour placeholders without thumbnail requests, worker, timer or row-art pool.
  Explicit `[streaming] list_art=1` enables small covers for visible track and
  album rows only, with no disk cache or whole-library preload. Placeholders
  remain during loading/errors; numbered album tracks keep their numbers.
- **Cache artwork isolation** - shared Yandex/Zvuk caches use each track's own
  cover, rather than another track's picture. Image writes publish atomically
  after bounded validation and rechecking card ownership.
- **Tidal** - sign in with the code on screen or with the QR-Code; Low, High, CD and Hi-res quality. Search, albums, artists, playlists, favourites and the featured lists. Favourites and playlists can be managed from the player.
- **Qobuz** - sign in with the account; MP3 320, FLAC 16/44.1, 24/96 and 24/192. Search, albums, artists, playlists, favourites and the featured lists. Favourites and playlists can be managed from the player.
- **Podcasts** - the Podcast Index catalogue: search, trending, follow up to 100
shows kept on the device.
- **Radio** - the radio-browser.info directory by language, country or genre,
plus search, favourites, recently played, and **My stations** read from a
`radio.txt` you write yourself in the card root. MP3 and AAC, HTTPS with real
certificate checking, and HLS.


## Wireless

- **Bluetooth out** - the whole stack is brought up by the player rather than by
  the firmware's scripts. Pair, connect, forget; the codec list comes from what
  the connected device announces and the best is chosen at each connection.
- **AirPods** - battery per earbud and for the case, model detection, and noise
  control: off, cancellation, transparency, adaptive.
- **Bluetooth in** - a phone or laptop connects and the player becomes its
  speaker, decoding through its own DAC. The incoming codec, rate and depth are
  shown.
- **AirPlay** - the player appears as an AirPlay target.
- **DLNA** - the player appears as a renderer.
- **Wi-Fi transfer** - a browser on the same network gets the card: upload
  files and whole folders, rename, move, delete, download, show hidden files.
  Reachable at `sonix-transfer.local`.
- **SonixLink** - a control protocol for a phone: the phone fetches the index
  once and queries it locally instead of paging over the wire. Playback state,
  queue, favourites, artwork and commands.
- **USB mass storage** - connect the device to a PC or a Smartphone to manage the files on the SD Card. Export requires a confirmed ordinary unmount; a busy card stays local. Local services resume only after USB releases the card and the filesystem is mounted again. ADB keeps the card local.

## Audiobooks

> Audiobooks go in the `Audiobooks` folder, in any subfolder: `Audiobooks/Author/Series/...` is read as the author and the series when the tags do not say.

- **Library, Series, Authors and Continue** - four sections, each sorted A-Z, Z-A or by date; the library also by recently listened, a series by the number of each book in it. Continue holds the books started and not finished, last listened first; the finished ones are behind the button beside the options.
- **A book is a file or a folder** - an `.m4b`, an `.mp3`, `.opus`, `.ogg` or `.flac` with chapters, or a single audio file is a book; audio files of one folder with the same album tag are one book whose files are its chapters, `CD 1` / `Disc 2` subfolders included, so several books side by side in a folder stay apart.
- **Author and series from the tags** - album artist or artist, `SERIES` / `SERIES-PART` and the movement name and number, in MP4, ID3v2 and Vorbis tags.
- The position is remembered per book, and survives a rescan.
- Chapters from the MP4 container, from ID3 frames, or from the `CHAPTERxxx` comments of Opus, Ogg Vorbis and FLAC.
- The two transport buttons are set independently to 10, 30 or 60 seconds.
- Speed 0.5x to 2.0x with the pitch preserved.
- Stop at the end of a chapter, and rewind a few seconds after a pause.
- **Sleep timer** - set a timer with the desired duration and the playback is paused automatically after it elapses.

## EPUB reader

> `.epub` files need to be inside the `Ebook` folder.

- Support for EPUB 2 and 3.
- Headings, emphasis, lists, quotes, rules, images, alignment and the table of
  contents.
- Text size from 14 to 50, line spacing, word spacing, margins, three reading themes (Paper,
  Sepia, Night) and three page-turn animations.
- **Bookmarks** - double-tap the top of a page; up to 64 per book.
- A **reading bar** along the bottom with chapter, page, progress, battery and
  clock, each one optional.

## Game Boy

> The `.gb` and `.gbc` ROMs need to be inside the `Games` folder, each in a separate folder: `GB` for Game Boy and `GBC` for Game Boy Color.
> The boot ROMS need to be inside the `Games/Bios` folder, dmg_boot.bin for Game Boy cgb_boot.bin for Game Boy Color. (This step is optional).

- Games show their real cartridge names, matched by CRC32 against the libretro
  databases.
- Save states, battery RAM, five palettes, shaders and GBC color
  correction.

## Settings

- **Appearance** - Graphite 2 dark and light surfaces, a Graphite accent, seven colour presets including neutral Gray, and cover accent. Gray adapts to dark/light themes; saved preset indices remain compatible. New configurations start without cover tint; saved choices are retained.
- **Settings controls** - filled selected segments, aligned title/switch rows and full-width explanations. Long Bluetooth battery details open separately without toggling the setting. Every accent has a visible name; date/time controls share the settings geometry.
- **Status bar** - compact 20px type and glyphs, a 24px battery and one neutral ink for normal states. High volume and low battery keep readable warnings; idle radios are dimmed. Local play/pause and sleep indicators are omitted, with their controls retained. Left, centre, right and hidden clocks support both time formats.
- **Playback surfaces** - the control centre and volume overlay share the mini-player's background and foreground, including cover accents. The standard player uses one flat canvas in both themes, also with cover tint; Studio and Waveform remain available.
- **Open shade** - the status bar adopts the control centre's colours while it is open and restores the underlying page/artwork treatment on dismissal. Quick-control captions can use two lines. The compact floating volume pill keeps its existing dimensions.
- **Quiet idle UI** - unchanged quick-control colours and glyphs no longer request new frames. R1 reads touch events on demand while idle, retaining timed reading during contact, holds and scrolling; `[display] touch_events=0` restores timer polling. Other models retain their previous default.
- **Startup identity** - an atmospheric Rekkod Mod splash credits Sonix. Baseline JPEG assets are supplied for both panels and all three boot-theme slots; the startup mechanism is unchanged.
- **Shared navigation** - equal-size tiles with small line icons, consistent list rows and an Accent mini-player showing the track and artist. Missing-art colours stay tied to the album or folder across lists, queue and playback.
- **Prototype fidelity** - 34px prototype tile icons, bottom-aligned titles with reserved description space, live library counts, grouped navigation settings and 88px setting choices. Local builds can supply Segoe UI Regular/Semibold while retaining script fallbacks.
- **Album page** - a horizontal 156px cover header, album details and playback actions. The compact album preference remains available; text-size changes reflow the header and its track list.
- **Album context** - after the album header scrolls away, its title and menu remain available in the fixed header. Browsing another album does not change the playing track's mini-player, transport or flat playback colours.
- **Consistent secondary views** - service line icons, rounded loaded album artwork and a protected scrolling header; coloured file/book/carousel placeholders, 88px carousel/file rows and shared game-setting choices. Fast carousel release uses the latest panel position.
- **List gestures** - swipe a track left to toggle its favourite state, right to play it next; hold for the menu and append-to-queue action. Album swipes and queue removal/reordering keep their existing meanings.
- **Now Playing** - an inset square sleeve, compact right-aligned Up Next, and audio-chain details in the menu. Waveform, Studio, lyrics and existing source controls remain available.
- **Save the current stream** - Zvuk and Yandex Music offer Save to card directly in the playing track's menu, or a checked Saved state. The action retains the track selected when the menu opened and uses the existing download queue. Services without permanent downloads do not show this action.
- **Track metadata** - symmetric inner row padding keeps duration away from the edge. Long artist names shorten before the format and duration, including compact queue rows.
- **Feedback** - two-line Undo toasts pause their timeout while touched; long sheet messages scroll above the action buttons.
- **Control centre and forms** - 64px quick controls with shared colours and readable captions; captions retain tap/hold actions. Playlist and folder naming share the same field and keep the native keyboard.
- **Service recovery** - bounded, scrollable error explanations with fixed Retry/Sign in and saved-track actions. Zvuk and Yandex return to the failed list after sign-in; Zvuk groups library navigation, shows known collection counts and keeps Search in the header.
- **Screen** - brightness, screen-off timeout, double-tap to wake, screensaver,
  and 180° rotation with the touch rotated with it.
- **Screensaver** - choose to show the cover of the current playing track or pictures from the `Screensaver` folder.
  > Pictures need to be exactly 480x720 jpegs non progressive.

- **Power** - standby, LED behaviour, automatic power-off from thirty seconds to
  six hours, and a charge limit from 80 to 100 %.
- **Date and time** - set by hand and written to the RTC, 24-hour clock, and a
  time zone with its own standard and daylight choice.
- **Language** - English, Italiano, Deutsch, Español, Français, Russian, Chinese,
  Japanese. Titles and tags in Korean, Thai and Arabic are shown too, Arabic
  right to left.
- **Remap buttons** - the three media buttons and the two volume buttons, each
  assignable to nothing, play/pause, previous, next, volume up or volume down.
- **Keyboard layout** - six layouts: English, Italian, German, Spanish, French
  and Russian.
- **Screenshots** - under More: volume up and the power key save the screen to
  the card.
- **System** - device, DAC, serial number, card space, OS version, firmware
  update from a `.upt` on the card, and factory reset.
- **Safe update handoff** - checked card sync and ordinary unmount before recovery; errors cancel the restart and restore local services. R1 additionally requires the reviewed read-only recovery repair described in `RECOVERY_R1.md`.
- **Developer options** - unlocked by tapping the build number: ADB, log to the
  card, database log, disabling the software volume attenuation, and a page of
  running processes and memory.
- **Bounded SD logging** - the main player log rotates at 8 MiB and retains one previous file. The normal writer limits output held in memory during startup or USB export to 32 KiB; concurrent fallback writes can briefly exceed that threshold. Explicit diagnostic log destinations keep their existing behavior.
