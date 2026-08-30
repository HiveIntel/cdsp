import json
import urllib.error
import urllib.request
import uuid
from datetime import datetime
from typing import Any

import streamlit as st

from ui_app.context import DEFAULT_MONITOR_MODE, DEFAULT_REASONER_DATASTORE
from ui_app.logs import (
    append_cqels_log,
    append_monitor_service_log,
    append_rdf4j_subscribe_log,
    append_subscribe_log,
)
from ui_app.websocket_layer import send_cqels_request


def send_monitor_reasoner_command(reasoner: str, *, active: bool = True) -> bool:
    monitoring_service_wss = st.session_state.get("monitoring_service_wss")
    mode = st.session_state.get("monitor_mode", DEFAULT_MONITOR_MODE)
    if (
        monitoring_service_wss is None
        or monitoring_service_wss.sock is None
        or not monitoring_service_wss.sock.connected
    ):
        message = (
            "Monitoring control websocket is not connected at "
            f"{st.session_state['monitoring_service_ws']}."
        )
        st.session_state["last_error"] = message
        return False

    request_id = f"{reasoner.lower()}-{'start' if active else 'stop'}-{int(datetime.utcnow().timestamp() * 1000)}"
    
    message = {
        "type": "command",
        "command": "set_reasoner_state",
        "requestId": request_id,
        "payload": {
            "reasoner": reasoner,
            "active": active,
            "mode": mode,
        },
    }
    try:
        monitoring_service_wss.send(json.dumps(message))
        append_monitor_service_log("outbound", "set_reasoner_state", message)
        return True
    except Exception as exc:
        st.session_state["last_error"] = f"Failed to send {reasoner} command: {exc}"
        append_monitor_service_log("outbound_error", "set_reasoner_state", st.session_state["last_error"])
        return False


def is_reasoner_running(reasoner: str) -> bool:
    return st.session_state.get("active_reasoner", "") == reasoner


def activate_rdf4j_reasoner() -> None:
    if not st.session_state.get("information_layer_active", False):
        message = "Start the Information Layer before enabling RDF4J."
        st.session_state["rdf4j_submit_feedback"] = message
        append_rdf4j_subscribe_log("RDF4J button blocked", message)
        return

    cqels_was_running = is_reasoner_running("CQELS")
    st.session_state["active_reasoner"] = "RDF4J"
    append_rdf4j_subscribe_log("RDF4J button clicked", "Knowledge layer switched to RDF4J.")
    cqels_stopped = True
    if cqels_was_running:
        cqels_stopped = send_monitor_reasoner_command("CQELS", active=False)

    rdf4j_started = send_monitor_reasoner_command("RDF4J", active=True)

    if cqels_was_running and cqels_stopped and rdf4j_started:
        st.session_state["rdf4j_submit_feedback"] = "Stopped CQELS and started RDF4J."
        append_rdf4j_subscribe_log(
            "RDF4J activation requested",
            "CQELS was active, so the UI sent stop for CQELS, then start for RDF4J.",
        )
    elif not cqels_was_running and rdf4j_started:
        st.session_state["rdf4j_submit_feedback"] = "Started RDF4J."
        append_rdf4j_subscribe_log(
            "RDF4J activation requested",
            "CQELS was not active, so the UI sent only start for RDF4J.",
        )
    elif cqels_was_running and not cqels_stopped and rdf4j_started:
        st.session_state["rdf4j_submit_feedback"] = "Started RDF4J, but stopping CQELS failed."
        append_rdf4j_subscribe_log(
            "RDF4J activation partially failed",
            "RDF4J start was sent, but CQELS stop failed.",
        )
    elif cqels_was_running and cqels_stopped and not rdf4j_started:
        st.session_state["rdf4j_submit_feedback"] = "Stopped CQELS, but starting RDF4J failed."
        append_rdf4j_subscribe_log(
            "RDF4J activation partially failed",
            "CQELS stop was sent, but RDF4J start failed.",
        )
    elif not cqels_was_running and not rdf4j_started:
        st.session_state["rdf4j_submit_feedback"] = "Failed to start RDF4J."
        append_rdf4j_subscribe_log(
            "RDF4J activation failed",
            "CQELS was not active, but starting RDF4J failed.",
        )
    else:
        st.session_state["rdf4j_submit_feedback"] = "Failed to stop CQELS and start RDF4J."
        append_rdf4j_subscribe_log(
            "RDF4J activation failed",
            "Failed to send both CQELS stop and RDF4J start to the monitoring service.",
        )
  
