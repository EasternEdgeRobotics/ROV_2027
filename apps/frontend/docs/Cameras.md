# Cameras
Each camera is a WebRTC stream fetched over WHEP, which is what MediaMTX on the ROV serves.

## URLs
Enter a URL such as `http://192.168.137.200:8889/cam/whep` in Settings > Cameras. A URL is dialled when you leave the field or press Enter, never while you are typing. Press Save to keep it for next time.

The Status column beside each camera shows its state.

| Status | Meaning |
|---|---|
| No URL | Nothing configured |
| Invalid URL | Not an `http://` or `https://` address |
| Connecting... | Dialling |
| Connected, waiting for video... | The link is up and the camera has not sent a keyframe yet. Look at the camera, not the network |
| Live (H264) | Showing video, with the codec in use |
| Retrying in Ns | Unreachable. Retries back off from 1s to 15s |

## Caps
Leave both caps fields blank for the defaults. The video default offers H.264, H.265, VP8, VP9 and AV1 and takes whichever the server has.

Set the audio caps to `none` for a camera with no audio, such as BlueStar's. Audio is otherwise offered and discarded, because some servers refuse a client that offers none.

## Flip and crop
Settings > Capture flips a camera and sets its crop, the virtual electrical tape. The crop is drawn over the live view and cut out of screenshots. Opaque shading suits a crop for one task. Translucent suits a crop that hides part of the ROV and is always on.

## Screenshot metadata
Each screenshot records which camera took it in EXIF. The details come from an `exif` block per camera in the config file. It is not editable in the app.

```json
"exif": {
    "model": "Front Camera",
    "position": "front camera, top-down view",
    "sensor": "IMX708",
    "sensor_active_px": [4608, 2592],
    "pixel_size_um": 1.4,
    "lens": "Fixed-focus 2.75mm F2.2 lens",
    "focal_length_mm": 2.75,
    "f_number": 2.2,
    "refraction_index": 1.333,
    "notes": { "shutter": "rolling", "focus_range": "1.5m-infinity" }
}
```
Those are the values for BlueStar's cameras.

| Field | Meaning |
|---|---|
| `model` | The camera's name. EXIF Model is the vehicle name, then this. Give each physical camera its own |
| `position` | Where it is mounted, in words |
| `sensor`, `lens` | Names. `lens` becomes EXIF LensModel |
| `sensor_active_px`, `pixel_size_um` | The sensor's full active area, used for pixel pitch and the 35 mm equivalent. Assumes the stream is the whole sensor scaled down |
| `focal_length_mm`, `f_number` | The physical lens |
| `refraction_index` | Multiplies the focal length. About 1.333 behind a flat window in water. Defaults to 1.0 |
| `notes` | Anything else, added to the EXIF user comment |

## Environment variables
These only affect the `webrtcbin` backend.

| Variable | Default | What it does |
|---|---|---|
| `EER_VIDEO_JITTER_LATENCY_MS` | `5` | Jitter buffering, 0 to 10000. Raising it did not change measured latency, so it is safe to raise on a noisy link |
| `EER_VIDEO_LOG_LATENCY` | off | Print each camera's pipeline latency once per session |
| `EER_VIDEO_STUN_SERVER` | none | Use a STUN server. Not needed when the ROV and laptop share a subnet |
