# 24 V input protection for the carrier board

Research for GitHub issue #3, "Which reverse-polarity protection and fuse for
the 24 V input?". Scope: what sits between the 24 V screw terminal and the
TMC2209 `VM` / 5 V buck input on the KiCad carrier board. That covers
reverse-polarity protection, the fuse, the TVS and the VM bulk capacitor.

Status: recommendation. Nothing here has been built or measured yet. The
current figures are estimates; section 7 lists the bench measurements that
would confirm them.

## TL;DR

```
J1 (+) ── F1 ── Q1 (P-FET, reverse polarity) ──┬── D1 TVS ──┬── C1 bulk ──┬── C2/C3 ceramics ──┬── TMC2209 VM
                                               │            │             │                    └── 5 V buck VIN
J1 (−) ────────────────────────────────────────┴────────────┴─────────────┴──── GND
```

| Ref | Function | Recommended part | Package | Alternative |
|---|---|---|---|---|
| F1 | Overcurrent | **Bourns SF-1206S300-2** (3 A slow-blow, 32 VDC, 50 A interrupt, 0.933 A²s) | 1206 | SF-1206S400-2 (4 A) if hot-plugging is routine; Littelfuse Nano2 0452-series slo-blo (not verified here) |
| Q1 | Reverse polarity | **AOS AOD4185** (−40 V P-ch, <15 mΩ at −10 V, V_GS ±20 V) | TO-252 (DPAK) | Any ≥40 V P-FET with V_GS ±20 V and R_DS(on) ≤ 50 mΩ at −10 V |
| R1 | Q1 gate pull-down | 100 kΩ, 1 % | 0603 | — |
| D2 | Q1 gate-source clamp | **Diodes Inc BZT52C12** (12 V zener, 500 mW) | SOD-123 | Any 10–15 V zener |
| D1 | Overvoltage / regen clamp | **Vishay SMBJ24A** (V_WM 24 V, V_BR 26.7–29.5 V, 600 W) | SMB (DO-214AA) | SMBJ26A (see open question 1) |
| C1 | VM bulk | **220 µF, 50 V**, low-ESR aluminium electrolytic, 105 °C, ripple rating ≥ 1 A at 100 kHz | radial or SMD can | 220 µF 35 V (the stocked part) is acceptable |
| C2, C3 | VM HF decoupling | 1 µF + 100 nF, 50 V X7R, at the module's VM pin | 0805 / 0603 | — |

Rejected: polyfuse (derates below the running current in a warm
enclosure, and is under-rated in voltage). Series Schottky (works, but cuts
the driver off from the PSU's capacitance, which is the case the TMC2209
datasheet warns about). Ideal-diode controller and eFuse (work, but they cost
more and also block regen current, so they buy nothing over the P-FET here).
Details follow.

## 1. What the load actually draws

### Hard limits from the parts

- TMC2209 operating range: `V_VS` 5.5–29 V. Absolute maximum: **32 V**
  "operating with inductive load", 33 V "supply and bridge voltage max", and
  **−0.5 V** minimum. So reverse polarity destroys it, and so does anything
  that lets VM ring above ~32 V. (TMC2209 datasheet rev 1.09, §19–20.1.)
  [1]
- The datasheet also warns that supply ringing from trace inductance "can
  easily generate a few volts of ringing leading to temporary voltage
  overshoot. This should be considered when working near the maximum
  voltage." [1, §19 footnote]
- BTT's module spec is narrower: VM **4.75–28 V DC**. [2]
- Mean Well LRS-50-24: 2.2 A rated, adjustable 21.6–28.8 V, ±1.0 %
  tolerance, overload 110–150 % (hiccup), **OVP 28.8–36.6 V** (shut down,
  re-power to recover), rise time 30 ms. LRS-150-24: 6.5 A, 21.6–28.8 V,
  overload 110–140 % (hiccup), **OVP 28.8–33.6 V**. (Both from the
  2025-04-07 spec sheets.) [3][4]

The PSU's own OVP can therefore let the rail reach 33.6–36.6 V before it
trips. That is above the TMC2209's 32 V absolute maximum, so **the PSU does
not protect the driver from a PSU overvoltage fault**. Only an on-board clamp
can.

### Supply current estimate

