# 2027 MATE ROV Tasks
MATE ROV 2027 Explorer Tasks.
Last Update: October 17th 2026 - 2027 Product Demonstration and Specs Briefing (Task 3 points from the 2027 MATE Floats Preview Mission, September 17th 2026)

## Contents
- [Task 1](#task-1)
- [Task 2](#task-2)
- [Task 3](#task-3)
  - [Vertical Profile Sequence](#vertical-profile-sequence)
  - [Required Data Packet Fields](#required-data-packet-fields)
- [Specs](#specs)
- [Size and Weight Points](#size-and-weight-points)

## Task 1
Zone Defense: Protecting the Pacific

- **Seagrass Field Championship**
    | Task | Points |
    |------|--------|
    | Draft the All-Stars: Collect seagrass from a healthy seagrass bed | TBA |
    | Run the Sideline: Survey seagrass by following a transect line through the seagrass bed | TBA |
    | Plant the Starting Lineup: Deploy seagrass seedlings in designated locations | TBA |
    | Instant Replay Review: Compare images to determine the recovery of seagrass over time | TBA |
    | Check the Playing Conditions: Determine the turbidity of the seagrass bed | TBA |
    | Into the Net: Deploy a new crab trap in the designated location | TBA |
    | Into the Net: Return the old crab trap to the surface, side of the pool | TBA |
- **Mid-water Play by Play**
    | Task | Points |
    |------|--------|
    | Develop a program to autonomously identify species in a midwater transect | TBA |
    | You Make the Call: Identify species in the midwater transect autonomously | TBA |
    | You Make the Call: Identify species in the midwater transect manually | TBA |
- **Deep Sea Relay**
    | Task | Points |
    |------|--------|
    | Anchor the Starting Block: Install the primary node in the designated location | TBA |
    | Power Play: Remove the cover from the generator port | TBA |
    | Power Play: Install the power connector into the generator port | TBA |
    | Sensor, Set, Match: Install the pressure and tilt sensor in the designated location | TBA |
    | Sensor, Set, Match: Connect the pressure and tilt sensor to the primary node | TBA |
    | Sensor, Set, Match: Level the pressure and tilt sensor | TBA |

## Task 2
Zero-G Training Camp: From Sea to Space

- **Solar Panel Pit Stop**
    | Task | Points |
    |------|--------|
    | Debris on the Track: Inspect the solar panel array and identify the damaged area | TBA |
    | Raise the Red Flag: Disengage the breaker on the power line | TBA |
    | Raise the Red Flag: Unplug main power from the solar panel array | TBA |
    | Enter the Pits: Remove the damaged solar panel | TBA |
    | Enter the Pits: Install a new solar panel | TBA |
    | Wave the Green Flag: Plug in main power from the solar panel array | TBA |
    | Wave the Green Flag: Engage the breaker on the power line | TBA |
- **Orbital Fumble Recovery**
    | Task | Points |
    |------|--------|
    | Locate: Move ROV to designated location | TBA |
    | Secure: Recover the tool before it hits the bottom | TBA |
    | Secure: Recover the tool after it hits the bottom | TBA |
    | Protect and Advance: Return the tool to the surface, side of the pool | TBA |
- **Video Review: AI Predictions From Satellite Imagery**
    | Task | Points |
    |------|--------|
    | Video Assistant Referee: Use an image of a hurricane on a map to determine the latitude and longitude of its eye | TBA |
    | Use the Telestrator: Input data into mapping software to create a detailed track of the hurricane | TBA |
    | Expert Analysis: Use the track positions to generate a prompt for AI to create a graphic of the hurricane's movement and predicted path | TBA |
- **Catch the Pass: Cargo Payload Docking**
    | Task | Points |
    |------|--------|
    | 1st Down Completion: Maneuver cargo payload into the docking bay autonomously | TBA |
    | 1st Down Completion: Maneuver cargo payload into the docking bay manually | TBA |
- **Call in the Play: Install a New Space to Ground ANTenna (SGANT)**
    | Task | Points |
    |------|--------|
    | Install the new SGANT | TBA |
    | Connect the power and communications link | TBA |
- **Technical Difficulties: Loss of Communications Recovery Testing**
    | Task | Points |
    |------|--------|
    | The Crew is Working: Before the competition, describe the loss of communication protocol | TBA |
    | Trying Our Best to Restore Systems: Surface after communications are disengaged, fully autonomous | TBA |
    | Trying Our Best to Restore Systems: Surface after communications are disengaged, initiate program | TBA |
    | Trying Our Best to Restore Systems: Surface after communications are disengaged, buoyancy | TBA |
    | Back on the Air: Signal to the mission station | TBA |

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

## Specs
| Spec | Requirement |
|------|-------------|
| Power | 48 V, 30 A DC. Conversion to lower voltages must be done on the ROV, not topside |
| Required components | SBS50 Anderson Powerpoles, Littelfuse fuse (30 A or less) and Littelfuse fuse holder |
| Fluid power | Pneumatics and hydraulics permitted if they follow the manual's specifications |
| Lasers | Not permitted (new in 2027) |
| Camera | Required |
| Depth | Up to 6 m at the World Championship |
| Maximum size | 100 cm in any dimension |
| Maximum weight | 22 kg |
| Launch | Must be safely launchable by hand |

## Size and Weight Points
Size is length + width + height of the ROV with all tools and components. Neither measurement includes the tether. Weight also excludes the topside control system, the vertical profiling float and any independent sensors removable from the ROV.

| Size | Points |
|------|--------|
| 120 cm | +30 |
| 140 cm | +10 |
| 160 cm | +5 |
| 200 cm | 0 |

| Weight | Points |
|--------|--------|
| 8 kg | +30 |
| 8.01 - 12 kg | +10 |
| 12.01 - 18 kg | +5 |
| 18.01 - 22 kg | 0 |

- Any ROV with one dimension over 1 m will not be allowed to compete
- Any ROV over 22 kg will not be allowed to compete