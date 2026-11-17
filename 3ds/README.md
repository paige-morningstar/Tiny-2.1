# TINY 2.1 for the Nintendo 3DS

A libctru port of the DOS program: the same 367,000-weight transformer in
`TINY.BIN`, running as a Homebrew Launcher app.

- The conversation is on the top screen, the neural monitor on the bottom one
- The 3DS software keyboard asks the question (A)
- The robot voice is synthesised into a 22 kHz PCM buffer and played by the
  DSP, lighting up each letter as it is spoken
- The model is embedded in the `.3dsx` as RomFS, so it is one file

## Keys

| Button | Does |
| --- | --- |
| A | ask a question |
| SELECT | help |
| X | sound on/off |
| Y | robot voice on/off |
| L / R | creativity 0-9 (0 = always the surest answer) |
| Up | ask the last question again |
| B | stop the voice, or stop a long answer |
| START | quit |

Typing `HELP`, `CLS`, `QUIT` or `EXIT` as the question does the obvious thing.

## Build

Needs devkitPro with devkitARM and libctru, and the model at `romfs/TINY.BIN`.

```
cd 3ds
make
```

This makes `tiny21.3dsx` and `tiny21.smdh`. Copy `tiny21.3dsx` to the SD card
and open it from the Homebrew Launcher, or push it over the network:

```
3dslink tiny21.3dsx
```

## Installing it as a title (CIA)

```
cd 3ds
make cia
```

This makes `tiny21.cia`: a normal application title (content type Application,
title ID `0x000400000F716900`, unencrypted, installs to the SD card). Copy it
to the SD card and install it with FBI, then it sits on the home menu like any
other app. The banner, its tune and the icon live in `assets/` and are already
built, so `make cia` only needs **makerom** on the `PATH`
(<https://github.com/3DSGuy/Project_CTR/releases>).

`tiny.rsf` is the ROM spec: it grants the services the port uses (`APT:U` for
the software keyboard, `dsp::DSP` for the voice, `fs:USER`/`gsp::Gpu`/
`hid:USER`, `DirectSdmc` for a model on the SD card) and nothing else.

To change the banner, icon or banner tune, edit `tools/make_gfx.py` and run
`make gfx`, which needs **bannertool**.

The model is read from `romfs:/TINY.BIN` inside the app. If you retrain the
network, drop the new file at `sdmc:/3ds/TINY/TINY.BIN` and it will be used
instead, without rebuilding.

## Testing without a 3DS

`source/tiny_model.c` has no 3DS or DOS code in it, so the network runs on a
PC too:

```
cc -O2 -Iinclude -o tiny_host source/tiny_model.c tools/host_test.c -lm
./tiny_host ../TINY.BIN
```