The bus current of a chopper driver is not the phase current. The supply only
delivers current during the "on" and fast-decay parts of each chopper cycle,
and the coil current recirculates during slow decay. The datasheet makes the
same point about the sense resistors: "no current flows through the sense
resistor during the slow decay phases", so it "conducts less than the coil RMS
current". [1, §8] So the supply power is the motor's copper loss, plus the
driver's own losses, plus the mechanical output, plus the buck's load.

Inputs:

- Run current from HARDWARE.md: 2HS60 at 1.2 A RMS/phase, 17HS4401 at
  1.36 A RMS/phase.
- 17HS4401: 1.5 Ω/phase and 2.8 mH/phase, per the MotionKing 17HS catalog.
  This is a secondary-hosted copy and unverified for Lino's units, so
  measure it. [16]
- 2HS60-1504JA05: no datasheet found. I assumed ≤ 3 Ω/phase hot, which is
  pessimistic for a 1.5 A, 60 mm motor. **Measure it.**
- TMC2209 R_DS(on): 0.17 Ω typ / 0.21 Ω max per FET at 25 °C [1, §20.2].
  The current always flows through one high-side and one low-side FET per
  bridge, and I allowed for R_DS(on) rising when hot (~0.3 Ω each).

| Contribution | 17HS4401 at 1.36 A | 2HS60 at 1.2 A |
|---|---|---|
| Copper, 2 × I² × R_hot (R_hot ≈ 1.4 × R_25) | 2 × 1.85 × 2.1 ≈ **7.8 W** | 2 × 1.44 × 3.0 ≈ **8.6 W** |
| Driver FETs, 2 × I² × 0.6 Ω | ≈ 2.2 W | ≈ 1.7 W |
| Sense resistors (0.11 Ω, partial duty) | < 0.4 W | < 0.3 W |
| Mechanical output (assumed ≤ 0.2 N·m at 6 rev/s = 37.7 rad/s) | ≤ 7.5 W | ≤ 7.5 W |
| Buck + XIAO + LED (at the 24 V side) | ≤ 1 W | ≤ 1 W |
| **Total, pessimistic** | **≈ 19 W → 0.8 A at 24 V** (0.86 A at 22 V) | **≈ 19 W → 0.8 A** |
| Energised standstill (no mechanical term) | ≈ 11 W → 0.45 A | ≈ 11 W → 0.45 A |
| Idle (EN̅ high), measured on rev 2 | 25 mA | 25 mA |

**Design figure: ~0.9 A continuous worst case.** The real figure is probably
0.4–0.6 A. HARDWARE.md's "a few watts per axis" is consistent with the lower
end of that.

The instantaneous chopper pulses are bigger. Each bridge can draw up to its
coil's peak current (1.36 × √2 ≈ 1.9 A), which is ~3.8 A for both bridges at
once. Those pulses run at the chopper frequency and are supplied by C1, not
the PSU or the fuse. That is what the bulk capacitor is for (section 4).

## 2. Reverse-polarity options

### Drop and heat at ~0.9 A

| Option | Drop at 0.9 A | Dissipation | Blocks reverse current? | Parts |
|---|---|---|---|---|
| Series Schottky, Vishay SS34 (40 V, 3 A, SMC) | ≤ 0.5 V (V_F max at 3 A is 0.5 V [5]), ~0.4 V at 0.9 A | ~0.35 W → ~+20 °C at R_θJA 55 °C/W [5] | **Yes, always** | 1 |
| P-FET, AOD4185 + R + zener | 0.9 A × 15 mΩ = 14 mV | ~12 mW | **No**, bidirectional when on | 3 |
| Ideal-diode controller, TI LM74700-Q1 + N-FET | regulates to 20 mV [6] | ~20 mW + 80 µA I_Q [6] | **Yes**, turns off in < 0.75 µs [6] | 2 + passives |
| eFuse, TI TPS2660x | 150 mΩ total R_ON → 0.14 V [7] | ~0.12 W | **Yes** ("reverse current blocking") [7] | 1 + passives |
| Low-side N-FET in the GND return | ~mΩ | negligible | No | 3 |

On drop and heat alone, everything except the Schottky is negligible, and
even the Schottky works (0.4 V off 24 V does not matter to the driver). The
choice is decided by regen.

### Regen and back-EMF: the deciding factor

The TMC2209 datasheet says, verbatim [1, §3.1]:

