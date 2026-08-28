#!/usr/bin/env python3
"""Generate one VSS JSONL file and plot for every scene in scenes/."""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from math import gcd
from pathlib import Path
from typing import Any

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Pillow is required for plots. Install it with: python3 -m pip install Pillow") from exc


SCRIPT_DIR = Path(__file__).resolve().parent
SCENES_DIR = SCRIPT_DIR / "scenes"
DATA_DIR = SCRIPT_DIR / "data"
PLOTS_DIR = DATA_DIR / "plots"

WINDOW_SECONDS = 3
ANGLE_THRESHOLD = 210
ANGLE = "Vehicle.Chassis.SteeringWheel.Angle"
SPEED = "Vehicle.Speed"
LATITUDE = "Vehicle.CurrentLocation.Latitude"
LONGITUDE = "Vehicle.CurrentLocation.Longitude"

# This is deliberately a small, fixed route for model testing, not routing simulation.
ROUTE_DESCRIPTION = "Unter den Linden, Berlin"
ROUTE_START = (52.516275, 13.377704)
ROUTE_END = (52.517040, 13.395550)
LOCATION_RATE_HZ = 1
DEFAULT_ANGLE_RATE_HZ = 10
DEFAULT_SPEED_RATE_HZ = 5


@dataclass(frozen=True)
class Event:
    start_second: int
    end_second: int


@dataclass(frozen=True)
class Sampling:
    angle_rate_hz: int
    speed_rate_hz: int


@dataclass(frozen=True)
class Scene:
    name: str
    duration_seconds: int
    sampling: Sampling
    events: tuple[Event, ...]


@dataclass(frozen=True)
class Frame:
    timestamp: datetime
    speed: float
    angle: int
    latitude: float
    longitude: float


@dataclass(frozen=True)
class Record:
    timestamp: datetime
    signals: tuple[tuple[str, Any], ...]
    order: int


def timestamp_us(timestamp: datetime) -> int:
    return int(round(timestamp.timestamp() * 1_000_000))


def interpolate(start: float, end: float, ratio: float) -> float:
    return start + (end - start) * ratio


def active_event(second: float, events: tuple[Event, ...]) -> Event | None:
    return next((event for event in events if event.start_second <= second <= event.end_second), None)


def inference_end(event: Event) -> int:
    return min(event.start_second + WINDOW_SECONDS, event.end_second)


def speed_at(second: float, events: tuple[Event, ...]) -> float:
    event = active_event(second, events)
    if event is None:
        return round(7.5 + 1.5 * ((int(second) % 6) / 6), 2)
    duration = max(float(event.end_second - event.start_second), 1.0)
    return round(interpolate(12, 22, (second - event.start_second) / duration), 2)


def angle_at(second: float, events: tuple[Event, ...]) -> int:
    event = active_event(second, events)
    if event is None:
        return int(round(20 + 10 * ((int(second) % 8) / 8)))
    end = inference_end(event)
    if second <= end:
        duration = max(float(end - event.start_second), 1.0)
        return int(round(interpolate(-120, 120, (second - event.start_second) / duration)))
    duration = max(float(event.end_second - end), 1.0)
    return int(round(interpolate(120, 45, (second - end) / duration)))


def frame_at(offset_ms: int, scene: Scene, start_time: datetime) -> Frame:
    second = offset_ms / 1_000
    ratio = min(max(second / scene.duration_seconds, 0), 1)
    return Frame(
        timestamp=start_time + timedelta(milliseconds=offset_ms),
        speed=speed_at(second, scene.events),
        angle=angle_at(second, scene.events),
        latitude=round(interpolate(ROUTE_START[0], ROUTE_END[0], ratio), 6),
        longitude=round(interpolate(ROUTE_START[1], ROUTE_END[1], ratio), 6),
    )


def integer_value(value: Any, field_name: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field_name} must be an integer")
    try:
        result = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{field_name} must be an integer") from exc
    if result != value:
        raise ValueError(f"{field_name} must be an integer")
    return result


def load_scene(path: Path) -> Scene:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ValueError(f"cannot read {path.name}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in {path.name}: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError(f"{path.name} must contain a JSON object")
    try:
        name = data["name"]
        if not isinstance(name, str) or not name or Path(name).name != name:
            raise ValueError("name must be a non-empty filename stem")
        duration = integer_value(data["duration_seconds"], "duration_seconds")
        sampling_data = data.get("sampling", {})
        if not isinstance(sampling_data, dict):
            raise ValueError("sampling must be an object")
        sampling = Sampling(
            angle_rate_hz=integer_value(sampling_data.get("steering_angle_hz", DEFAULT_ANGLE_RATE_HZ), "sampling.steering_angle_hz"),
            speed_rate_hz=integer_value(sampling_data.get("speed_hz", DEFAULT_SPEED_RATE_HZ), "sampling.speed_hz"),
        )
        raw_events = data.get("aggressive_events", [])
        if not isinstance(raw_events, list):
            raise ValueError("aggressive_events must be an array")
        events = tuple(
            Event(
                integer_value(event["start_second"], f"aggressive_events[{index}].start_second"),
                integer_value(event["end_second"], f"aggressive_events[{index}].end_second"),
            )
            for index, event in enumerate(raw_events)
            if isinstance(event, dict)
        )
        if len(events) != len(raw_events):
            raise ValueError("every aggressive event must be an object")
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"invalid scene {path.name}: {exc}") from exc

    scene = Scene(name, duration, sampling, tuple(sorted(events, key=lambda event: event.start_second)))
    validate_scene(scene)
    return scene


