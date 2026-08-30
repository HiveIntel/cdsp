import json
import os
import queue
import threading
from collections import deque
from pathlib import Path
from typing import Any

import streamlit as st


UI_DIR = Path(__file__).resolve().parent.parent
LOG_DIR = Path(os.getenv("UI_LOG_DIR", UI_DIR / "logs"))
CONFIG_PATH = Path(os.getenv("UI_CONFIG_PATH", UI_DIR / "monitoring_config.json"))
SIGNAL_LOG_PATH = Path(os.getenv("UI_SIGNAL_LOG_PATH", LOG_DIR / "vehicle_signal.log"))
RDF4J_RESULT_LOG_PATH = Path(os.getenv("UI_RDF4J_RESULT_LOG_PATH", LOG_DIR / "rdf4j_result.log"))
CQELS_RESULT_LOG_PATH = Path(os.getenv("UI_CQELS_RESULT_LOG_PATH", LOG_DIR / "cqels_result.log"))
INFORMATION_LAYER_UI_LOG_PATH = Path(os.getenv("UI_INFORMATION_LAYER_LOG_PATH", LOG_DIR / "information_layer_ui.log"))
MONITOR_SERVICE_LOG_PATH = Path(os.getenv("UI_MONITOR_SERVICE_LOG_PATH", LOG_DIR / "monitor_service.log"))
MONITOR_PORT = int(os.getenv("MONITOR_WS_PORT", "1805"))
INFORMATION_LAYER_PORT = int(os.getenv("INFORMATION_LAYER_PORT", "8080"))
REASONER_PORT = int(os.getenv("REASONER_PORT", "12110"))
CQELS_PORT = int(os.getenv("CQELS_WS_PORT", "8505"))
DEFAULT_SERVICE_IP = os.getenv("SERVICE_DEVICE_IP", "localhost")
HISTORY_LIMIT = int(os.getenv("METRICS_HISTORY_LIMIT", "1000"))
REFRESH_MILLISECONDS = int(os.getenv("UI_REFRESH_MILLISECONDS", "1000"))
MONITOR_SUBSCRIBE_INTERVAL_SECONDS = float(os.getenv("MONITOR_SUBSCRIBE_INTERVAL_SECONDS", "5"))
DEFAULT_MONITOR_MODE = os.getenv("MONITOR_CONTROL_MODE", "docker")
DEFAULT_VEHICLE_INSTANCE = os.getenv("MONITOR_VEHICLE_INSTANCE", "VIN_1805")
DEFAULT_REASONER_DATASTORE = os.getenv("REASONER_DATASTORE", "ds-test")
DEFAULT_RDF4J_RULE_PATH = Path(
    os.getenv(
        "DEFAULT_RDF4J_RULE_PATH",
        "/home/anh/projects/cdsp-demo/cdsp-knowledge-layer/KL-config/rdf4j_rules/driving_style_inference_rules_shacl.dlog",
    )
)
DEFAULT_CQELS_RULE_PATH = Path(
    os.getenv(
        "DEFAULT_CQELS_RULE_PATH",
        "/home/anh/projects/cdsp-demo/cdsp-reasoner-cqels/src/main/resources/queries/aggressive-driving.cqels",
    )
)
DEFAULT_SUBSCRIBE_MESSAGE = os.getenv(
    "MONITOR_SUBSCRIBE_MESSAGE",
    json.dumps(
        {
            "jsonrpc": "2.0",
            "method": "subscribe",
            "id": "vehicle-subscribe",
            "params": {"instance": DEFAULT_VEHICLE_INSTANCE, "schema": "Vehicle"},
        }
    ),
)
RDF4J_REASONER_SUBSCRIBE_MESSAGE = os.getenv(
    "RDF4J_REASONER_SUBSCRIBE_MESSAGE",
    json.dumps(
        {
            "jsonrpc": "2.0",
            "method": "subscribe",
            "id": "vehicle-subscribe",
            "params": {"instance": DEFAULT_VEHICLE_INSTANCE, "schema": "Vehicle", "path":"AI.Reasoner.InferenceResults"},
        }
    ),
)
DEFAULT_RDF4J_RULE = os.getenv("DEFAULT_RDF4J_RULE", "")
DEFAULT_CQELS_RULE = os.getenv("DEFAULT_CQELS_RULE", "")

SPEED_MIN = 0
SPEED_MAX = 95
SPEED_STEP = 10
ANGLE_MIN = -350
ANGLE_MAX = 400
ANGLE_STEP = 90
TIME_TICK_STEP_MS = 1000

SIGNAL_COLUMNS = ["timestamp", "speed", "angle", "latitude", "longitude"]
SIGNAL_LOG_HEADER = "timestamp,speed_kmh,angle_deg,latitude,longitude\n"
RDF4J_RESULT_LOG_HEADER = "timestamp\ttitle\tcontent\n"
CQELS_RESULT_LOG_HEADER = "timestamp\ttitle\tcontent\n"
INFORMATION_LAYER_UI_LOG_HEADER = "timestamp\taction\tdetail\n"
MONITOR_SERVICE_LOG_HEADER = "timestamp\tdirection\taction\tdetail\n"
LOCKED_REASONERS: set[str] = set()


@st.cache_resource
def get_shared_state() -> tuple[queue.Queue, threading.Event]:
    return queue.Queue(), threading.Event()


message_queue, ws_open_flag = get_shared_state()


@st.cache_resource
def get_subscription_state() -> tuple[queue.Queue, threading.Event]:
    return queue.Queue(), threading.Event()


subscription_queue, il_open_flag = get_subscription_state()


