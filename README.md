# PhobCGCC — USB-C edition

PhobGCC is an open-source Gamecube controller motherboard aiming to make an accessible and consistent controller. The key feature is the use of hall effect sensors instead of potentiometers, which eliminates a wear item. Additonally, it features notch calibration, digital snapback filtering, button remapping, and various trigger configurations.

This fork replaces the GameCube **joybus** output with a **USB-C** transport: the controller enumerates as the official Nintendo GameCube controller adapter (WUP-028 emulation) on port 1, so it can be plugged directly into a Switch over USB-C. Input is emitted on a fixed 8.333 ms **consistency-mode** cadence (NaxGCC model), independent of the host's polling.

## USB-C / consistency mode

- The joybus PIO transport (`joybus.cpp` / `joybus.pio`) has been removed.
- USB is handled by `rp2040/src/usb_gc_adapter.cpp` (TinyUSB HID device) via `enterUsbMode()`, reusing the same `GCReport` as the joybus build.
- PhobVision (hold Z on plug-in) is unchanged.

### Hardware

To physically convert your Phob to a USB-C port, you need the **OlyU** by sean44104: https://github.com/sean44104/OlyU

## LZ button (second Z)

GPIO12 (previously a spare line) is now an **LZ** button input. By default it acts as a second **Z**. Prebuilt firmware images for each mapping are provided in [`uf2_builds/`](uf2_builds/):

| File | LZ acts as |
|---|---|
| `phobcgcc_rp2040_LZ-as-z.uf2` | Z |
| `phobcgcc_rp2040_LZ-as-a.uf2` | A |
| `phobcgcc_rp2040_LZ-as-b.uf2` | B |
| `phobcgcc_rp2040_LZ-as-x.uf2` | X |
| `phobcgcc_rp2040_LZ-as-y.uf2` | Y |
| `phobcgcc_rp2040_no-LZ.uf2` | nothing (LZ disabled) |

To flash, hold Start while plugging in to enter BOOTSEL, then drag the desired `.uf2` onto the `RPI-RP2` drive.

If you are interested in making one, join the project Discord to ask questions and get the most up to date information: https://discord.gg/eNJ7xWMvxf

You can find all documentation here: https://github.com/PhobGCC/PhobGCC-doc

Board version 1.2:

![Board version 1.2](https://github.com/PhobGCC/PhobGCC-doc/raw/main/For_Makers/BuildPics_1.2.2/CVAC1118_1lwoupq-output.jpg?raw=true)

Hall effect sensors:

![Prototypes](https://www.dropbox.com/s/fyltdef79c2z78y/Hall%20Sensors.png?raw=1)

Initial prototypes:

![Prototypes](https://www.dropbox.com/s/q8ypkzmfeijdc5w/boards.jpg?raw=1)
