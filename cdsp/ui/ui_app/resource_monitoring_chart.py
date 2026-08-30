from collections import deque
from datetime import datetime

import pandas as pd
import streamlit as st


def format_bytes(value: float | int | None) -> str:
    if value is None:
        return "n/a"

    size = float(value)
    units = ["B", "KB", "MB", "GB", "TB"]
    unit_index = 0
    while size >= 1024 and unit_index < len(units) - 1:
        size /= 1024
        unit_index += 1
    precision = 0 if size >= 10 else 1
    return f"{size:.{precision}f} {units[unit_index]}"


def build_chart_dataframe(history: deque) -> pd.DataFrame:
    rows = []
    for item in history:
        timestamp_raw = item.get("timestamp")
        try:
            timestamp = datetime.fromisoformat(timestamp_raw)
        except (TypeError, ValueError):
            timestamp = datetime.utcnow()

        rows.append(
            {
                "timestamp": timestamp,
                "cpu_percent": float(item.get("cpu", {}).get("percent", 0.0)),
                "memory_percent": float(item.get("memory", {}).get("percent", 0.0)),
                "reasoner": item.get("reasoner") or "",
            }
        )

    df = pd.DataFrame(rows)
    if df.empty:
        return df

    cpu_rdf4j: list[float | None] = []
    cpu_cqels: list[float | None] = []
    cpu_unassigned: list[float | None] = []
    memory_rdf4j: list[float | None] = []
    memory_cqels: list[float | None] = []
    memory_unassigned: list[float | None] = []

    previous_reasoner: str | None = None
    previous_cpu: float | None = None
    previous_memory: float | None = None

    for row in rows:
        current_reasoner = row["reasoner"]
        current_cpu = row["cpu_percent"]
        current_memory = row["memory_percent"]

        rdf4j_cpu_value = current_cpu if current_reasoner == "RDF4J" else None
        cqels_cpu_value = current_cpu if current_reasoner == "CQELS" else None
        unassigned_cpu_value = current_cpu if current_reasoner not in {"RDF4J", "CQELS"} else None
        rdf4j_memory_value = current_memory if current_reasoner == "RDF4J" else None
        cqels_memory_value = current_memory if current_reasoner == "CQELS" else None
        unassigned_memory_value = current_memory if current_reasoner not in {"RDF4J", "CQELS"} else None

        if previous_reasoner and current_reasoner != previous_reasoner:
            if current_reasoner == "RDF4J":
                if cpu_rdf4j:
                    cpu_rdf4j[-1] = previous_cpu
                if memory_rdf4j:
                    memory_rdf4j[-1] = previous_memory
            elif current_reasoner == "CQELS":
                if cpu_cqels:
                    cpu_cqels[-1] = previous_cpu
                if memory_cqels:
                    memory_cqels[-1] = previous_memory
            else:
                if cpu_unassigned:
                    cpu_unassigned[-1] = previous_cpu
                if memory_unassigned:
                    memory_unassigned[-1] = previous_memory

            if previous_reasoner == "RDF4J":
                rdf4j_cpu_value = current_cpu
                rdf4j_memory_value = current_memory
            elif previous_reasoner == "CQELS":
                cqels_cpu_value = current_cpu
                cqels_memory_value = current_memory
            else:
                unassigned_cpu_value = current_cpu
                unassigned_memory_value = current_memory

        cpu_rdf4j.append(rdf4j_cpu_value)
        cpu_cqels.append(cqels_cpu_value)
        cpu_unassigned.append(unassigned_cpu_value)
        memory_rdf4j.append(rdf4j_memory_value)
        memory_cqels.append(cqels_memory_value)
        memory_unassigned.append(unassigned_memory_value)

        previous_reasoner = current_reasoner
        previous_cpu = current_cpu
        previous_memory = current_memory

    df["cpu_rdf4j"] = cpu_rdf4j
    df["cpu_cqels"] = cpu_cqels
    df["cpu_unassigned"] = cpu_unassigned
    df["memory_rdf4j"] = memory_rdf4j
    df["memory_cqels"] = memory_cqels
    df["memory_unassigned"] = memory_unassigned
    return df


def render_metrics(payload: dict | None) -> None:
    with st.container(border=True):
        cpu_col, memory_col, detail_col = st.columns([1, 1, 1])

        cpu_percent = float(payload.get("cpu", {}).get("percent", 0.0)) if payload else 0.0
        memory = payload.get("memory", {}) if payload else {}
        memory_percent = float(memory.get("percent", 0.0)) if payload else 0.0

        with cpu_col:
            st.metric("CPU Usage", f"{cpu_percent:.1f}%")

        with memory_col:
            st.metric("Memory Usage", f"{memory_percent:.1f}%")

        with detail_col:
            used = format_bytes(memory.get("used_bytes"))
            total = format_bytes(memory.get("total_bytes"))
            st.metric("Memory Footprint", f"{used} / {total}")


def render_charts(history: deque) -> None:
    df = build_chart_dataframe(history)
    if df.empty:
        st.info("Waiting for monitoring data from the service device.")
        return

    cpu_chart_df = df.set_index("timestamp")[["cpu_unassigned", "cpu_rdf4j", "cpu_cqels"]].tail(300)
    memory_chart_df = df.set_index("timestamp")[["memory_unassigned", "memory_rdf4j", "memory_cqels"]].tail(300)

    with st.container(border=True):
        #st.caption("Yellow lines show samples with no reasoner. Red lines show RDF4J. Blue lines show CQELS.")
        chart_col_1, chart_col_2 = st.columns(2)
        with chart_col_1:
            st.subheader("CPU History")
            st.line_chart(
                cpu_chart_df,
                height=320,
                use_container_width=True,
                color=["#eab308", "#dc2626", "#2563eb"],
            )

        with chart_col_2:
            st.subheader("Memory History")
            st.line_chart(
                memory_chart_df,
                height=320,
                use_container_width=True,
                color=["#eab308", "#dc2626", "#2563eb"],
            )
