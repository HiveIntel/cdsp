from collections import deque
from datetime import datetime, timedelta
from typing import Any

import altair as alt
import pandas as pd
import streamlit as st

from ui_app.context import (
    ANGLE_MAX,
    ANGLE_MIN,
    ANGLE_STEP,
    SIGNAL_COLUMNS,
    SPEED_MAX,
    SPEED_MIN,
    SPEED_STEP,
    TIME_TICK_STEP_MS,
)


def extract_nested_value(data: dict[str, Any], path: list[str]) -> float | None:
    current: Any = data
    for key in path:
        if not isinstance(current, dict):
            return None
        current = current.get(key)

    try:
        return float(current) if current is not None else None
    except (TypeError, ValueError):
        return None


def extract_payload_timestamp(payload: dict[str, Any]) -> datetime:
    result = payload.get("result", {}) if isinstance(payload, dict) else {}
    metadata = result.get("metadata", {}) if isinstance(result, dict) else {}

    for entry in metadata.values():
        if not isinstance(entry, dict):
            continue
        timestamps = entry.get("timestamps", {})
        generated = timestamps.get("generated", {})
        seconds = generated.get("seconds")
        nanos = generated.get("nanos", 0)
        if seconds is not None:
            return datetime.fromtimestamp(float(seconds) + (float(nanos) / 1_000_000_000))

    for entry in metadata.values():
        if not isinstance(entry, dict):
            continue
        timestamps = entry.get("timestamps", {})
        received = timestamps.get("received", {})
        seconds = received.get("seconds")
        nanos = received.get("nanos", 0)
        if seconds is not None:
            return datetime.fromtimestamp(float(seconds) + (float(nanos) / 1_000_000_000))

    return datetime.utcnow()


def extract_signal_sample(payload: dict[str, Any]) -> dict[str, Any] | None:
    result = payload.get("result", {}) if isinstance(payload, dict) else {}
    data = result.get("data", {}) if isinstance(result, dict) else {}
    if not isinstance(data, dict):
        return None

    signal_sample = {
        "timestamp": extract_payload_timestamp(payload),
        "speed": extract_nested_value(data, ["Speed"]),
        "angle": extract_nested_value(data, ["Chassis", "SteeringWheel", "Angle"]),
        "latitude": extract_nested_value(data, ["CurrentLocation", "Latitude"]),
        "longitude": extract_nested_value(data, ["CurrentLocation", "Longitude"]),
        "reasoner": st.session_state["active_reasoner"],
    }

    if all(signal_sample[key] is None for key in ("speed", "angle", "latitude", "longitude")):
        return None

    return signal_sample


def build_signal_dataframe(history: deque) -> pd.DataFrame:
    rows = [
        {
            "timestamp": item["timestamp"],
            "speed": item["speed"],
            "angle": item["angle"],
            "latitude": item["latitude"],
            "longitude": item["longitude"],
        }
        for item in history
    ]

    if not rows:
        return pd.DataFrame(columns=SIGNAL_COLUMNS)

    return pd.DataFrame(rows, columns=SIGNAL_COLUMNS)


def get_signal_series_frame(signal_df: pd.DataFrame, value_column: str) -> pd.DataFrame:
    if signal_df.empty:
        return pd.DataFrame(columns=[value_column])

    return signal_df.dropna(subset=[value_column]).set_index("timestamp")[[value_column]]


def resolve_chart_time_bounds(chart_df: pd.DataFrame) -> tuple[datetime, datetime]:
    min_time = chart_df["timestamp"].min()
    max_time = chart_df["timestamp"].max()
    right = max_time if max_time > (min_time + timedelta(seconds=45)) else (min_time + timedelta(seconds=45))
    return min_time, right