> "Provide overvoltage protection in case the motor could be manually turned
> at a high velocity, or in case the driver could become cut off from the
> main supply capacitors. Significant energy can be fed back from motor coils
> to the power supply in the event of quick deceleration, or when the driver
> becomes disabled."

A series Schottky, an ideal-diode controller and an eFuse all block reverse
current. TI's SLVAE57 says the Schottky "blocks reverse current flowing from
the load back into the battery all the time" [8, §5]. So each of them cuts
VM off from the PSU's output capacitors, which is the second case in the
quote. Energy coming back from the motor can only go into C1 (and the small
buck load), and it pumps VM up.

A P-FET does not block reverse current. Per SLVAE57, "this P-Channel MOSFET
protection does not block reverse current from flowing back into the input"
[8, §4.1]. So regen current flows back to the PSU's output capacitors and,
with a shared LRS-150-24, into the other units' loads. The Mean Well cannot
*sink* current, so this is extra capacitance, not a true sink. That is why
the TVS stays in the design either way (section 3).

How big is the energy? Estimated for the worst-case 17HS4401 unit:

- **Coil energy when EN̅ goes high** (firmware disables the driver at idle,
  `src/motion.c`): ½ L (i_A² + i_B²) = ½ × 2.8 mH × (√2 × 1.36 A)² ≈
  **5 mJ**. The two coils are in sine/cosine, so the sum of squares is
  constant.
- **Rotor and blind kinetic energy at 6 rev/s.** Assumed rotor inertia is
  ~54 g·cm² for a 40 mm NEMA 17. The blind adds 2 kg at a 19 mm radius,
  reflected through 1:15 as ~32 g·cm². That gives ½ × 8.6e-6 × 37.7² ≈
  **6 mJ**. This only comes back during a deceleration, and
  `motion_stop()` always ramps down (it never cuts EN̅ mid-move), so most
  of it goes into copper loss instead.
- **Continuous regen while the blind lowers.** P = m g v ≈ 2 kg × 9.81 ×
  ~0.05 m/s ≈ 1 W, against 8–11 W of copper and driver loss. The motor is
  never a net generator in steady descent, so there is no continuous
  pumping.

The worst transient is ~11 mJ into C1 alone, which is the blocking-element
case: V = √(24² + 2 × 0.011 / 220 µF) ≈ **26.0 V**. With the 2HS60's larger
rotor (assume 2–3× the energy) it is **≈ 27–28 V**. That already reaches the
SMBJ24A's minimum breakdown (26.7 V) and comes within ~1 V of the chip's
29 V operating maximum. It is survivable, but the margin is thin. And it
ignores the case the datasheet calls out first: someone hand-pulling a blind
through a back-drivable 1:15 gearbox spins the motor 15× faster, and the
body diodes rectify the back-EMF straight into VM.

With a P-FET, the same energy is shared with the PSU's output capacitance as
well. (The LRS output capacitance is not published; it is typically hundreds
to thousands of µF.) The rise is correspondingly smaller.

**Decision: P-channel MOSFET.** It has the lowest loss of the simple options,
it keeps VM tied to the PSU's capacitance for regen, and it is three cheap
parts.

- Orientation follows TI SLVA139 Fig. 3 [9]: drain to the input, source to
  the protected rail, and the body diode pointing in the normal current
  direction.
- Gate to GND through R1 (100 kΩ). D2 (12 V zener, cathode to source)
  clamps V_GS, because the rail can exceed the FET's ±20 V V_GS rating
  [10]: the PSU is adjustable to 28.8 V and its OVP point is up to 36.6 V
  [3].
- At 24 V this gives V_GS ≈ −12 V, above the −10 V at which R_DS(on)
  < 15 mΩ is specified [10]. D2 is rated 11.4–12.7 V at 5 mA [11]. Zener
  current is (24 − 12) V / 100 kΩ ≈ 0.12 mA.
- Reversed input: the gate sits at the source potential, V_GS ≈ 0, the
  body diode is reverse-biased, and the FET sees 24 V V_DS against its
  40 V rating [10].

Why not the others:

- **Schottky:** it works, but it is the "cut off from the main supply
  capacitors" case, it costs 0.35 W of heat, and it would need C1 enlarged
  to recover the regen margin. Keep it as the fallback if the P-FET's
  extra parts are unwanted. If so, use SS34 (40 V) [5] and C1 ≥ 470 µF.
