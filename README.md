# CrossPoint Reader

Firmware for the **Xteink X4** e-paper display reader (unaffiliated with Xteink).
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

CrossPoint Reader is a purpose-built firmware designed to be a drop-in, fully open-source replacement for the official 
Xteink firmware. It aims to match or improve upon the standard EPUB reading experience.

![](./docs/images/cover.jpg)

## Motivation

E-paper devices are fantastic for reading, but most commercially available readers are closed systems with limited 
customisation. The **Xteink X4** is an affordable, e-paper device, however the official firmware remains closed.
CrossPoint exists partly as a fun side-project and partly to open up the ecosystem and truely unlock the device's
potential.

CrossPoint Reader aims to:
* Provide a **fully open-source alternative** to the official firmware.
* Offer a **document reader** capable of handling EPUB content on constrained hardware.
* Support **customisable font, layout, and display** options.
* Run purely on the **Xteink X4 hardware**.

This project is **not affiliated with Xteink**; it's built as a community project.

## Features & Usage

- [x] EPUB parsing and rendering (EPUB 2 and EPUB 3)
- [x] Image support within EPUB
- [x] Saved reading position
- [x] File explorer with file picker
  - [x] Basic EPUB picker from root directory
  - [x] Support nested folders
  - [ ] EPUB picker with cover art
- [x] Custom sleep screen
  - [x] Cover sleep screen
- [x] Wifi book upload
- [x] Wifi OTA updates
  - [x] Settings → System → Check for updates
  - [x] Download progress and auto-reboot after install
  - [x] Installs latest `firmware.bin` from this fork’s releases
- [x] KOReader Sync integration for cross-device reading progress
- [x] Configurable font, layout, and display options
  - [ ] User provided fonts
  - [ ] Full UTF support
- [x] Screen rotation
- [x] UI themes (Settings → Display → UI Theme)
  - [x] Classic, Lyra, Lyra Extended
  - [x] CatPose (welcome header, recent cover strip, 2×2 home menu; default on fresh install)
- [x] Games (Home → Games)
  - [x] TRETIS (falling-block puzzle)
  - [x] Sudoku
  - [x] Cat Run (side-view auto-runner)
  - [x] High scores with pause/resume via SD saves
- [x] Lock screen
  - [x] Optional 4-digit PIN on boot/wake
  - [x] Custom message line 1 and line 2
- [x] Performance-focused build for constrained hardware
  - [x] Size-optimized compile (`-Os`)
  - [x] Production release logging trimmed (`LOG_LEVEL=0`)
  - [x] Single framebuffer + SD chapter cache (~380KB RAM)