def build_live_signal_chart(
    signal_df: pd.DataFrame,
    *,
    value_column: str,
    title: str,
    axis_min: int,
    axis_max: int,
    axis_step: int,
    tooltip_title: str,
) -> alt.Chart:
    chart_df = signal_df.reset_index()
    min_time, right = resolve_chart_time_bounds(chart_df)

    return (
        alt.Chart(chart_df)
        .mark_line(point=True)
        .encode(
            x=alt.X(
                "timestamp:T",
                title="Time",
                axis=alt.Axis(format="%M:%S", tickMinStep=TIME_TICK_STEP_MS),
                scale=alt.Scale(domain=[min_time, right]),
            ),
            y=alt.Y(
                f"{value_column}:Q",
                title=title,
                axis=alt.Axis(values=list(range(axis_min, axis_max + 1, axis_step))),
                scale=alt.Scale(domain=[axis_min, axis_max]),
            ),
            tooltip=[
                alt.Tooltip("timestamp:T", title="Time"),
                alt.Tooltip(f"{value_column}:Q", title=tooltip_title, format=".1f"),
            ],
        )
        .properties(height=250)
    )


def render_signal_header(title: str, latest_value: float | None, unit: str, latest_timestamp: datetime | None) -> None:
    metric_label = f"{latest_value:.1f} {unit}" if latest_value is not None else f"n/a {unit}"
    #timestamp_label = latest_timestamp.strftime("%H:%M:%S") if latest_timestamp else "waiting"
    title_col, value_col = st.columns([3, 1])
    with title_col:
        st.subheader(title)
    with value_col:
        st.metric("Latest", metric_label)
        #st.caption(f"Updated {timestamp_label}")


def render_live_signal_charts(signal_history: deque) -> None:
    signal_df = build_signal_dataframe(signal_history)
    if signal_df.empty:
        st.info("Waiting for vehicle subscribe data from the information layer.")
        return

    speed_df = get_signal_series_frame(signal_df, "speed")
    angle_df = get_signal_series_frame(signal_df, "angle")
    location_df = signal_df.dropna(subset=["latitude", "longitude"]).copy()

    with st.container(border=True):
        st.markdown(
            "<p style='font-size: 1.5rem; font-weight: 700; margin-bottom: 0.4rem;'>"
            "Vehicle signal charts from the Information Layer."
            "</p>",
            unsafe_allow_html=True,
        )
        left_col, right_col = st.columns([3, 2])

        with left_col:
            latest_speed = speed_df["speed"].iloc[-1] if not speed_df.empty else None
            latest_speed_timestamp = speed_df.index[-1].to_pydatetime() if not speed_df.empty else None
            render_signal_header("Speed", latest_speed, "km/h", latest_speed_timestamp)
            if speed_df.empty:
                st.info("Waiting for speed updates from the Information Layer.")
            else:
                st.altair_chart(
                    build_live_signal_chart(
                        speed_df,
                        value_column="speed",
                        title="Speed (km/h)",
                        axis_min=SPEED_MIN,
                        axis_max=SPEED_MAX,
                        axis_step=SPEED_STEP,
                        tooltip_title="Speed (km/h)",
                    ),
                    use_container_width=True,
                )

            latest_angle = angle_df["angle"].iloc[-1] if not angle_df.empty else None
            latest_angle_timestamp = angle_df.index[-1].to_pydatetime() if not angle_df.empty else None
            render_signal_header("Steering Angle", latest_angle, "deg", latest_angle_timestamp)
            if angle_df.empty:
                st.info("Waiting for steering-angle updates from the subscribe stream.")
            else:
                st.altair_chart(
                    build_live_signal_chart(
                        angle_df,
                        value_column="angle",
                        title="Steering Angle (deg)",
                        axis_min=ANGLE_MIN,
                        axis_max=ANGLE_MAX,
                        axis_step=ANGLE_STEP,
                        tooltip_title="Steering Angle (deg)",
                    ),
                    use_container_width=True,
                )

        with right_col:
            st.subheader("Vehicle Location")
            if location_df.empty:
                st.info("Waiting for location updates from the subscribe stream.")
            else:
                latest_location = location_df.iloc[-1]
                st.map(
                    location_df.rename(columns={"latitude": "lat", "longitude": "lon"})[["lat", "lon"]],
                    color="#2563eb",
                    size=1,
                    zoom=17,
                    use_container_width=True,
                )
                st.caption(
                    f"Latest: {latest_location['latitude']:.6f}, {latest_location['longitude']:.6f}"
                )
