# Install

Two ways in. The first builds it from source on a Mac; the second needs
nothing installed at all.

## 1. Build it on a Mac

```sh
xcode-select --install        # make and cc, if you don't already have them
brew install cc65 vice        # the 6502 cross compiler, and the emulator
```

`cc65` gives you `cl65`. `vice` gives you `xplus4` (the Plus/4 emulator) and
`c1541` (which builds the disk image). Nothing else is required — the
generated song data is committed, so no Python is needed for a plain build.

Then:

```sh
git clone https://github.com/vonglurt/urfinkel.git
cd urfinkel
./start.sh
```

`start.sh` puts Homebrew on `PATH` and runs `make run`, which compiles the
game, packs `build/urfinkel.d64`, and boots it in `xplus4`. If Homebrew is
already on your `PATH`, `make run` on its own does the same thing.

Other targets worth knowing:

| Command | What it does |
|---|---|
| `make` | build `build/urfinkel.prg` |
| `make disk` | pack `build/urfinkel.d64` |
| `make run` | build, then boot it in `xplus4` |
| `make run200` | the same at 200% speed |
| `make check && make conform` | the gate: host tests, then the board diff |

## 2. Run it in a browser

Nothing to install. Download either file — **right-click → Save Link As**, or
your browser may just display it:

- [**urfinkel.d64**](https://github.com/vonglurt/urfinkel/raw/main/build/urfinkel.d64) — a disk image (174 848 B)
- [**urfinkel.prg**](https://github.com/vonglurt/urfinkel/raw/main/build/urfinkel.prg) — the program alone (56 511 B)

Open [**plus4.cybernoid.xyz**](https://plus4.cybernoid.xyz/), click **ADD
MEDIA**, choose the file you saved, then double-click its entry to load and
run it. Either file works; the `.prg` starts faster.

Desktop emulators — **VICE** (`xplus4 urfinkel.d64`), **YAPE**, **plus4emu** —
all take either file too.

## On real hardware

Put `urfinkel.d64` on an SD2IEC and load it as you would any disk, or
`dload"urfinkel"` from the image. It was last run this way on a restored PAL
Plus/4 on 2026-08-07.
