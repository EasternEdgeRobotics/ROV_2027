#!/usr/bin/env python3
"""Sets BlueStar's lights from brightness levels on stdin, one per line, 0 to 255.

StarStruck starts this and writes to it. To test it alone, run it and type
numbers. Talks to the 2026 backend through rosbridge, so it needs roslibpy but
no ROS install.
"""

import argparse
import sys
import threading
import time

import roslibpy

# Sent again at this rate. The first messages after advertising can be dropped
# while rosbridge sets up the topic, and a restarted backend picks the level
# up without StarStruck sending anything.
RESEND_SECONDS = 0.2

# Where the 2026 frontend starts its servos. 0 would swing them to one end.
SERVO_REST = 127


def pilot_input(brightness):
    # The whole message, because the backend takes it as the pilot's input.
    # Power 0 keeps the thrusters off.
    message = {
        name: 0
        for name in [
            "surge", "sway", "heave", "pitch", "yaw", "roll",
            "dc_motor_1", "dc_motor_2", "power_multiplier",
            "surge_multiplier", "sway_multiplier", "heave_multiplier",
            "pitch_multiplier", "yaw_multiplier", "roll_multiplier",
            "configuration_mode_thruster_number",
        ]
    }

    for name in [
        "open_claw", "close_claw", "heave_up", "heave_down", "pitch_up",
        "pitch_down", "roll_cw", "roll_ccw", "enter_auto_mode",
        "configuration_mode",
    ]:
        message[name] = False

    for i in range(1, 5):
        message[f"servo_{i}_angle"] = SERVO_REST

    message["led_1_brightness"] = brightness
    message["led_2_brightness"] = brightness
    return roslibpy.Message(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True, help="BlueStar's address")
    parser.add_argument("--port", type=int, default=9090, help="rosbridge port")
    args = parser.parse_args()

    ros = roslibpy.Ros(host=args.host, port=args.port)
    ros.run()

    topic = roslibpy.Topic(ros, "/pilot_input", "eer_interfaces/msg/PilotInput")
    topic.advertise()

    level = 0
    done = False
    changed = threading.Event()

    def read_levels():
        nonlocal level, done

        for line in sys.stdin:
            try:
                level = max(0, min(255, int(line)))
                changed.set()
            except ValueError:
                print(f"lights.py: ignoring {line.strip()!r}", file=sys.stderr)

        done = True
        changed.set()

    threading.Thread(target=read_levels, daemon=True).start()

    # A new level goes out straight away, otherwise the countdown blinks would
    # wait on the next resend.
    while not done:
        topic.publish(pilot_input(level))
        changed.wait(RESEND_SECONDS)
        changed.clear()

    # stdin closed: StarStruck has quit. Leave the lights off.
    for _ in range(3):
        topic.publish(pilot_input(0))
        time.sleep(RESEND_SECONDS)

    topic.unadvertise()
    ros.terminate()


if __name__ == "__main__":
    main()
