# StarStruck
A photo booth on an ROV camera. The ROV's lights are the flash, and photos print on a Canon Ivy 2.

## Running
Build as in the [top-level instructions](../../AGENTS.md#building), then:
```sh
./build/apps/starstruck/eer_starstruck.app/Contents/MacOS/eer_starstruck   # macOS
./build/apps/starstruck/eer_starstruck                                     # Linux
```
Open Settings (F1) and enter the camera's WHEP URL.

## Using the booth
Take photo counts down, blinking the lights each second, then turns the lights to full and takes the photo. The photo stays on screen until you choose Print or Retake.

| Key | |
|---|---|
| Space | Take photo, or retake |
| Up, Down | More or fewer copies, up to 10 |
| Enter | Print |
| Escape | Cancel the countdown, or retake |
| F1 | Settings |
| F11 | Full screen |

Every photo is saved as a TIFF in `Pictures/starstruck/<start time>/`. Print copies it into the print folder once per copy, `Pictures/starstruck/print` unless set otherwise.

## Lights
StarStruck starts the program in `lights_command` and writes one brightness per line to its stdin, from 0 to 255. Closing stdin means StarStruck has quit. The levels for idle, the countdown blink and the flash are in Settings.

For BlueStar, use `scripts/lights.py`. It publishes to `/pilot_input` through the rosbridge server BlueStar's backend already runs. Install its requirements with `pip install -r scripts/requirements.txt`, then set the command in `starstruck_config.json`:
```json
"lights_command": ["python3", "/path/to/apps/starstruck/scripts/lights.py", "--host", "192.168.137.200"]
```
Do not run it alongside the 2026 frontend, since both publish the pilot's input. It sends power 0, so the thrusters stay off.

For another vehicle, write a script that reads the same lines and point `lights_command` at it.

## Printing
`scripts/print_watcher.py` prints each photo that appears in the print folder, then moves it to `printed/`. A photo that fails three times moves to `failed/`. It runs on Linux only, because the printer library needs BlueZ.

1. Clone [dtgreene/ivy2](https://github.com/dtgreene/ivy2) and follow its README to install PyBluez and pair the printer.
2. Run `sudo hciconfig 0 sspmode 0`. Do this after every reboot, or prints fail with "Invalid exchange".
3. Start the watcher with the printer's Bluetooth address:
   ```sh
   PYTHONPATH=/path/to/ivy2 python3 scripts/print_watcher.py XX:XX:XX:XX:XX:XX
   ```

If the watcher is on another machine, share the print folder and pass it with `--dir`. The printer refuses to print below 30% battery.

## Config
`starstruck_config.json`, in the same directory as the frontend's config.

| Field | |
|---|---|
| `vehicle` | Written into each photo's metadata |
| `camera` | `url`, `video_caps`, `audio_caps`, `flip_vertical`, `flip_horizontal` |
| `countdown_seconds` | 1 to 10 |
| `idle_brightness` | Lights outside the countdown, 0 to 255 |
| `blink_brightness` | Lights for the first half of each countdown second |
| `flash_brightness` | Lights while the photo is taken |
| `flash_lead_ms` | How long the lights are at full before the photo is taken. Raise it if photos come out dark |
| `lights_command` | The lights program and its arguments. Empty for no lights |
| `print_dir` | The print folder. Empty for `Pictures/starstruck/print` |

## Files
| | |
|---|---|
| `src/main.cpp` | The booth, the settings window and the key handling |
| `src/Config.cpp` | Loading and saving the config file |
| `src/Lights.cpp` | Running the lights program |
| `scripts/lights.py` | BlueStar's lights over rosbridge |
| `scripts/print_watcher.py` | Printing the print folder on an Ivy 2 |
