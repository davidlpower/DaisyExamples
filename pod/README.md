# Daisy Pod Loop FX

## Overview

Daisy Pod Loop FX is a real-time stereo audio effects processor built for the Electrosmith Daisy Pod platform. It features three distinct processing modes:

- **Reverb** – A stereo Schroeder-type reverb with dry/wet and feedback control
- **Delay** – A stereo delay with configurable time and feedback
- **Looper** – A basic stereo looper supporting recording, playback, and overdubbing

All controls are accessible via the Daisy Pod's two knobs, two buttons, encoder, RGB LEDs, and external MIDI input.

---

## Mode Navigation

Press **Button 1** to cycle between:

1. **Reverb Mode (REV)**
2. **Delay Mode (DEL)**
3. **Looper Mode (LOOP)**

Each mode has its own LED color indication and parameter mappings.

---

## Parameter Controls

### Knob and MIDI Control Mapping

| Mode   | Knob 1              | Knob 2              | MIDI CC                  |
|--------|---------------------|---------------------|--------------------------|
| REV    | Dry/Wet mix         | Reverb feedback     | 10 (drywet), 11 (fb)     |
| DEL    | Delay time (inverted) | Delay feedback    | 12 (time), 11 (fb)       |
| LOOP   | No function (future) | No function (future) | Not mapped               |

> MIDI takes precedence over knobs if any MIDI CC is received more recently than knob movement.

---

## Reverb Details

- Effect: Stereo Schroeder reverb (`ReverbSc`)
- Parameters:
  - `DryWet` (0.0 to 1.0) – controls blend of wet signal
  - `Feedback` (0.0 to 1.0) – controls internal tail regeneration
- Internal LPF fixed at 18kHz

---

## Delay Details

- Effect: Dual-channel feedback delay
- Parameters:
  - `DelayTime` (50ms to 2500ms, inverted knob sweep)
  - `Feedback` (0.0 to 1.0)

> DelayTime is smoothed in real time using one-pole interpolation (`fonepole`).

---

## Looper Details

- Stereo, RAM-based loop buffer (max 10 seconds)
- Single-loop model with 4 states:
  - **Idle** – No loop recorded
  - **Recording** – Capture input to buffer (Button 2)
  - **Playing** – Loop playback (after first recording ends)
  - **Overdubbing** – Mixes live input with existing loop (Button 2 again)

Press **Button 2** to advance loop state:
```
Idle → Recording → Playing → Overdubbing → Playing (repeat)
```

- Loop length is fixed on first record pass
- Internal pointer wraparound ensures glitch-free looping
- Output is muted during idle state

---

## Visual Feedback (LEDs)

| Mode   | LED1 Color        | LED2 Color         |
|--------|-------------------|--------------------|
| REV    | Blue (DryWet)     | Blue (Feedback)    |
| DEL    | Green (Time)      | Green (Feedback)   |
| LOOP   | State dependent:  | Off                |
|        | • Idle: Dim grey  |                    |
|        | • Recording: Red  |                    |
|        | • Playing: Green  |                    |
|        | • Overdub: Orange |                    |

---

## MIDI CC Map

| CC Number | Function         |
|-----------|------------------|
| 10        | DryWet           |
| 11        | Feedback         |
| 12        | Delay Time       |
| 13        | Mode Change (value < 63 = prev, > 64 = next) |

---

## Loop Mode Variants

This version supports a **single stereo loop**. A dual-loop mode could be added by:
- Adding separate `LoopState` machines
- Duplicating buffer and playback logic
- Multiplexing loop selection via MIDI CC or encoder

Currently, the single loop supports creative layering through overdubbing and live regeneration.

---
