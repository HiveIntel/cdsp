# Aggressive-Driving Data Simulator

The simulator generates VSS JSONL data for every JSON scene in `scenes/` and a
speed/steering-angle plot for each scene. It can then replay one generated scene
directly to the Information Layer.

## Setup

Python 3.10 or later is required. Install the two Python dependencies once:

```bash
python3 -m pip install -r requirements.txt
```

## Define scenes

Create one JSON file per scene under `scenes/`. The filename does not determine
the output name; the required `name` property does.

```json
{
  "name": "scene01",
  "duration_seconds": 30,
  "sampling": {
    "steering_angle_hz": 10,
    "speed_hz": 5
  },
  "aggressive_events": [
    {
      "start_second": 5,
      "end_second": 9
    }
  ]
}
```

| Property | Description |
| --- | --- |
| `name` | Unique output filename stem. |
| `duration_seconds` | Positive scene duration. |
| `sampling.steering_angle_hz` | Angle samples per second; default is 10. |
| `sampling.speed_hz` | Speed samples per second; default is 5. |
| `aggressive_events` | Optional non-overlapping aggressive-driving windows. |

Rates must be positive divisors of 1000, such as `1`, `2`, `4`, `5`, `10`,
`20`, `25`, `50`, or `100`. The simulator chooses timestamp offsets in code so
that signal groups never share a timestamp:

- `CurrentLocation` is fixed at 1 Hz at exactly `*.000`; latitude and longitude
  are contained in the same record and have the same timestamp.
- Steering angle and speed are separate records with rate-dependent offsets.

The route is deliberately hard-coded to a short path in Berlin because this is
a model-test simulator, not a general routing engine. Each run uses the current
UTC time, rounded to a whole second, as the common start time for every scene.

## Generate data

The generator accepts no arguments. It validates every `scenes/*.json` file
before writing output.

```bash
python3 generate_stream.py
```

For every scene it writes ignored generated files:

```text
data/<scene-name>.jsonl
data/plots/<scene-name>_speed_steering.png
```

The plot uses blue for speed, red for steering angle, pale orange for the full
aggressive event, and an orange outline for its three-second inference window.

## Replay one scene to the Information Layer

Start the Information Layer, generate the data, then replay just one scene:

```bash
./play_stream scene01
```

Equivalently:

```bash
python3 play_stream.py scene01
```

The player preserves the generated timing and sends JSON-RPC `set` messages to
`ws://127.0.0.1:8080` for `VIN_1805`. To use another endpoint or vehicle ID,
set environment variables before running it:

```bash
INFORMATION_LAYER_URL=ws://localhost:8080 \
VEHICLE_OBJECT_ID=VIN_1805 \
./play_stream scene01
```