- **LM74700-Q1 ideal diode:** it is designed to block reverse current fast
  [6], which is the opposite of what is wanted here. It adds a charge-pump
  IC and an N-FET to save ~0 mV over the P-FET at 1 A.
- **TPS2660x eFuse:** it is attractive as a single chip. It gives reverse
  polarity protection to −60 V, a 0.1–2.23 A adjustable current limit, an
  adjustable OVP cut-off and inrush slew control [7]. But it also blocks
  reverse current [7]. Its OVP *cut-off* (TPS26600/1) would disconnect VM
  from the supply during exactly the overvoltage event the datasheet says
  to absorb [1], so if used, the TVS must sit on its output. It is a
  reasonable "rev 2" upgrade if inrush control becomes a problem
  (section 5). It is not needed now.
- **Low-side N-FET:** SLVAE57 notes the ground shift it introduces [8,
  §4.4]. Here the XIAO's USB ground would bypass it (the PC ground ties
  straight to board GND), so it is rejected.

## 3. TVS on the input: warranted

There are three reasons, all from primary sources:

1. The datasheet explicitly asks for "overvoltage protection" against
   regen and hand-turned motors [1, §3.1]. Section 2 shows that the regen
   margin is real but thin.
2. Hot-plugging into ceramics rings the input to "almost twice its DC
   value" [12]. For 24 V that is ~48 V, against a 32 V absolute maximum.
   C1 damps this (section 4), but a terminal-block board will get
   hot-plugged eventually.
3. The PSU's OVP sits above the driver's absolute maximum (section 1).
   That needs a crowbar-style clamp, with F1 upstream to clear it if the
   TVS conducts continuously.

**Part: Vishay SMBJ24A, unidirectional, SMB.** [13]

- V_WM 24 V, V_BR 26.7–29.5 V at 1 mA, I_R ≤ 1 µA at V_WM, V_C 38.9 V at
  I_PP 15.4 A, 600 W at 10/1000 µs.
- Its V_BR(max) of 29.5 V sits below the 32 V inductive-load absolute
  maximum. So for the low-current, mJ-scale events that actually occur
  here (regen, ringing), VM is clamped below the chip's limit.
- At full surge current it clamps at 38.9 V, above the absolute maximum.
  No standard TVS achieves both a ≥24 V stand-off and a sub-32 V clamp at
  amps. Large surges are not expected on an indoor 24 V rail fed by a
  short DC cable.
- Placement: after Q1, so a reversed input is blocked by Q1 instead of
  forward-biasing D1 and blowing F1. Place it close to C1 and the VM pin.

The cost of that tight clamp: the LRS-50/150 are adjustable to 28.8 V [3][4].
With the SMBJ24A fitted, **the PSU must stay at 24 V nominal (≤ 24.5 V)**.
Trimming it up makes D1 conduct. The alternative is the SMBJ26A (V_WM 26 V,
V_BR 28.9–31.9 V, V_C 42.1 V) [13]. That tolerates trimming up to ~26 V,
but its clamp starts only just below the 32 V limit. See open question 1.

## 4. VM bulk capacitor: re-justified

HARDWARE.md today: **220 µF 35 V** electrolytic at the driver (100 µF 50 V
acceptable), plus an optional 100 nF ceramic.

What the sources say:

- TMC2209 datasheet, §3.1: "Use low ESR capacitors for filtering the power
  supply. The capacitors need to cope with the current ripple cause by
  chopper operation. A minimum capacity of 100 µF near the driver is
  recommended for best performance." It also says: "Low ESR electrolytic
  capacitors are recommended for VS filtering. Ensure sufficient capacity
  on VS to limit supply ripple, and to keep power slopes below 1 V/µs.
  Failure to do so could result in destructive currents via the charge
  pump capacitor." [1]
- The same datasheet, §21.3, asks for "an electrolytic capacitor (47 or
  larger is recommended) and a ceramic capacitor, placed close to the
  device". [1]
- Trinamic's own TMC2209 training module (hosted by DigiKey): "Best to
  choose a low-ESR capacitor of minimum 330 µF." [14]
- BTT's TMC2209 V1.2 module schematic shows only ceramics on board (100 nF
  / 1 µF, 50 V); there is no bulk electrolytic [2]. I assumed V1.3 is the
  same and did not verify it. **So the carrier must supply the bulk
  capacitance.**
- An electrolytic's ESR is what damps hot-plug ringing; ceramics alone
  ring to ~2× [12].

