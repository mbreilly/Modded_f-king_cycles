# Machines

Machines 1–6 are Elektron's own KICK, SNARE, METAL, PERC, TONE and CHORD, unchanged. Machines 7–25 are added.

Main knobs are the usual COLOR / SHAPE / SWEEP / CONTOUR (MIDI CC 16–19). **Extras** are played with MACHINES held
(or the knob layer, double-tap PUNCH): PITCH / DECAY / COLOR / SHAPE become Extra 1–4 (CC 20–23, NRPN 1:40–1:43).
Every extra defaults to 0, which is the machine's plain sound.

## 7 · 909BD

TR-909 kick from analog circuit models (DMDaudio model909, MIT), tuned against real 909 recordings.

**Accent** makes accented hits (PUNCH on) up to 6 dB louder, like the 909's TOTAL ACCENT; plain hits are unchanged.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | BD Attack | 64 |
| SHAPE | BD Shape | 64 |
| SWEEP | BD Sweep | 64 |
| CONTOUR | Sweep Time | 64 |
| MACHINES + PITCH | Accent | 0 |
| MACHINES + DECAY | Tune Decay | 0 |
| MACHINES + COLOR | Click | 0 |
| MACHINES + SHAPE | Tone | 0 |

## 8 · 808BD

TR-808 kick from a circuit model, tuned against real 808 recordings. A new hit rings on top of the old tail instead of cutting it. DECAY 64 fades out in about 1.5 s like a classic 808, 0 in 0.1 s, 127 in about 4 s.

CONTOUR is the **Accent** level of accented hits, as on the 808. **Drift** gives every hit a slightly different pitch (up to ±25 cents) and level (about ±1 dB), like the parts of a real drum machine.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | BD Tone | 64 |
| SHAPE | BD Drop | 64 |
| SWEEP | Drop Time | 64 |
| CONTOUR | Accent | 0 |
| MACHINES + PITCH | Drift | 0 |
| MACHINES + DECAY | Tail | 0 |
| MACHINES + COLOR | Click | 0 |

## 9 · RUMBL

A rumble kick: a sine kick with pitch drop, held at full level for a moment, fed into a dark feedback delay network, so the rumble outlives the kick. C4 at PITCH 64 = a 52 Hz kick. DECAY = kick body, 60 ms to 3 s.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Tail | 64 |
| SHAPE | Damp | 40 |
| SWEEP | Drive | 40 |
| CONTOUR | Hold | 0 |
| MACHINES + PITCH | Sweep | 0 |
| MACHINES + DECAY | Sweep Time | 0 |
| MACHINES + COLOR | Rumble Level | 0 |
| MACHINES + SHAPE | Tail Pitch | 0 |

## 10 · 909

The TR-909 kit apart from the kick, in one machine: SHAPE (Voice) picks LT, MT, HT, RIM, CLP, CH, OH, RD, CR or **SD** on each trig (lock it per step). Each part keeps ringing when another is hit, as on the real 909. The knobs go to the voice of the last trig. The SD is tuned against real 909 snare recordings.

**Accent** makes accented hits up to 6 dB louder; **Drift** varies pitch and level from hit to hit.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Tone/ Noise | 64 |
| SHAPE | Voice (LT / MT / HT / RIM / CLP / CH / OH / RD / CR / SD) | 0 |
| SWEEP | Sweep/ Spread | 64 |
| CONTOUR | Time/ Burst | 64 |
| MACHINES + PITCH | Accent | 0 |
| MACHINES + DECAY | Range/ Tail | 0 |
| MACHINES + COLOR | Drift | 0 |
| MACHINES + SHAPE | Tone | 0 |

## 11 · 606

