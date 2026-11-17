# TINY 2.1 — a transformer for DOS

![TINY 2.1 running under DOS](tiny21.png)

A tiny question-answering transformer written in Turbo C 2.0. It runs on a
Pentium or 486DX under plain DOS and fits on a floppy disk.

- 367,000 parameters: 4 layers, 96 wide, reads and writes letter by letter
- 8-bit weights (one scale per row), 380 KB in all
- Needs about 540 KB of free conventional memory
- Trained on about 3,500 facts, each asked in many wordings
  Outside those it makes things up confidently
- Neon full-screen interface with a live neural monitor and a PC-speaker
  robot voice

Written by Fable 5.2 and trained by Opus 5.5.

## Download for the 3DS

[![One-click download](https://img.shields.io/badge/download-tiny21.cia-2ea44f?style=for-the-badge)](https://github.com/paige-morningstar/Tiny-2.1/releases/download/v1.0/tiny21.cia)

One click gets you **`tiny21.cia`** (641 KB). Install it with FBI (Titles → SD,
or any title manager), then launch **TINY 2.1** from the home menu. Custom
firmware (Luma3DS) required. The network and its 380 KB of weights are inside
the title, so nothing else has to be copied to the SD card.

To install straight from the 3DS, open **FBI → Remote Install → Scan QR Code**
and point the camera at this:

![QR code that downloads tiny21.cia](qr-cia.png)

Or copy the direct link: <https://github.com/paige-morningstar/Tiny-2.1/releases/download/v1.0/tiny21.cia>

## Run

```
C:\> CD \TINY
C:\TINY> TINY
```

Keep `TINY.EXE` and `TINY.BIN` in the same folder. To rebuild:
`TCC -ml TINY.C`

## On a Nintendo 3DS

`3ds/` holds a libctru port of the same network, built as a Homebrew Launcher
app: the conversation on the top screen, the neural monitor on the bottom one,
the 3DS software keyboard for questions, and the robot voice synthesised into
the DSP. The model is embedded in the app, so it ships as one file.

```
cd 3ds && make      # needs devkitPro (devkitARM + libctru)
cd 3ds && make cia  # also needs makerom, for an installable title
```

Copy `3ds/tiny21.3dsx` to the SD card and open it from the Homebrew Launcher,
or install `3ds/tiny21.cia` with FBI so it lives on the home menu as a normal
title (title ID `0x000400000F716900`, needs CFW). Both carry the network
inside, and both will use a retrained `TINY.BIN` placed at
`sdmc:/3ds/TINY/TINY.BIN`. See `3ds/README.md` for the button map.