def activate_cqels_reasoner() -> None:
    if not st.session_state.get("information_layer_active", False):
        message = "Start the Information Layer before enabling CQELS."
        st.session_state["cqels_submit_feedback"] = message
        append_cqels_log("CQELS button blocked", message)
        return

    rdf4j_was_running = is_reasoner_running("RDF4J")
    st.session_state["active_reasoner"] = "CQELS"
    append_cqels_log("CQELS button clicked", "Knowledge layer switched to CQELS.")
    rdf4j_stopped = True
    if rdf4j_was_running:
        rdf4j_stopped = send_monitor_reasoner_command("RDF4J", active=False)

    cqels_started = send_monitor_reasoner_command("CQELS", active=True)

    if rdf4j_was_running and rdf4j_stopped and cqels_started:
        st.session_state["cqels_submit_feedback"] = "Stopped RDF4J and started CQELS."
        append_cqels_log(
            "CQELS activation requested",
            "RDF4J was active, so the UI sent stop for RDF4J, then start for CQELS.",
        )
    elif not rdf4j_was_running and cqels_started:
        st.session_state["cqels_submit_feedback"] = "Started CQELS."
        append_cqels_log(
            "CQELS activation requested",
            "RDF4J was not active, so the UI sent only start for CQELS.",
        )
    elif rdf4j_was_running and not rdf4j_stopped and cqels_started:
        st.session_state["cqels_submit_feedback"] = "Started CQELS, but stopping RDF4J failed."
        append_cqels_log(
            "CQELS activation partially failed",
            "CQELS start was sent, but RDF4J stop failed.",
        )
    elif rdf4j_was_running and rdf4j_stopped and not cqels_started:
        st.session_state["cqels_submit_feedback"] = "Stopped RDF4J, but starting CQELS failed."
        append_cqels_log(
            "CQELS activation partially failed",
            "RDF4J stop was sent, but CQELS start failed.",
        )
    elif not rdf4j_was_running and not cqels_started:
        st.session_state["cqels_submit_feedback"] = "Failed to start CQELS."
        append_cqels_log(
            "CQELS activation failed",
            "RDF4J was not active, but starting CQELS failed.",
        )
    else:
        st.session_state["cqels_submit_feedback"] = "Failed to stop RDF4J and start CQELS."
        append_cqels_log(
            "CQELS activation failed",
            "Failed to send both RDF4J stop and CQELS start to the monitoring service.",
        )

def start_active_reasoner() -> None:
    if not st.session_state.get("information_layer_active", False):
        message = "Start the Information Layer before starting the Knowledge Layer."
        st.session_state["last_error"] = message
        append_cqels_log("Knowledge layer start blocked", message)
        return

    reasoner = st.session_state.get("active_reasoner", "")
    if not reasoner:
        message = "Choose RDF4J or CQELS before starting the knowledge layer."
        st.session_state["last_error"] = message
        append_cqels_log("Knowledge layer start blocked", message)
        return

    started = send_monitor_reasoner_command(reasoner, active=True)
    message = f"Knowledge layer start requested for {reasoner}."
    if reasoner == "RDF4J":
        append_rdf4j_subscribe_log("Knowledge layer start button clicked", message)
        if started:
            st.session_state["rdf4j_submit_feedback"] = message
    else:
        append_cqels_log("Knowledge layer start button clicked", message)
        if started:
            st.session_state["cqels_submit_feedback"] = message