@st.cache_resource
def get_rdf4j_reasoner_subscription_state() -> tuple[queue.Queue, threading.Event]:
    return queue.Queue(), threading.Event()


rdf4j_reasoner_subscription_queue, rdf4j_reasoner_il_open_flag = get_rdf4j_reasoner_subscription_state()


@st.cache_resource
def get_monitor_control_state() -> tuple[queue.Queue, threading.Event]:
    return queue.Queue(), threading.Event()


monitor_control_queue, monitor_subscription_ack_flag = get_monitor_control_state()


def build_monitor_url(node_ip: str) -> str:
    normalized_ip = node_ip.strip() or DEFAULT_SERVICE_IP
    return f"ws://{normalized_ip}:{MONITOR_PORT}"


def build_information_layer_url(node_ip: str) -> str:
    normalized_ip = node_ip.strip() or DEFAULT_SERVICE_IP
    return f"ws://{normalized_ip}:{INFORMATION_LAYER_PORT}"


def build_reasoner_base_url(node_ip: str) -> str:
    normalized_ip = node_ip.strip() or DEFAULT_SERVICE_IP
    return f"http://{normalized_ip}:{REASONER_PORT}"


def build_cqels_ws_url(node_ip: str) -> str:
    normalized_ip = node_ip.strip() or DEFAULT_SERVICE_IP
    return f"ws://{normalized_ip}:{CQELS_PORT}"


def load_default_rdf4j_rule() -> str:
    if DEFAULT_RDF4J_RULE:
        return DEFAULT_RDF4J_RULE

    try:
        return DEFAULT_RDF4J_RULE_PATH.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def load_default_cqels_rule() -> str:
    if DEFAULT_CQELS_RULE:
        return DEFAULT_CQELS_RULE

    try:
        return DEFAULT_CQELS_RULE_PATH.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def load_monitor_config() -> tuple[dict[str, Any], str]:
    default_config = {"node_ip": DEFAULT_SERVICE_IP}

    try:
        raw_config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        CONFIG_PATH.write_text(json.dumps(default_config, indent=2) + "\n", encoding="utf-8")
        return default_config, ""
    except json.JSONDecodeError as exc:
        return default_config, f"Invalid config file {CONFIG_PATH.name}: {exc}"

    if not isinstance(raw_config, dict):
        return default_config, f"Invalid config file {CONFIG_PATH.name}: root value must be an object."

    node_ip = str(raw_config.get("node_ip", DEFAULT_SERVICE_IP)).strip() or DEFAULT_SERVICE_IP
    return {"node_ip": node_ip}, ""


def ensure_bounded_history(key: str) -> None:
    history = st.session_state.get(key)
    if isinstance(history, deque) and history.maxlen == HISTORY_LIMIT:
        return

    old_items = list(history) if isinstance(history, deque) else []
    st.session_state[key] = deque(old_items[-HISTORY_LIMIT:], maxlen=HISTORY_LIMIT)


def init_session_state() -> None:
    from ui_app.logs import (
        ensure_information_layer_ui_log_file,
        ensure_cqels_result_log_file,
        ensure_monitor_service_log_file,
        ensure_rdf4j_result_log_file,
        ensure_signal_log_file,
    )

    ensure_signal_log_file()
    ensure_rdf4j_result_log_file()
    ensure_cqels_result_log_file()
    ensure_information_layer_ui_log_file()
    ensure_monitor_service_log_file()
    config, config_error = load_monitor_config()
    monitoring_service_ws = build_monitor_url(config["node_ip"])
    il_url = build_information_layer_url(config["node_ip"])
    reasoner_base_url = build_reasoner_base_url(config["node_ip"])
    cqels_ws_url = build_cqels_ws_url(config["node_ip"])

    defaults = {
        "node_ip": config["node_ip"],
        "monitoring_service_ws": monitoring_service_ws,
        "monitoring_service_wss": None,
        "il_url": il_url,
        "il_ws_app": None,
        "cqels_ws_url": cqels_ws_url,
        "cqels_ws_app": None,
        "rdf4j_reasoner_il_wss": None,
        "reasoner_base_url": reasoner_base_url,
        "ws_thread_started": False,
        "il_ws_thread_started": False,
        "cqels_ws_thread_started": False,
        "rdf4j_reasoner_il_ws_thread_started": False,
        "last_error": "",
        "config_error": config_error,
        "latest_payload": None,
        "last_metrics_sample_at": None,
        "connection_status": "Disconnected",
        "connection_status_error" : "",
        "monitor_mode": DEFAULT_MONITOR_MODE,
        "subscribe_message": DEFAULT_SUBSCRIBE_MESSAGE,
        "subscription_active": False,
        "information_layer_active": False,
        "active_reasoner": "",
        "monitor_subscription_ack": False,
        "rdf4j_reasoner_subscription_active": False,
        "rdf4j_rule": load_default_rdf4j_rule(),
        "cqels_rule": load_default_cqels_rule(),
        "rdf4j_results": "",
        "cqels_results": "",
        "rdf4j_submit_feedback": "",
        "cqels_submit_feedback": "",
        "cqels_subscription_id": "",
        "cqels_query_active": False,
        "pending_cqels_request": None,
        "subscribe_logs": [],
        "rdf4j_subscribe_logs": [],
        "cqels_logs": [],
        "info_layer_message": "",
        "signal_history": deque(maxlen=HISTORY_LIMIT),
        "history": deque(maxlen=HISTORY_LIMIT),
    }

    for key, value in defaults.items():
        if key not in st.session_state:
            st.session_state[key] = value

    for key in ("history", "signal_history"):
        ensure_bounded_history(key)
