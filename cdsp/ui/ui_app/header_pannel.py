import json
from typing import Any

import streamlit as st

from ui_app.context import (
    DEFAULT_MONITOR_MODE,
    MONITOR_PORT,
)

def render_header(payload: dict[str, Any] | None) -> None:
    st.title("CDSP Device Monitoring")

    with st.container(border=True):
        status_col, service_col, target_col, port_col, mode_col = st.columns([1, 2, 2, 1, 1])
        with status_col:
            st.metric("Connection", st.session_state["connection_status"])
        with service_col:
            st.metric("Node IP", st.session_state["node_ip"])
        with target_col:
            target = payload.get("target", {}) if payload else {}
            target_text = f"{target.get('kind', 'n/a')}:{target.get('name', 'n/a')}"
            st.metric("Target Device", target_text)
        with port_col:
            st.metric("Monitor Port", str(MONITOR_PORT))
        with mode_col:
            st.session_state["monitor_mode"] = st.selectbox(
               "Mode",
                     options=["native", "docker"],
                     index=0 if st.session_state.get("monitor_mode", DEFAULT_MONITOR_MODE) == "native" else 1,
                     key="monitor_mode_selector",
                 )
    
    #if not st.session_state["connection_status"] == "Live" :
    #    st.warning(st.session_state["connection_status_error"])
    
    #if st.session_state["config_error"]:
    #    st.warning(st.session_state["config_error"])
