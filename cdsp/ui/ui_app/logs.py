import json
from datetime import datetime
from typing import Any

import streamlit as st

from ui_app.context import (
    CQELS_RESULT_LOG_HEADER,
    CQELS_RESULT_LOG_PATH,
    INFORMATION_LAYER_UI_LOG_HEADER,
    INFORMATION_LAYER_UI_LOG_PATH,
    MONITOR_SERVICE_LOG_HEADER,
    MONITOR_SERVICE_LOG_PATH,
    RDF4J_RESULT_LOG_HEADER,
    RDF4J_RESULT_LOG_PATH,
    SIGNAL_LOG_HEADER,
    SIGNAL_LOG_PATH,
)

LOG_ENTRY_SEPARATOR = "\n\n---\n\n"


def normalize_embedded_json(value: Any) -> Any:
    if isinstance(value, str):
        trimmed = value.strip()
        if trimmed.startswith(("{", "[")) and trimmed.endswith(("}", "]")):
            try:
                return normalize_embedded_json(json.loads(trimmed))
            except json.JSONDecodeError:
                return value
        return value

    if isinstance(value, dict):
        return {key: normalize_embedded_json(item) for key, item in value.items()}

    if isinstance(value, list):
        return [normalize_embedded_json(item) for item in value]

    return value


def render_log_content(content: Any, *, normalize: bool = False, pretty: bool = False) -> str:
    rendered_content = normalize_embedded_json(content) if normalize else content
    if isinstance(rendered_content, (dict, list)):
        indent = 2 if pretty else None
        return json.dumps(rendered_content, separators=(",", ":") if not pretty else None, indent=indent)
    return str(rendered_content)


def ensure_signal_log_file(reset: bool = False) -> None:
    SIGNAL_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    if reset or not SIGNAL_LOG_PATH.exists():
        SIGNAL_LOG_PATH.write_text(SIGNAL_LOG_HEADER, encoding="utf-8")


def ensure_rdf4j_result_log_file(reset: bool = False) -> None:
    RDF4J_RESULT_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    if reset or not RDF4J_RESULT_LOG_PATH.exists():
        RDF4J_RESULT_LOG_PATH.write_text(RDF4J_RESULT_LOG_HEADER, encoding="utf-8")


def ensure_cqels_result_log_file(reset: bool = False) -> None:
    CQELS_RESULT_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    if reset or not CQELS_RESULT_LOG_PATH.exists():
        CQELS_RESULT_LOG_PATH.write_text(CQELS_RESULT_LOG_HEADER, encoding="utf-8")


def ensure_information_layer_ui_log_file(reset: bool = False) -> None:
    INFORMATION_LAYER_UI_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    if reset or not INFORMATION_LAYER_UI_LOG_PATH.exists():
        INFORMATION_LAYER_UI_LOG_PATH.write_text(INFORMATION_LAYER_UI_LOG_HEADER, encoding="utf-8")


def ensure_monitor_service_log_file(reset: bool = False) -> None:
    MONITOR_SERVICE_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    if reset or not MONITOR_SERVICE_LOG_PATH.exists():
        MONITOR_SERVICE_LOG_PATH.write_text(MONITOR_SERVICE_LOG_HEADER, encoding="utf-8")


def format_signal_log_value(value: Any, *, precision: int = 5) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return f"{value:.{precision}f}"
    return str(value)


def append_signal_log(signal_sample: dict[str, Any]) -> None:
    ensure_signal_log_file()
    timestamp = signal_sample["timestamp"].isoformat()
    log_line = ",".join(
        [
            timestamp,
            format_signal_log_value(signal_sample.get("speed"), precision=2),
            format_signal_log_value(signal_sample.get("angle"), precision=2),
            format_signal_log_value(signal_sample.get("latitude"), precision=6),
            format_signal_log_value(signal_sample.get("longitude"), precision=6),
        ]
    )
    with SIGNAL_LOG_PATH.open("a", encoding="utf-8") as log_file:
        log_file.write(log_line + "\n")


def append_rdf4j_result_log(title: str, content: Any) -> None:
    ensure_rdf4j_result_log_file()
    timestamp = datetime.now().isoformat()
    if isinstance(content, (dict, list)):
        rendered = json.dumps(content, separators=(",", ":"))
    else:
        rendered = str(content)
    sanitized_rendered = rendered.replace("\n", "\\n").replace("\t", " ")
    sanitized_title = str(title).replace("\n", " ").replace("\t", " ")
    with RDF4J_RESULT_LOG_PATH.open("a", encoding="utf-8") as log_file:
        log_file.write(f"{timestamp}\t{sanitized_title}\t{sanitized_rendered}\n")


