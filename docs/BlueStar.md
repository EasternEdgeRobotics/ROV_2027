# BlueStar
BlueStar is Eastern Edge's 2026 ROV for the MATE ROV Competition, where it placed 1st in the Explorer class. BlueStar is used as a development platform before Blue Mist, the 2027 ROV, is ready.

## Hardware
BlueStar uses a Raspberry Pi 5 SBC running Raspberry Pi OS 13 Trixie connected to a Raspberry Pi Pico using the Eastern Edge LapisHAT. LapisHAT is a custom PCB built around a Pi Pico microcontroller that sits on top of the Raspberry Pi 5 and centralizes control of all 6 thrusters, the LED, and the DC motor, simplifying cabling within the enclosure. LapisHAT supports 6 thrusters, 4 servos, 2 DC motors and 2 LEDs.

### Cameras
BlueStar features 2 waterproofed cameras based on the Arducam IMX708 Ultra Wide B0304 USB cameras, these cameras are running at 720p 30fps with ffmpeg doing software transcoding from MJPEG to H264, this is then fed into MediaMTX to provide a WebRTC stream. The camera assemblies are potted in epoxy and silicone rather than using o-ring enclosures. Camera 1 is front-mounted for a top-down view, and Camera 2 is rear-mounted for the main piloting view.

### Power System
Two Murata 600W 48V-to-12V DC-DC converters in parallel supply power to the propulsion system, and one Murata 75W 48V-to-5V converter powers the control system. All converters are socketed so they can be swapped without soldering. Custom aluminum bus bars handle 12V distribution.

### Chassis
The chassis is built from 20 mm × 20 mm aluminum T-slot extrusion with self-aligning T-nuts for quick component and tool skid swapping. Dimensions: 610 × 590 × 340 mm, mass: 9.9 kg. Approximately 700 g of ballast is mounted on the bottom for stability.

### Electronics Enclosure
The electronics enclosure measures 280 × 120 × 84 mm, sealed with an o-ring and transparent polycarbonate sheet.

### Thrusters
BlueStar uses 6 Blue Robotics T200 thrusters in a 5-DOF configuration: 2 vertical (heave, roll) and 4 horizontal at 45° (surge, sway, yaw). No pitch control.

### Tether
A 20-meter neutrally buoyant tether manufactured by Derul Cable with 2 × 14 AWG conductors for power and 4 twisted pairs for Ethernet, enclosed in a polyurethane outer jacket. Metal Kellems grips on both ends provide strain relief.