A TR-606 kit in one machine (after Matthew Fecher's TR-606 analysis); SHAPE picks BD, SD, LT, HT, CH, OH or CY on each trig. PITCH tunes (64 = the 606), DECAY sets the decay, PUNCH is the accent.

**Drift** varies pitch and level from hit to hit. The other extras act on the voice SHAPE selects.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Tone | 64 |
| SHAPE | Voice (BD / SD / LT / HT / CH / OH / CY) | 0 |
| SWEEP | Sweep | 64 |
| CONTOUR | Noise | 64 |
| MACHINES + PITCH | Drift | 0 |
| MACHINES + DECAY | Long Decay | 0 |
| MACHINES + COLOR | Sweep | 0 |
| MACHINES + SHAPE | Accent Level | 0 |

## 12 · 808

The TR-808 hi-hat and cymbal in one machine: six square oscillators through band-pass and high-pass filters. SHAPE 0–63 is the hat, 64–127 the cymbal, and within each half it is that voice's high-pass. The hat and the cymbal each keep ringing when the other is hit.

**Drift** varies the level from hit to hit.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Color/ Tone | 64 |
| SHAPE | Voice HPF | 32 |
| SWEEP | Res/Hi Decay | 64 |
| CONTOUR | Noise/ Spread | 0 |
| MACHINES + PITCH | Metal Tune | 0 |
| MACHINES + DECAY | Drift | 0 |
| MACHINES + COLOR | Filter/ Curve | 0 |
| MACHINES + SHAPE | Accent Level | 0 |

## 13 · METL2

Modal metal: a mallet or noise exciter into 8 damped resonators whose ratios morph across six materials (after the modal resonators of Émilie Gillet's Rings and Plaits, written from scratch). DECAY = damping, the lowest mode ringing 50 ms to 10 s.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Struct (STR / PIP / SHT / PLT / BEL / BAR) | 70 |
| SHAPE | Bright | 80 |
| SWEEP | Position | 20 |
| CONTOUR | Exciter | 10 |
| MACHINES + PITCH | Dent | 0 |
| MACHINES + DECAY | Scrape | 0 |
| MACHINES + COLOR | Hi Damp | 0 |
| MACHINES + SHAPE | Drive | 0 |

## 14 · BIAS

A percussion and drone voice ported from [100demons/bias](https://github.com/100demons/bias) 2.0 (MIT): six oscillators with their own decays and an unlimited wavefolder, on an internal clock locked to the pitch (a clean-room voice after the published description of the Basimilus Iteritas Alia). COLOR adds partials, SHAPE morphs sine → triangle → saw → square, SWEEP spreads the partials from harmonic to inharmonic, CONTOUR sets the fold. DECAY 127 drones; PUNCH gives the Liquid pitch drop when the Liquid extra is 0.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Harm | 40 |
| SHAPE | Morph | 43 |
| SWEEP | Spread | 16 |
| CONTOUR | Fold | 24 |
| MACHINES + PITCH | Liquid | 0 |
| MACHINES + DECAY | Liq Time | 0 |
| MACHINES + COLOR | Strike | 0 |
| MACHINES + SHAPE | Drive | 0 |

## 15 · ACID

A TB-303 style bass (after Robin Schmidt's Open303, MIT, rewritten in integer code): saw or square into a resonant filter with its own envelope, slides from your trigs, and a drive, tuned against a TD-3. The extras are Devil Fish style modifications. Hold PUNCH half a second to switch saw / square.

Slide: make a trig longer than the gap to the next trig and the note glides into it.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Cutoff | 40 |
| SHAPE | Reso | 90 |
| SWEEP | Env Mod | 80 |
| CONTOUR | Drive | 0 |
| MACHINES + PITCH | Slide Time | 0 |
| MACHINES + DECAY | Filter FM | 0 |
| MACHINES + COLOR | Muffler | 0 |
| MACHINES + SHAPE | Accent | 0 |

## 16 · GROWL

A dark, growling bass: three detuned saws that drift slowly, a sub, a noise layer and a drive into a dark low-pass, with a fast growl (4 to 32 Hz) on the filter, the level and, at high depth, the pitch. Best between C1 and C2.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Detune | 40 |
| SHAPE | Growl Rate | 72 |
| SWEEP | Growl Depth | 64 |
| CONTOUR | Tone | 60 |
| MACHINES + PITCH | Sub Level | 0 |
| MACHINES + DECAY | Attack | 0 |
| MACHINES + COLOR | Noise | 0 |
| MACHINES + SHAPE | Drive | 0 |

## 17 · MACRO

Émilie Gillet's Braids macro-oscillator (MIT), compiled unchanged: 47 models (classic and sync waves, triple oscillators, swarm, filtered waves, vowels and formants, FM, physical models, drums, wavetables and noise). FUNC + PITCH = lo-fi (sample rate to the left, bits to the right).

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Timbre | 64 |
| SHAPE | Model | 0 |
| SWEEP | Color | 64 |
| CONTOUR | Env Timbre | 0 |
| MACHINES + PITCH | Attack | 0 |
| MACHINES + DECAY | Env Color | 0 |
| MACHINES + COLOR | Lofi Bits | 0 |
| MACHINES + SHAPE | Lofi Rate | 0 |

## 18 · VACF

A virtual-analog voice: a variable-shape oscillator and a sub into two state-variable filters, with a filter envelope, hard sync, a detuned second oscillator and drive (after Plaits' VA engine, written from scratch).

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Cutoff | 50 |
| SHAPE | Reso | 40 |
| SWEEP | Env Mod | 60 |
| CONTOUR | Wave (SAW / SQR / PLS) | 0 |
| MACHINES + PITCH | Sync | 0 |
| MACHINES + DECAY | Detune | 0 |
| MACHINES + COLOR | Sub | 0 |
| MACHINES + SHAPE | Drive | 0 |

## 19 · XMOD

Cross-modulation: a carrier at the note and a modulator at a musical ratio, combined by six algorithms that CONTOUR morphs through (after Warps, written from scratch).

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Timbre | 40 |
| SHAPE | Mod Ratio (11 named values) | 40 |
| SWEEP | Mod Wave (SIN / TRI / SAW / SQR) | 0 |
| CONTOUR | Algo (XFD / FLD / DIO / RNG / XOR / CMP) | 50 |
| MACHINES + PITCH | Car Wave (SIN / TRI / SAW / SQR) | 0 |
| MACHINES + DECAY | Mod Env | 0 |
| MACHINES + COLOR | Detune | 0 |
| MACHINES + SHAPE | Crush | 0 |

## 20 · DX

Six-operator FM like a DX7: the 32 classic algorithms, per-operator envelopes, feedback, and a bank of 32 sounds of its own (no Yamaha patches). C4 at PITCH 64.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Patch (32 named values) | 0 |
| SHAPE | Bright | 64 |
| SWEEP | Env Time | 64 |
| CONTOUR | Algo (33 named values) | 0 |
| MACHINES + PITCH | Feedback | 0 |
| MACHINES + DECAY | Op6 Ratio | 0 |
| MACHINES + COLOR | Vel Sens | 0 |
| MACHINES + SHAPE | Detune | 0 |

## 21 · ATMOS

A dark pad: a 4-note chord of detuned saw pairs under a low-pass that drifts slowly, with its own plate tail. DECAY is the release (127 = hold). GATE on = a drone that plays by itself while the sequencer runs (STOP releases it, PLAY starts it again).

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Color | 72 |
| SHAPE | Chord (OCT / 5TH / MIN / MI7 / MI9 / M11 / SU2 / SU4 / MAJ / MA7 / DRK / CLU) | 3 |
| SWEEP | Drift | 64 |
| CONTOUR | Space | 70 |
| MACHINES + PITCH | Swell | 0 |
| MACHINES + DECAY | Air | 0 |
| MACHINES + COLOR | Wobble | 0 |
| MACHINES + SHAPE | Width | 0 |

## 22 · SAMPLER

Plays a sample you sent with Elektron Transfer (see the README, Samples). One sample per track, up to about 71 s. PITCH and the trig note transpose (64 / C4 = as recorded).

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Start | 0 |
| SHAPE | End | 127 |
| SWEEP | Mode (FWD / REV / LOP / PNG) | 0 |
| CONTOUR | Lofi | 0 |
| MACHINES + PITCH | Fine | 0 |
| MACHINES + DECAY | Attack | 0 |
| MACHINES + COLOR | Loop Start | 0 |
| MACHINES + SHAPE | Drive | 0 |

## 23 · GRANULAR

Granular playback of the track's sample: up to 8 grains under a Hann window.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Position | 0 |
| SHAPE | Size | 64 |
| SWEEP | Density | 64 |
| CONTOUR | Spray | 0 |
| MACHINES + PITCH | Spread | 0 |
| MACHINES + DECAY | Window | 0 |
| MACHINES + COLOR | Scan | 0 |
| MACHINES + SHAPE | Reverse | 0 |

## 24 · WAVETABLE

Turns the track's sample into a wavetable: frames of 2,048 samples, one period per frame. The phase restarts on each trig.

| Knob | Parameter | Default |
|---|---|---|
| COLOR | Position | 0 |
| SHAPE | Warp | 0 |
| SWEEP | Env | 0 |
| CONTOUR | Env Speed | 64 |
| MACHINES + PITCH | Sub | 0 |
| MACHINES + DECAY | Detune | 0 |
| MACHINES + COLOR | Crush | 0 |
| MACHINES + SHAPE | Drive | 0 |

## 25 · EMPTY

Silence at almost no CPU. A new project puts EMPTY on all six tracks, so you start with the whole CPU budget free.