def append_cqels_result_log(title: str, content: Any) -> None:
    ensure_cqels_result_log_file()
    timestamp = datetime.now().isoformat()
    if isinstance(content, (dict, list)):
        rendered = json.dumps(content, separators=(",", ":"))
    else:
        rendered = str(content)
    sanitized_rendered = rendered.replace("\n", "\\n").replace("\t", " ")
    sanitized_title = str(title).replace("\n", " ").replace("\t", " ")
    with CQELS_RESULT_LOG_PATH.open("a", encoding="utf-8") as log_file:
        log_file.write(f"{timestamp}\t{sanitized_title}\t{sanitized_rendered}\n")


def append_information_layer_ui_log(action: str, detail: Any) -> None:
    ensure_information_layer_ui_log_file()
    timestamp = datetime.now().isoformat()
    if isinstance(detail, (dict, list)):
        rendered = json.dumps(detail, separators=(",", ":"))
    else:
        rendered = str(detail)
    sanitized_action = str(action).replace("\n", " ").replace("\t", " ")
    sanitized_detail = rendered.replace("\n", "\\n").replace("\t", " ")
    with INFORMATION_LAYER_UI_LOG_PATH.open("a", encoding="utf-8") as log_file:
        log_file.write(f"{timestamp}\t{sanitized_action}\t{sanitized_detail}\n")


def append_monitor_service_log(direction: str, action: str, detail: Any) -> None:
    ensure_monitor_service_log_file()
    timestamp = datetime.now().isoformat()
    if isinstance(detail, (dict, list)):
        rendered = json.dumps(detail, separators=(",", ":"))
    else:
        rendered = str(detail)
    sanitized_direction = str(direction).replace("\n", " ").replace("\t", " ")
    sanitized_action = str(action).replace("\n", " ").replace("\t", " ")
    sanitized_detail = rendered.replace("\n", "\\n").replace("\t", " ")
    with MONITOR_SERVICE_LOG_PATH.open("a", encoding="utf-8") as log_file:
        log_file.write(f"{timestamp}\t{sanitized_direction}\t{sanitized_action}\t{sanitized_detail}\n")


def append_subscribe_log(title: str, content: Any) -> None:
    timestamp = datetime.now().strftime("%H:%M:%S")
    rendered = render_log_content(content, normalize=True, pretty=True)
    st.session_state["subscribe_logs"].append(f"[{timestamp}] {title}\n{rendered}")
    st.session_state["subscribe_logs"] = st.session_state["subscribe_logs"][-5:]
    append_information_layer_ui_log(title, content)


def render_subscribe_log_output() -> str:
    log_output = LOG_ENTRY_SEPARATOR.join(st.session_state["subscribe_logs"][-5:])
    if not log_output:
        return "Waiting for information-layer output..."
    return log_output


def append_rdf4j_subscribe_log(title: str, content: Any) -> None:
    timestamp = datetime.now().strftime("%H:%M:%S")
    rendered = render_log_content(content, normalize=True, pretty=True)
    st.session_state["rdf4j_subscribe_logs"].append(f"[{timestamp}] {title}\n{rendered}")
    st.session_state["rdf4j_subscribe_logs"] = st.session_state["rdf4j_subscribe_logs"][-5:]
    st.session_state["rdf4j_results"] = render_rdf4j_log_output()
    append_rdf4j_result_log(title, content)


def render_rdf4j_log_output() -> str:
    log_output = LOG_ENTRY_SEPARATOR.join(st.session_state["rdf4j_subscribe_logs"][-5:])
    if not log_output:
        return "Waiting for information-layer output..."
    return log_output


def append_cqels_log(title: str, content: Any) -> None:
    timestamp = datetime.now().strftime("%H:%M:%S")
    rendered = render_log_content(content, normalize=True, pretty=True)
    st.session_state["cqels_logs"].append(f"[{timestamp}] {title}\n{rendered}")
    st.session_state["cqels_logs"] = st.session_state["cqels_logs"][-5:]
    st.session_state["cqels_results"] = render_cqels_log_output()
    append_cqels_result_log(title, content)


def render_cqels_log_output() -> str:
    log_output = LOG_ENTRY_SEPARATOR.join(st.session_state["cqels_logs"][-5:])
    if not log_output:
        return "Waiting for CQELS activity..."
    return log_output

