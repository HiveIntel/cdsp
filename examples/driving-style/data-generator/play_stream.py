#!/usr/bin/env python3
"""Replay one generated scene into the Information Layer over WebSocket."""

from __future__ import annotations

import asyncio
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

import websockets


SCRIPT_DIR = Path(__file__).resolve().parent
DATA_DIR = SCRIPT_DIR / "data"
INFORMATION_LAYER_URL = os.getenv("INFORMATION_LAYER_URL", "ws://127.0.0.1:8080")
VEHICLE_OBJECT_ID = os.getenv("VEHICLE_OBJECT_ID", "VIN_1805")


def timestamp_metadata(timestamp_us: int, path: str = "") -> dict[str, Any]:
    return {
        path: {
            "timestamps": {
                "generated": {
                    "seconds": timestamp_us // 1_000_000,
                    "nanos": (timestamp_us % 1_000_000) * 1_000,
                }
            }
        }
    }


def information_layer_request(signals: list[dict[str, Any]], request_id: str) -> dict[str, Any]:
    if not signals:
        raise ValueError("record must contain at least one signal")
    request: dict[str, Any] = {
        "jsonrpc": "2.0",
        "method": "set",
        "id": request_id,
        "params": {"instance": VEHICLE_OBJECT_ID},
    }
    if len(signals) == 1:
        signal = signals[0]
        schema, _, path = signal["name"].partition(".")
        request["params"].update(
            schema=schema,
            path=path,
            data=signal["value"],
            metadata=timestamp_metadata(signal["timestamp_us"]),
        )
        return request

    schema = signals[0]["name"].split(".", 1)[0]
    data: dict[str, Any] = {}
    metadata: dict[str, Any] = {}
    for signal in signals:
        signal_schema, _, relative_path = signal["name"].partition(".")
        if signal_schema != schema:
            raise ValueError("all signals in one record must use the same schema")
        parts = relative_path.split(".")
        level = data
        for part in parts[:-1]:
            level = level.setdefault(part, {})
        level[parts[-1]] = signal["value"]
        metadata.update(timestamp_metadata(signal["timestamp_us"], relative_path))
    request["params"].update(schema=schema, data=data, metadata=metadata)
    return request


def load_records(scene_name: str) -> list[dict[str, Any]]:
    if Path(scene_name).name != scene_name or scene_name.endswith(".jsonl"):
        raise ValueError("scene name must be a filename stem, for example: scene01")
    path = DATA_DIR / f"{scene_name}.jsonl"
    if not path.is_file():
        raise ValueError(f"generated data file not found: {path}; run generate_sample_data.py first")
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
            signals = record["signals"]
            if not isinstance(signals, list) or not signals:
                raise ValueError("signals must be a non-empty list")
            for signal in signals:
                if not isinstance(signal, dict) or not {"timestamp_us", "name", "value"} <= signal.keys():
                    raise ValueError("signal requires timestamp_us, name, and value")
        except (json.JSONDecodeError, KeyError, ValueError) as exc:
            raise ValueError(f"invalid record on line {line_number} in {path}: {exc}") from exc
        records.append(record)
    if not records:
        raise ValueError(f"no records found in {path}")
    return records


def record_timestamp_us(record: dict[str, Any]) -> int:
    return min(int(signal["timestamp_us"]) for signal in record["signals"])


async def replay(records: list[dict[str, Any]], scene_name: str) -> None:
    first_timestamp_us = record_timestamp_us(records[0])
    replay_started_at = time.monotonic()
    async with websockets.connect(INFORMATION_LAYER_URL) as websocket:
        for index, record in enumerate(records, start=1):
            scheduled_seconds = (record_timestamp_us(record) - first_timestamp_us) / 1_000_000
            delay_seconds = replay_started_at + scheduled_seconds - time.monotonic()
            if delay_seconds > 0:
                await asyncio.sleep(delay_seconds)
            await websocket.send(json.dumps(information_layer_request(record["signals"], str(index))))
            response = json.loads(await websocket.recv())
            if "error" in response:
                raise RuntimeError(f"Information Layer rejected record {index}: {response}")
            print(f"{scene_name}: replayed {index}/{len(records)}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: play_stream.py SCENE_NAME")
    scene_name = sys.argv[1]
    try:
        records = load_records(scene_name)
    except ValueError as exc:
        raise SystemExit(f"error: {exc}") from exc
    print(f"replaying {scene_name} to {INFORMATION_LAYER_URL} for {VEHICLE_OBJECT_ID}")
    asyncio.run(replay(records, scene_name))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