Checks for 220 µF:

- **Slope:** dV/dt = I/C. Reaching 1 V/µs on 220 µF would take 220 A, so
  the 1 V/µs rule is met by a wide margin. With only the module's ~1 µF of
  ceramics, 1 A of inrush already gives 1 V/µs.
- **Ripple current:** each bridge's input current is a pulse train of
  amplitude |i_coil|. The capacitor's RMS ripple is I × √(D(1−D)), the
  same form as a buck input capacitor [15, eq. 3], with a maximum of
  0.5 × I. Two bridges adding in quadrature give √2 × 0.5 × 1.36 A ≈
  **≤ 1.0 A RMS** worst case. C1 needs a ripple rating ≥ 1 A at
  100 kHz / 105 °C. That is the real reason to specify "low-ESR", not
  just the µF figure.
- **Regen:** 220 µF alone gives the ~26–28 V figures in section 2. That is
  acceptable with the P-FET (the PSU's capacitance helps) and the TVS
  (backstop).

**Decision: keep 220 µF, raise the voltage to 50 V, and specify low-ESR
with ≥ 1 A ripple.**

- **50 V, not 35 V.** D1 can clamp up to 38.9 V at full surge [13], and
  the LRS-50's OVP allows up to 36.6 V [3]. Both exceed a 35 V part's
  rating. 35 V stays acceptable, as HARDWARE.md says, but on a new board
  the 50 V part is cheap insurance.
- **Not 330 µF+**, despite Trinamic's training figure. A larger C1 raises
  the hot-plug I²t through F1 linearly (section 5). With the P-FET the
  PSU's capacitance supplements it anyway. 220 µF is 2.2× the datasheet's
  stated minimum.
- If the Schottky fallback is used, go to ≥ 470 µF and pick F1 for that
  I²t.

Keep 1 µF + 100 nF, 50 V X7R ceramics at the VM pin for HF decoupling. The
exact C1 part number should be picked from PCBWay's live stock against
these specs. Panasonic FR (radial) or FK (SMD) series are typical
candidates; I did not verify their datasheet ratings in this research.

## 5. Fuse

Requirements:

- **Running current:** Littelfuse's guidance is to load a fuse "not more
  than 75% of the nominal rating" at 25 °C, and to derate further for
  ambient temperature [17]. For 0.86 A that means ≥ 1.15 A, and
  ≥ ~1.5 A for a warm enclosure.
- **Pulse (hot-plug inrush):** "select a fuse such that the I²t of the
  waveform is no more than 20% of the nominal melting I²t rating of the
  fuse" [17]. Charging C from a stiff source dissipates ½CV² in the loop
  resistance R regardless of the inductance, so I²t ≈ C·V²/(2R).
  - With C = 220 µF and V = 24 V: 63 mJ / R.
  - Installed, with 5 m of 0.75 mm² (0.23 Ω round trip) plus ESR and
    contacts: R ≈ 0.5 Ω, so **≈ 0.13 A²s**.
  - On the bench with short leads: R ≈ 0.15 Ω, so **≈ 0.42 A²s**.
  - Normal power-up through the mains switch is not a concern. The
    LRS-50's 30 ms output rise [3] limits inrush to C·dV/dt ≈ 0.18 A.
- **Voltage:** ≥ 24 V working.

**Polyfuse rejected.** The only Bourns MF-MSMF part with enough voltage and
hold current is the MF-MSMF110/33X [18]:

- I_hold 1.10 A at 23 °C, but **0.93 A at 40 °C and 0.83 A at 50 °C**.
  That is at or below the 0.86 A worst-case running current inside an
  enclosure that also holds a heatsinked driver.
- R1max 0.20 Ω.
- V_max 33 V, below the LRS-50's possible 36.6 V OVP fault voltage [3].
- A PTC adds nuisance-trip risk without adding protection that matters
  here.

**SMD slow-blow fuse chosen.** The Bourns SF-1206S series [19] is a 1206
thin-film slow-blow fuse. It clears within 5 s max at 250 %, and holds at
100 % for 4 h minimum.

| Part | Rating | R typ | Melt I²t | Running 0.86 A as % of rating | Hot-plug I²t as % of melt (installed / bench) |
|---|---|---|---|---|---|
| SF-1206S200-2 | 2 A, 32 VDC | 41 mΩ | 0.339 A²s | 43 % | 38 % / 124 % ✗ |
| SF-1206S250-2 | 2.5 A, 32 VDC | 33 mΩ | 0.605 A²s | 34 % | 21 % / 69 % |
| **SF-1206S300-2** | **3 A, 32 VDC** | **23 mΩ** | **0.933 A²s** | **29 %** | **14 % / 45 %** |
| SF-1206S400-2 | 4 A, 32 VDC | 15.5 mΩ | 1.537 A²s | 22 % | 8 % / 27 % |

All of these interrupt 50 A at 32 VDC [19].

**Choice: SF-1206S300-2 (3 A slow-blow).**

- It meets the 20 % pulse rule for installed units.
- In the bench short-lead worst case it is at ~45 %. That survives
  occasional hot-plugs, but is not a "repeat thousands of times" margin.
  If Lino hot-plugs 24 V on the bench routinely, use the 4 A part.
- What it protects: with a single LRS-50 (overload hiccup at 2.4–3.3 A
  [3]), the PSU limits a short before the fuse opens, and that is fine.
  The fuse is for:
  - a short on a **shared LRS-150** (up to ~9 A available [4]), where one
    unit must not drag the others down;
  - the D1 crowbar case after a PSU OVP failure;
  - a driver that fails short.

Gap: the SF-1206S ≥ 1.5 A parts are rated 32 VDC [19], below the LRS-50's
36.6 V worst-case OVP point [3]. A fuse with a ≥ 50 V rating would close
that. The Littelfuse Nano2 0452 slo-blo series is the usual candidate, but I
could not retrieve its datasheet in this research (403), so verify it before
substituting.

**Interaction with HARDWARE.md's per-branch "~2 A fast-blow" fuse** (shared
PSU section). A 2 A *fast* fuse upstream of a board with a 3 A slow-blow will
be the one that pops on hot-plug inrush. Once the carrier has F1, either drop
the branch fuse or make it ≥ F1 and slow-blow. This is a doc change for
later (open question 2), and is not made here.

