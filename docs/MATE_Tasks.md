# 2027 MATE ROV Tasks
MATE ROV 2027 Explorer Tasks.
Last Update: September 17th 2026 - 2027 MATE Floats Preview Mission

## Contents
- [Task 1](#task-1)
- [Task 2](#task-2)
- [Task 3](#task-3)
  - [Vertical Profile Sequence](#vertical-profile-sequence)
  - [Required Data Packet Fields](#required-data-packet-fields)

## Task 1
TBA

## Task 2
TBA

## Task 3
Design and construct an operational vertical profiling float

- **Game Plan – 5 Points** 
    | Task | Points |
    |------|--------|
    | Design, build, and submit DOC-004 for a vertical profiling float with a buoyancy engine| 5 |
- **Mic'd Up – 5 Points**
    | Task | Points |
    |------|--------|
    | Float transmits at least one defined data packet to the shore station before descending | 5 |
- **Vertical Profile 1 - 25 Points, -5 penalty**
    | Task | Points |
    |---|---|
    | Complete vertical profile via buoyancy engine | 10 |
    | Transmit ≥10 data packets to station | 5 |
    | Data confirms 2.0 m depth held for 30 sec (7 packets, 5 sec apart, within 1.67–2.33 m) | 5 |
    | Temp & light readings within range every 0.5 m on ascent (4.0 m → 0.0 m) | 5 |
     - **Penalty:** Float touches pool bottom 
- **Vertical Profile 2 - 25 Points, -5 penalty** 
    - Same as above
- **Review the Play - 10 Points**
    | Task | Points |
    |------|--------|
    | Graph **temperature vs. depth** (temp on X-axis, depth on Y-axis) | 5 |
    | Graph **light vs. depth** (light on X-axis, depth on Y-axis) | 5 |
    - Must be done on a computer; no hand-drawn graphs
    - If float fails to collect data, MATE-supplied data may be used instead (forfeits communication/sensor points)

---

### Vertical Profile Sequence
1. Surface → descend to **2.0 m**, hold for **30 seconds**
2. Descend to **4.0 m**
3. Ascend to surface, collecting data every **0.5 m** (±20 cm tolerance per depth)

### Required Data Packet Fields
- Company number
- Time (UTC, local, or float time)
- Pressure (Pa or kPa) and/or depth (m or cm)
- Temperature (°C)
- Light (any appropriate unit: lux, W/m², V, etc.)