Multi-language support: Read EPUBs in various languages, including English, Spanish, French, German, Italian, Portuguese, Russian, Ukrainian, Polish, Swedish, Norwegian, [and more](./USER_GUIDE.md#supported-languages).

See [the user guide](./USER_GUIDE.md) for instructions on operating CrossPoint, including the
[KOReader Sync quick setup](./USER_GUIDE.md#365-koreader-sync-quick-setup).

For more details about the scope of the project, see the [SCOPE.md](SCOPE.md) document.

## Thai language support

This firmware includes targeted support for reading Thai EPUBs on constrained hardware:

- **Word breaking** — An embedded Thai dictionary (DAWG, built from a community word list) supplies word boundaries for hyphenation and paragraph layout so long Thai runs wrap cleanly.
- **User dictionary** — Optional entries on the SD card at `/crosspoint/thai_dict.txt` (one word per line; lines starting with `#` are comments) are loaded at boot and merged into segmentation.
- **EPUB layout** — The HTML parser and `ParsedText` pipeline segment Thai runs and insert layout boundaries so line breaking and justification work without awkward gaps.
- **Rendering** — Thai combining marks (vowels, tone marks, Sara Am) are positioned and stacked in the glyph renderer; Thai cluster logic supports measurement and breaks.

These components live in the EPUB engine (`lib/Epub/`), hyphenation (`ThaiWordBreaker`), and renderer (`GfxRenderer`). They are distinct from community forks that add separate Thai UI (for example keyboard layouts); see the acknowledgement below.

## CatPose theme

**CatPose** is the default home UI on a fresh install (change anytime under **Settings → Display → UI Theme**):

- Welcome header with battery and time
- Horizontal recent-book cover strip (focus cover larger; **View All** opens the file browser)
- Compact 2×2 menu cards: Browse, Transfer File, Games, Settings

## Games

Open **Home → Games** to play:

- **TRETIS** — Falling-block puzzle tuned for e-ink refresh; pause saves progress to the SD card.
- **Sudoku** — Medium 9×9 puzzles ranked by completion time.
- **Cat Run** — Side-view auto-runner: pick a cat color, then jump/duck through collectibles, hazards, and a late-run boss phase.
- **High Scores** — Shared scoreboard across games; unfinished sessions can be resumed from the Games menu (SD saves).

## Lock screen

Optional PIN gate shown on boot and wake when enabled:

1. Enable **Lock Screen** in System settings (or via the web settings page).
2. Open **Lock Screen Setup** to set a 4-digit PIN and optional **message line 1** / **line 2** (shown above the keypad).
3. Enter the PIN to unlock; **Back** on the lock screen returns the device to deep sleep.

## Performance

This fork keeps the device responsive within the ESP32-C3’s ~380KB usable RAM:

- Compile with size optimization (`-Os`) for a smaller firmware footprint.
- Production (`gh_release`) builds use error-only logging (`LOG_LEVEL=0`).
- Single 48KB framebuffer mode (`EINK_DISPLAY_SINGLE_BUFFER_MODE`) and aggressive SD chapter caching (see [Internals](#internals)) avoid large DRAM allocations during reading.

## OTA updates

Update on the device over WiFi (no USB required):

1. Open **Settings → System → Check for updates**.
2. Connect to WiFi; the device compares the current version to the latest [GitHub release](https://github.com/teerarattanapon/crosspoint-reader/releases) for this fork.
3. Confirm to download `firmware.bin` (progress is shown), validate the image, then auto-reboot after about 10 seconds.

RC builds (`*-rc`) can update to the matching stable release when that tag is the latest on GitHub.

For USB/web flashing instead, see [Installing](#installing) below.

## Installing

### Web (latest upstream firmware)

1. Connect your Xteink X4 to your computer via USB-C and wake/unlock the device
2. Go to https://xteink.dve.al/ and click "Flash CrossPoint firmware"

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Web (this fork — specific firmware version)

Binaries for **this fork** (Thai support, CatPose theme, games, lock screen, on-device OTA, performance tweaks) are published on the
[releases page](https://github.com/teerarattanapon/crosspoint-reader/releases).

1. Connect your Xteink X4 to your computer via USB-C
2. Download the `firmware.bin` file from the [release](https://github.com/teerarattanapon/crosspoint-reader/releases) you want (for example **1.2.4**)
3. Go to https://xteink.dve.al/ and flash the firmware file using the "OTA fast flash controls" section

To revert back to the official firmware, you can flash the latest official firmware from https://xteink.dve.al/, or swap
back to the other partition using the "Swap boot partition" button here https://xteink.dve.al/debug.

### Manual

See [Development](#development) below.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

CrossPoint uses PlatformIO for building and flashing the firmware. To get started, clone this repository:

```
git clone --recursive https://github.com/teerarattanapon/crosspoint-reader

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Flashing your device

Connect your Xteink X4 to your computer via USB-C and run the following command.

```sh
pio run --target upload
```
### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```
after that run the script:
```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```
Minor adjustments may be required for Windows.

## Internals

CrossPoint Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only
has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based
on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the 
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:


```
.crosspoint/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
└── epub_189013891/
```

Deleting the `.crosspoint` directory will clear the entire cache. 

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book
file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Contributing

Contributions are very welcome!

If you are new to the codebase, start with the [contributing docs](./docs/contributing/README.md).

If you're looking for a way to help out, take a look at the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas).
If there's something there you'd like to work on, leave a comment so that we can avoid duplicated effort.

Everyone here is a volunteer, so please be respectful and patient. For more details on our goverance and community 
principles, please see [GOVERNANCE.md](GOVERNANCE.md).

### To submit a contribution:

1. Fork the repo
2. Create a branch (`feature/dithering-improvement`)
3. Make changes
4. Submit a PR

---

CrossPoint Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.

Huge shoutout to [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader), which was a project I took a lot of inspiration from as I
was making CrossPoint.

ขอบคุณโครงการ **CrossPoint Halo 2 (HALO)** ที่ [crosspoint-halo2-custom](https://github.com/kocha01/crosspoint-halo2-custom) โดย [@kocha01](https://github.com/kocha01) สำหรับการขยายประสบการณ์ภาษาไทยและ UI เพิ่มเติมบนพื้นฐาน CrossPoint Reader รวมถึง [รีลีส CrossPoint Halo 2 UI — Beta 2](https://github.com/kocha01/crosspoint-halo2-custom/releases/tag/CrossPoint_Halo_2_UI_Beta_2) ที่พัฒนาแป้นพิมพ์ไทยและความสามารถอื่น ๆ แยกจากรีโปนี้