## 6. Sourcing note (PCBWay)

Every recommended part is a mainstream, multi-distributor part:

- SMBJ24A: second-sourced by many TVS vendors.
- BZT52C12: Diodes, Nexperia, onsemi and others.
- SS34: multi-source.
- AOD4185: widely stocked DPAK.
- SF-1206S: Bourns.

I could not query LCSC or PCBWay stock from here (access denied), so confirm
each part on the PCBWay BOM quote. If an exact P/N is missing, the
constraints for substitutes are:

| Part | Substitute must have |
|---|---|
| Q1 | V_DS ≤ −40 V, V_GS ±20 V, R_DS(on) ≤ 50 mΩ at −10 V, D2 clamp kept |
| D1 | V_WM 24 V, V_BR(max) < 32 V, ≥ 600 W SMB |
| F1 | slow-blow, ≥ 32 V, melt I²t ≥ 0.9 A²s, 3 A |
| C1 | ≥ 220 µF, ≥ 50 V (35 V min), ripple ≥ 1 A, 105 °C |

## 7. To confirm on the bench

1. **Bus current**, clamp meter on 24 V: during a full-speed move with each
   motor type, during a deliberate stall, and at the energised standstill.
   This replaces the 0.9 A design figure. HARDWARE.md already suggests
   this.
2. **2HS60 coil resistance** with a multimeter, cold. It is the biggest
   unknown in section 1.
3. **VM overshoot**, scope on VM, with the P-FET fitted and the PSU's
   capacitance present: during the end-of-move decel and EN̅ going high,
   and while hand-pulling a blind down with the driver disabled. This
   confirms the regen margin against the SMBJ24A's 26.7 V V_BR(min).
4. **Hot-plug**, 24 V into the terminal with the actual cable: scope VM
   peak and, if possible, the inrush current. This validates the
   F1 I²t estimate and that C1 damps the ringing.

## Open questions for Lino

1. **Are any PSUs trimmed above 24 V, or might they be?** If not, use the
   SMBJ24A (tighter clamp). If they are, use the SMBJ26A and accept a clamp
   closer to the chip's 32 V limit.
2. **Branch fusing on the shared LRS-150.** Should HARDWARE.md drop the
   per-branch "~2 A fast-blow" once the carrier has F1, or change it to
   ≥ 3 A slow-blow?