def load_rdf4j_rule_from_upload(uploaded_file: Any) -> None:
    if uploaded_file is None:
        st.session_state["rdf4j_submit_feedback"] = "Choose a .dlog file before loading."
        return

    rule_text = uploaded_file.read().decode("utf-8")
    st.session_state["rdf4j_rule"] = rule_text
    st.session_state["rdf4j_rule_editor_refresh"] = True
    st.session_state["rdf4j_submit_feedback"] = ""
    st.rerun()

def load_cqels_rule_from_upload(uploaded_file: Any) -> None:
    if uploaded_file is None:
        st.session_state["cqels_submit_feedback"] = "Choose a CQELS rule file before loading."
        return

    rule_text = uploaded_file.read().decode("utf-8")
    st.session_state["cqels_rule"] = rule_text
    st.session_state["cqels_rule_editor_refresh"] = True
    st.session_state["cqels_submit_feedback"] = ""
    st.rerun()

def submit_cqels_rule(rule_text: str) -> None:
    if not st.session_state.get("information_layer_active", False):
        message = "Start the Information Layer before submitting a CQELS query."
        st.session_state["cqels_submit_feedback"] = message
        append_cqels_log("CQELS submit blocked", message)
        return

    if st.session_state.get("active_reasoner") != "CQELS":
        message = "Activate CQELS before submitting a query."
        st.session_state["cqels_submit_feedback"] = message
        append_cqels_log("CQELS submit blocked", message)
        return

    if not rule_text.strip():
        message = "Enter a CQELS query before submitting."
        st.session_state["cqels_submit_feedback"] = message
        append_cqels_log("CQELS submit blocked", message)
        return

    subscription_id = st.session_state.get("cqels_subscription_id", "").strip()
    if not subscription_id:
        subscription_id = f"cqels-ui-{uuid.uuid4().hex[:12]}"
        st.session_state["cqels_subscription_id"] = subscription_id

    request = {
        "type": "command",
        "requestId": f"cqels-register-{int(datetime.utcnow().timestamp() * 1000)}",
        "command": "register_query",
        "payload": {
            "subscriptionId": subscription_id,
            "query": rule_text,
            "streamName": "VehicleSignals",
        },
    }

    try:
        if send_cqels_request(request, log_title="CQELS query submitted"):
            st.session_state["cqels_submit_feedback"] = "CQELS query submitted. Waiting for results..."
        else:
            st.session_state["cqels_submit_feedback"] = (
                f"Connecting to CQELS service at {st.session_state['cqels_ws_url']} before submitting the query..."
            )
    except Exception as exc:
        message = f"Failed to submit CQELS query: {exc}"
        st.session_state["cqels_submit_feedback"] = message
        append_cqels_log("CQELS submit failed", message)


def submit_rdf4j_rule(rule_text: str) -> None:
    endpoint = (
        f"{st.session_state['reasoner_base_url']}/datastores/{DEFAULT_REASONER_DATASTORE}/rules/add"
    )
    payload = rule_text.encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=payload,
        method="POST",
        headers={"Content-Type": "text/plain; charset=utf-8"},
    )

    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            body = response.read().decode("utf-8", errors="replace").strip()
            status = getattr(response, "status", 200)
        st.session_state["rdf4j_submit_feedback"] = (
            f"RDF4J rule added successfully ({status}) at {endpoint}."
        )
        append_subscribe_log("RDF4J rule added", body or "Rules appended successfully.")
    except urllib.error.HTTPError as exc:
        error_body = exc.read().decode("utf-8", errors="replace").strip()
        st.session_state["rdf4j_submit_feedback"] = (
            f"RDF4J rule add failed ({exc.code}) at {endpoint}: {error_body or exc.reason}"
        )
    except urllib.error.URLError as exc:
        st.session_state["rdf4j_submit_feedback"] = (
            f"RDF4J reasoner is unreachable at {endpoint}: {exc.reason}"
        )