def validate_rate(rate: int, field_name: str) -> None:
    if rate <= 0 or 1_000 % rate != 0:
        raise ValueError(f"{field_name} must be a positive divisor of 1000")


def validate_scene(scene: Scene) -> None:
    if scene.duration_seconds <= 0:
        raise ValueError(f"{scene.name}: duration_seconds must be greater than zero")
    validate_rate(scene.sampling.angle_rate_hz, "sampling.steering_angle_hz")
    validate_rate(scene.sampling.speed_rate_hz, "sampling.speed_hz")
    previous_end = -1
    for event in scene.events:
        if not 0 <= event.start_second < event.end_second <= scene.duration_seconds:
            raise ValueError(f"{scene.name}: events must satisfy 0 <= start < end <= duration")
        if event.start_second <= previous_end:
            raise ValueError(f"{scene.name}: events must not overlap")
        previous_end = event.end_second
        if abs(angle_at(event.start_second, scene.events) - angle_at(inference_end(event), scene.events)) <= ANGLE_THRESHOLD:
            raise ValueError(f"{scene.name}: event does not cross the steering-angle threshold")


def collides(phase_ms: int, period_ms: int, existing: list[tuple[int, int]]) -> bool:
    return any((phase_ms - other_phase) % gcd(period_ms, other_period) == 0 for other_phase, other_period in existing)


def phase_ms(period_ms: int, preferred_phase_ms: int, existing: list[tuple[int, int]]) -> int:
    for candidate in range(max(1, preferred_phase_ms), period_ms):
        if not collides(candidate, period_ms, existing):
            return candidate
    raise ValueError("cannot find a non-overlapping timestamp offset for the configured rates")