3. **Is hot-plugging 24 V on the bench a routine habit?** If yes, use F1 =
   4 A (SF-1206S400-2).
4. **DPAK footprint acceptable?** The AOD4185 is overkill on current; a
   SOT-23 40 V P-FET would do if board space matters. I could not retrieve
   a primary datasheet for a SOT-23 candidate (Vishay's site refused
   automated fetches), so one still needs choosing against the
   constraints in section 6.

## Sources

1. ADI/Trinamic, *TMC2209 Datasheet*, rev 1.09 (2023-02-16). Read from a
   byte copy (sha256 `1d9e8eed…a7b0893`) because analog.com timed out:
   https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf
   (copy: https://github.com/slimcdk/esphome-custom-components/blob/master/esphome/components/tmc2209/docs/TMC2209_datasheet_rev1.09.pdf).
   Cited: §3.1 standard application circuit notes, §8 sense resistors, §19
   absolute maximum ratings, §20.1 operational range, §20.2 R_DS(on), §21.3
   supply filtering.
2. BIGTREETECH, *TMC2209 V1.2 manual* and *schematic*:
   https://github.com/bigtreetech/BIGTREETECH-TMC2209-V1.2 (files
   `manual/TMC2209-V1.2-manual.pdf`, `Schematic/TMC2209-V1.2.pdf`). This is
   the V1.2 board; no public V1.3 repository was found.
3. Mean Well, *LRS-50 spec*, file date 2025-04-07:
   https://www.meanwell.com/Upload/PDF/LRS-50/LRS-50-SPEC.PDF
4. Mean Well, *LRS-150 spec*, file date 2025-04-07:
   https://www.meanwell.com/Upload/PDF/LRS-150/LRS-150-SPEC.PDF
5. Vishay, *SS32–SS36 Schottky rectifier*, doc 88751, rev 2020-04-23:
   https://www.vishay.com/docs/88751/ss32.pdf
6. TI, *LM74700-Q1 ideal diode controller*, SNOSD17G:
   https://www.ti.com/lit/ds/symlink/lm74700-q1.pdf
7. TI, *TPS2660x 60-V 2-A industrial eFuse*, SLVSDG2G:
   https://www.ti.com/lit/ds/symlink/tps2660.pdf
8. TI, *Basics of Ideal Diodes*, SLVAE57B:
   https://www.ti.com/lit/ab/slvae57/slvae57.pdf
9. TI, *Reverse Current/Battery Protection Circuits*, SLVA139:
   https://www.ti.com/lit/an/slva139/slva139.pdf
10. Alpha & Omega, *AOD4185 40 V P-channel MOSFET*:
    https://www.aosmd.com/res/datasheets/AOD4185.pdf
11. Diodes Inc, *BZT52C2V0–BZT52C51 zener*, DS18004:
    https://www.diodes.com/assets/Datasheets/ds18004.pdf
12. TI, *Hot plugging DC/DC converters safely*, SSZTDA5:
    https://www.ti.com/lit/ta/ssztda5/ssztda5.pdf
13. Vishay, *SMBJ5.0A thru SMBJ188A TVS*, doc 88392, rev 2024-01-09:
    https://www.vishay.com/docs/88392/smbj.pdf
14. Trinamic, *TMC2209 Silent Stepper Motor Driver* training module, slide 6
    (DigiKey PTM):
    https://www.digikey.de/de/ptm/t/trinamic-motion-control-gmbh/tmc2209-silent-stepper-motor-driver/tutorial?slide=6
15. TI, *How to select input capacitors for a buck converter*, SLYT670:
    https://www.ti.com/lit/an/slyt670/slyt670.pdf
16. 17HS4401 parameters (1.7 A, 1.5 Ω, 2.8 mH) from the MotionKing 17HS
    catalog. This is a **secondary** host and the only one found:
    https://www.datasheetcafe.com/17hs4401-datasheet-stepper-motor/
17. Littelfuse, *Fuseology Design Guide*:
    https://info.littelfuse.com/hubfs/Fuseology%20Design%20Guide.pdf
18. Bourns, *MF-MSMF series PTC*, rev BE 09/26:
    https://www.bourns.com/docs/product-datasheets/mf-msmf.pdf
19. Bourns, *SinglFuse SF-1206S slow-blow*, rev J 04/21:
    https://www.bourns.com/docs/product-datasheets/sf-1206s.pdf
