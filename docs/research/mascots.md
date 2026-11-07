# Mascot ideas

Clawd (Claude Code's mascot) sits in the welcome panel today (`welcome()` in src/harness/harness.cpp). These are
the replacements we sketched, so we can change our minds later. ClawDOS is partly a play on GLaDOS, hence the last
two rounds; those take Portal's themes (a deadpan lab AI, test subjects, optics, personality cores) without
copying its characters or props.

All art is CP437 half blocks (`█` 0xDB, `▀` 0xDF, `▄` 0xDC, space): each text row is two "pixel" rows, so a
4-row drawing is 8 pixels tall. The slot is rows 4-7 of the welcome panel's left half (38 columns), centred.
Every cell has its own attribute, so a drawing can mix colours cell by cell (on black), not within a cell.

## Chosen: CORE, "watching" (11x4)

```
  █▀▀▀▀▀▀▀▀▀█
  █ █▀███▀█ █
  █ ▀▀▀▀▀▀▀ █
  ▀▀█ █ █ █▀▀
```

A personality core as 1983 hardware: an 8-bit ISA card with one eye, plugged into the 5160. The "AI" is
literally something added to this PC. Suggested colours: green board, light red eye, yellow edge-connector fingers
(the harness still draws it all in Clawd's brown, `A_CLAWD`).

It's 11 wide so the four legs sit symmetrically; the first 10-wide draft (`▀▀█ █ █ █▀`) had two cells of edge
left of the legs and one on the right, and looked off-centre in 86Box. The outer `▀▀` on each side rounds off
the bottom corners. As C strings, in `welcome()`:

```c
"\xDB\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDB",
"\xDB \xDB\xDF\xDB\xDB\xDB\xDF\xDB \xDB",
"\xDB \xDF\xDF\xDF\xDF\xDF\xDF\xDF \xDB",
"\xDF\xDF\xDB \xDB \xDB \xDB\xDF\xDF",
```

The same card had other moods, which could come back if a state ever gets its own picture (thinking, safe-mode
warning, error):

```
  █▀▀▀▀▀▀▀▀█    █▀▀▀▀▀▀▀▀█
  █ ▄▄▄▄▄▄ █    █ ██  ██ █
  █ ▀    ▀ █    █        █
  ▀▀█ █ █ █▀    ▀▀█ █ █ █▀
    sleepy         alert
```

A bigger first version (16x4), with an eye chip and other chips on the board:

```
  █▀▀▀▀▀▀▀▀▀▀▀▀▀▀█
  █ ▄█▀▀█▄  ▄▄ ▄ █
  █ ▀█▄▄█▀  ▀▀ ▀ █
  ▀▀▀▀▀▀█ █ █ █ █▀
```

### Voice

Much of GLaDOS is tone, and the harness has places for one: spinner verbs, the welcome line, error notes. These
samples are our own lines, not quotes from the game; keep it light, it's on screen all day.

- Spinner: `Calibrating...`, `Observing...`, `Recording results...`, `Testing...`
- Welcome: `Welcome to ClawDOS. Your participation is noted.`
- Lost link: `The tunnel has stopped cooperating. Retrying, as is tradition.`
- Safe mode: `This will modify files. For your safety, confirm with Y.`

## For reference: Clawd today (12x4, brown)

```
   ██████████
  ███▄████▄███
  ▀██████████▀
   █ █    █ █
```

## Round 1

**Hermit**: a hermit crab living in an IBM 5160 case. Keeps the "claw"; old hardware with something new inside.
Light gray shell, light red crab; the eye stalks could pull in while thinking.

```
   █ █  ▄█▀▀▀▀▀▀▀█
  ▄▄█ ▄███ █████ █
  █▄▄▀▀███▄▄▄▄▄▄▄█
        ▀ ▀ ▀ ▀ ▀
```

**Disko**: a 5.25" floppy whose read slot and hub become a face. White or light cyan.

```
   █▀▀▀▀██▀▀▀▀█
   █   ▄▄▄▄   █
   █  ▀▄▄▄▄▀  █
   █▄▄▄▄██▄▄▄▄█
```

**Blinky**: the block cursor at a `C:\>` prompt, with eyes and feet. Light green; it could blink with the
INT 1Ch ticker.

```
       ████████
       ██ ██ ██
  C:\> ████████
        █    █
```

**Roach**: the bug that survives everything, like DOS. Brown.

```
    ▀▄    ▄▀
     ▄████▄
  ▀▀▄█▄██▄█▄▀▀
  ▀▀▄▀████▀▄▀▀
```

**The Crane**: an arcade claw grabbing a byte, no creature at all. Cyan claw, magenta prize (CGA palette 1).

```
        ██
     ▄▀▀██▀▀▄
    █  ▄▄▄▄  █
     ▀ ████ ▀
```

**Kit**: a CGA cat; claws without crustaceans. Light magenta.

```
   █▄    ▄█
   █▀████▀█  ▄
   ▀██████▀ ▄▀
    █▀█▀▀█▀█
```

## Round 2

**Chip**: an 8088 walking on its own DIP pins. Dark or light gray.

```
  ▄█▄█▄█▄█▄█▄█▄█▄
  ██ ██     ██ ██
  ███████████████
   █ █ █ █ █ █ █
```

**Shelldon**: the hermit crab with a 5.25" floppy for a shell. White floppy, light red crab.

```
   █ █  ██▀████▀██
  ▄▄█  ▄███▀▀▀▀███
  █▄▄▀▀███▄ ▀▀ ▄██
        ▀ ▀ ▀ ▀ ▀
```

**4.77**: a turtle; slow and steady at 4.77 MHz. Green.

```
     ▄▀█▀█▀█▀▄
   ▄▀█▀█▀█▀█▀█▀▄ ▄
   ▀▀█▀█▀▀▀▀█▀█▀▀▀
     ▀ ▀    ▀ ▀
```

**Snail**: a snail with a little CRT for a shell; the screen could show the spinner. Light gray, cyan screen.

```
     █▀▀▀▀▀▀▀█
  █ ▄█ █████ █
   ███▄▄▄▄▄▄▄█▄
   ▀▀▀▀▀▀▀▀▀▀▀▀▀
```

**Ghost**: the ghost in the machine. Light cyan or white (steer clear of any one game's ghost colours).

```
    ▄██████▄
   ██ ████ ██
   ██████████
   █▀██▀▀██▀█
```

**Octo**: an octopus; many arms on a single-tasking OS, or one per tool call. Magenta.

```
    ▄██████▄
    █▄████▄█
   ▄▀█▀██▀█▀▄
  ▀▄ ▀▄▀  ▀▄ ▀
```

**Bit**: a tiny robot whose antenna could blink. Light blue or cyan.

```
       █
    █▀███▀█
   ▄███████▄
  ▀ █▀▀▀▀▀█ ▀
```

**Hoot**: a night-owl coder. Brown with yellow eyes.

```
   █▄▄▄▄▄▄▄█
  █▀▄ ███ ▄▀█
  ▀█▄▄█▄█▄▄█▀
   ▀█▀█▀█▀█▀
```

## Round 3

**Hopper**: a moth, after the one taped into the Harvard Mark II logbook in 1947 ("first actual case of bug
being found"). Light gray, or brown with yellow spots.

```
   ▀▄       ▄▀
  ████▄ ▄ ▄████
  ▀█████ █████▀
    ▀▀▀ ▀▄▀▀▀
```

**Webby**: a spider spinning the network (NE2000 over WiFi). Dark gray with red eyes, or magenta.

```
  ▀▄ ▀▄    ▄▀ ▄▀
   ▄█ ▄████▄ █▄
  ▀▄ ▀█▄██▄█▀ ▄▀
  ▀ ▄▀  ▀▀  ▀▄ ▀
```

**Digger**: a mole tunnelling through directories with its digging claws. Brown, light red nose.

```
    ▄▄██████▄▄
   ██▄██████▄██
  ▄█▀███▄▄███▀█▄
  ▀ ▀▀▀    ▀▀▀ ▀
```

**Clicky**: the 5160's Model F keyboard on legs. Light gray, dark gray keys.

```
   ██████████████
   █▄▄█▄▄█▄▄█▄▄█▄▀
   █▄█▄▄█▄▄█▄▄█▄▄▀
    █▀        ▀█
```

**Tapey**: a cassette (the PC had a cassette port); spinning reels for the busy animation. Dark gray, white label.

```
  █▀▀▀▀▀▀▀▀▀▀▀▀█
  █ █▀█    █▀█ █
  █ ▀▀▀    ▀▀▀ █
  ███▄▄████▄▄███
```

**Lotl**: an axolotl, which regenerates like a resumed conversation. Light magenta.

```
  ▀▄▀▄▄▄▄▄▄▀▄▀
  ▀ █▀████▀█ ▀
    ▀██████▀
    █▀ █▀ ▀█
```

**Nightbyte**: a bat at home on a black CGA screen; wing claws. Magenta or dark gray.

```
  █▄  ▄██▄  ▄█
  ███▄█▄▄█▄███
  ▀  ▀████▀  ▀
       ▀▀
```

**Kit Fox**: big ears, quick, digs with its claws; the warmest swap for Clawd's brown. Brown or light red.

```
  █▄        ▄█
  ▀██▀████▀██▀
   ▀█▄████▄█▀
      ▀██▀
```

## Round 4 (Portal themes)

**OPTIC**: the facility's single, mildly disappointed eye. Dark gray bezel, yellow iris that could narrow and widen.

```
    ▄▄██████▄▄
  ▄█▀▀ ▄▄▄▄ ▀▀█▄
  ▀█▄▄ ▀▀▀▀ ▄▄█▀
    ▀▀██████▀▀
```

**CORE**: the ISA personality card; see the chosen design above.

**The Arm**: a ceiling-rail claw that lowers in to handle files. Light gray, red eye band.

```
  ▀▀▀▀▀█
    ▄▄▄█▄▄▄
   ▀█▄█▄█▄█▀
  ▄▀   █   ▀▄
```

**Subject**: the lab rat; the 8088 doing its best under evaluation. White or light gray, pink tail.

```
  ▄██▄
   ▀█▀█▄██████▄
   ██▀▀██████▀█▀
        ▀  ▀   █
```

**Breach**: a crab climbing through a ring. One ring colour (cyan), light red crab: a nod, not the game's
blue/orange pair.

```
    ▄▄▀▀▀▀▀▀▄▄
  ▄▀  █▄██▄█  ▀▄
  ▀▄ ▀█▀▀▀▀█▀ ▄▀
    ▀▀▄▄▄▄▄▄▀▀
```

**HAZARD**: a caution sign with a claw pictogram; could grow into a set (safe mode, unsafe, memory). Yellow.

```
       ▄▀▀▄
     ▄▀▄██▄▀▄
   ▄▀ ▀ ▀▀ ▀ ▀▄
  ▀▀▀▀▀▀▀▀▀▀▀▀▀▀
```

## Round 5 (Portal themes)

**MONITOR**: a 5153 CRT with one eye in the phosphor; the AI is the screen. Light gray, light cyan eye.

```
  ██▀▀▀▀▀▀▀▀▀▀▀▀██
  ██   ▄█▀▀█▄   ██
  ██    ▀▀▀▀    ██
  ▀▀▀▀▀██████▀▀▀▀▀
```

**WATCHER**: a wall-mounted camera that pans while waiting. Dark gray, red lens.

```
  ██
  ██▀▀███▄▄▄▄▄▄▄
      ██████▀▀██
      ██████▄▄██
```

**BEEP**: the PC speaker as a face; it could beep when a reply finishes. Dark gray, white.

```
   ▄▀▀▀▀▀▀▀▀▀▀▄
  █  █ █  █ █  █
  █   ▄▄▄▄▄▄   █
   ▀▄▄▄▄▄▄▄▄▄▄▀
```

**Dr. Pincer**: a crab scientist in lab goggles. Light red, white goggles.

```
  █▄█        █▄█
   █ ▄▀▀██▀▀▄ █
   ▀▄█▄▄██▄▄█▄▀
   ▄▀ █▀▀▀▀█ ▀▄
```

**EYE-8088**: the CPU opens one eye. Dark gray, yellow iris.

```
  ▄█▄█▄█▄█▄█▄█▄█▄
  ████▀ ▄▄▄ ▀████
  ████▄ ▀▀▀ ▄████
  ▀█▀█▀█▀█▀█▀█▀█▀
```

**The Reward**: a cupcake that may or may not exist; better as an easter egg (say, after a passing tool tour).
Magenta frosting, brown base, yellow flame.

```
       ▄█▄
     ▄▄▄█▄▄▄
   ▄█████████▄
    █▄█▄█▄█▄█
```