def stream_offsets(scene: Scene) -> tuple[int, int, int, int, int, int]:
    location_period = 1_000 // LOCATION_RATE_HZ
    angle_period = 1_000 // scene.sampling.angle_rate_hz
    speed_period = 1_000 // scene.sampling.speed_rate_hz
    location_phase = 0
    angle_phase = phase_ms(angle_period, angle_period // 4, [(location_phase, location_period)])
    speed_phase = phase_ms(speed_period, speed_period // 3, [(location_phase, location_period), (angle_phase, angle_period)])
    return location_period, angle_period, speed_period, location_phase, angle_phase, speed_phase


def records_for(scene: Scene, start_time: datetime) -> tuple[Record, ...]:
    location_period, angle_period, speed_period, location_phase, angle_phase, speed_phase = stream_offsets(scene)
    duration_ms = scene.duration_seconds * 1_000
    records: list[Record] = []
    for offset_ms in range(location_phase, duration_ms, location_period):
        frame = frame_at(offset_ms, scene, start_time)
        records.append(Record(frame.timestamp, ((LATITUDE, frame.latitude), (LONGITUDE, frame.longitude)), 0))
    for offset_ms in range(angle_phase, duration_ms, angle_period):
        frame = frame_at(offset_ms, scene, start_time)
        records.append(Record(frame.timestamp, ((ANGLE, frame.angle),), 1))
    for offset_ms in range(speed_phase, duration_ms, speed_period):
        frame = frame_at(offset_ms, scene, start_time)
        records.append(Record(frame.timestamp, ((SPEED, frame.speed),), 2))
    return tuple(sorted(records, key=lambda record: (record.timestamp, record.order)))


def serialise(record: Record) -> str:
    return json.dumps(
        {
            "recorded_at": record.timestamp.isoformat().replace("+00:00", "Z"),
            "signals": [
                {"timestamp_us": timestamp_us(record.timestamp), "name": name, "namespace": "vss", "value": value}
                for name, value in record.signals
            ],
        },
        separators=(",", ":"),
    )


def write_data(records: tuple[Record, ...], path: Path) -> None:
    with path.open("w", encoding="utf-8") as output:
        output.writelines(serialise(record) + "\n" for record in records)


def scale(value: float, source_min: float, source_max: float, target_min: int, target_max: int) -> int:
    if source_min == source_max:
        return (target_min + target_max) // 2
    return int(target_min + (value - source_min) / (source_max - source_min) * (target_max - target_min))


def write_plot(scene: Scene, start_time: datetime, path: Path) -> None:
    sample_rate = max(LOCATION_RATE_HZ, scene.sampling.angle_rate_hz, scene.sampling.speed_rate_hz)
    frames = [frame_at(index * 1_000 // sample_rate, scene, start_time) for index in range(scene.duration_seconds * sample_rate + 1)]
    width, height, left, right, top, bottom = 1280, 720, 90, 92, 70, 95
    plot_left, plot_right, plot_top, plot_bottom = left, width - right, top, height - bottom
    image = Image.new("RGB", (width, height), "white")
    draw, font = ImageDraw.Draw(image), ImageFont.load_default()
    timestamps = [frame.timestamp.timestamp() for frame in frames]
    speeds, angles = [frame.speed for frame in frames], [frame.angle for frame in frames]
    time_min, time_max = min(timestamps), max(timestamps)
    speed_min, speed_max = min(0, min(speeds)) - 2, max(speeds) + 2
    angle_min, angle_max = min(angles) - 30, max(angles) + 30
    for event in scene.events:
        start = scale((start_time + timedelta(seconds=event.start_second)).timestamp(), time_min, time_max, plot_left, plot_right)
        end = scale((start_time + timedelta(seconds=event.end_second)).timestamp(), time_min, time_max, plot_left, plot_right)
        draw.rectangle((start, plot_top, end, plot_bottom), fill=(255, 235, 198))
    for index in range(6):
        y = plot_top + index * (plot_bottom - plot_top) // 5
        draw.line((plot_left, y, plot_right, y), fill=(225, 225, 225))
    draw.rectangle((plot_left, plot_top, plot_right, plot_bottom), outline=(80, 80, 80), width=2)
    speed_points = [(scale(frame.timestamp.timestamp(), time_min, time_max, plot_left, plot_right), scale(frame.speed, speed_min, speed_max, plot_bottom, plot_top)) for frame in frames]
    angle_points = [(scale(frame.timestamp.timestamp(), time_min, time_max, plot_left, plot_right), scale(frame.angle, angle_min, angle_max, plot_bottom, plot_top)) for frame in frames]
    draw.line(speed_points, fill="#1f5fbf", width=3, joint="curve")
    draw.line(angle_points, fill="#bf3030", width=3, joint="curve")
    for event in scene.events:
        start = scale((start_time + timedelta(seconds=event.start_second)).timestamp(), time_min, time_max, plot_left, plot_right)
        end = scale((start_time + timedelta(seconds=inference_end(event))).timestamp(), time_min, time_max, plot_left, plot_right)
        draw.rectangle((start, plot_top, end, plot_bottom), outline=(210, 120, 30), width=3)
    draw.text((left, 24), scene.name.replace("_", " ").title(), fill=(30, 30, 30), font=font)
    draw.text((left, 44), f"{scene.duration_seconds}s, angle {scene.sampling.angle_rate_hz} Hz, speed {scene.sampling.speed_rate_hz} Hz, location 1 Hz", fill=(80, 80, 80), font=font)
    draw.text((15, top), "Speed", fill="#1f5fbf", font=font)
    draw.text((width - 86, top), "Angle", fill="#bf3030", font=font)
    draw.text((plot_right - 285, top + 8), "Blue: Vehicle.Speed", fill="#1f5fbf", font=font)
    draw.text((plot_right - 285, top + 25), "Red: SteeringWheel.Angle", fill="#bf3030", font=font)
    draw.text((plot_right - 285, top + 42), "Orange: event / inference window", fill=(160, 90, 20), font=font)
    image.save(path)


def main() -> int:
    if len(sys.argv) != 1:
        raise SystemExit("generate_sample_data.py reads every scenes/*.json file and accepts no arguments")
    scene_paths = sorted(SCENES_DIR.glob("*.json"))
    if not scene_paths:
        raise SystemExit(f"no scene files found in {SCENES_DIR}")
    try:
        scenes = [(path, load_scene(path)) for path in scene_paths]
    except ValueError as exc:
        raise SystemExit(f"error: {exc}") from exc
    names = [scene.name for _, scene in scenes]
    if len(names) != len(set(names)):
        raise SystemExit("error: every scene name must be unique")

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)
    run_start_time = datetime.now(timezone.utc).replace(microsecond=0)
    for _, scene in scenes:
        records = records_for(scene, run_start_time)
        data_path = DATA_DIR / f"{scene.name}.jsonl"
        plot_path = PLOTS_DIR / f"{scene.name}_speed_steering.png"
        write_data(records, data_path)
        write_plot(scene, run_start_time, plot_path)
        print(f"wrote {data_path} ({len(records)} records)")
        print(f"wrote {plot_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
