import streamlit as st

from ui_app.context import DEFAULT_CQELS_RULE_PATH, DEFAULT_RDF4J_RULE_PATH, LOCKED_REASONERS
from ui_app.logs import render_cqels_log_output, render_rdf4j_log_output
from ui_app.reasoner_actions import (
    activate_rdf4j_reasoner,
    activate_cqels_reasoner,
    load_cqels_rule_from_upload,
    load_rdf4j_rule_from_upload,
    submit_cqels_rule,
    submit_rdf4j_rule,
)


def render_reasoner_panel(reasoner: str, rule_key: str, result_key: str) -> None:
    is_active = st.session_state["active_reasoner"] == reasoner
    is_locked = reasoner in LOCKED_REASONERS
    information_layer_active = st.session_state.get("information_layer_active", False)
    editor_key = f"{rule_key}_editor"
    refresh_key = f"{rule_key}_editor_refresh"
    if st.session_state.get(refresh_key, False) or editor_key not in st.session_state:
        st.session_state[editor_key] = st.session_state[rule_key]
        st.session_state[refresh_key] = False

    with st.container(border=True):
        st.session_state[rule_key] = st.text_area(
            f"{reasoner} Rule Editor",
            height=300,
            disabled=not information_layer_active or not is_active or is_locked,
            key=editor_key,
        )

        if reasoner == "RDF4J":
            #st.caption(f"Default rule file: {DEFAULT_RDF4J_RULE_PATH}")
            uploaded_rule_file = st.file_uploader(
                "Upload a .dlog rule file",
                type=["dlog"],
                key="rdf4j_uploaded_rule",
                disabled=not information_layer_active or is_locked,
            )
            load_col, submit_col = st.columns(2)
            with load_col:
                if st.button(
                    "Load Rule File",
                    use_container_width=True,
                    disabled=not information_layer_active or is_locked,
                    key="rdf4j_load_rule_file",
                ):
                    load_rdf4j_rule_from_upload(uploaded_rule_file)
            with submit_col:
                if st.button(
                    "Submit RDF4J Rule",
                    use_container_width=True,
                    disabled=not information_layer_active or not is_active or is_locked,
                    key="rdf4j_submit_rule",
                ):
                    submit_rdf4j_rule(st.session_state[rule_key])

            if st.session_state["rdf4j_submit_feedback"]:
                st.info(st.session_state["rdf4j_submit_feedback"])

        elif reasoner == "CQELS":
            #st.caption(f"Default rule file: {DEFAULT_CQELS_RULE_PATH}")
            uploaded_rule_file = st.file_uploader(
                "Upload a .cqels rule file",
                type=["cqels", "txt", "rq"],
                key="cqels_uploaded_rule",
                disabled=not information_layer_active or is_locked,
            )
            load_col, submit_col = st.columns(2)
            with load_col:
                if st.button(
                    "Load Rule File",
                    use_container_width=True,
                    disabled=not information_layer_active or is_locked,
                    key="cqels_load_rule_file",
                ):
                    load_cqels_rule_from_upload(uploaded_rule_file)
            with submit_col:
                if st.button(
                    "Submit CQELS Rule",
                    use_container_width=True,
                    disabled=not information_layer_active or not is_active or is_locked,
                    key="cqels_submit_rule",
                ):
                    submit_cqels_rule(st.session_state[rule_key])

            if st.session_state["cqels_submit_feedback"]:
                st.info(st.session_state["cqels_submit_feedback"])

        #else:
        #    st.text_area(
        #        f"{reasoner} Results",
        #        value=st.session_state[result_key],
        #        height=180,
        #        disabled=not is_active or is_locked,
        #        key=f"{result_key}_viewer",
        #    )


def render_reasoner_log_panel(reasoner: str) -> None:
    with st.container(border=True):
        #st.subheader("Knowledge Layer Logs")
        if reasoner == "CQELS":
            st.subheader("CQELS Logs")
            log_output = render_cqels_log_output()
            if not log_output:
                log_output = "Waiting for CQELS activity..."
        else:
            st.subheader("RDF4 Logs")
            log_output = render_rdf4j_log_output()
            if not log_output:
                log_output = "Waiting for RDF4J activity..."
        st.code(log_output, language=None, height=420)


def render_knowledge_layer() -> None:
    information_layer_active = st.session_state.get("information_layer_active", False)
    st.markdown(
        """
        <style>
        div[data-testid="stButton"] > button[kind="primary"] {
            background-color: #22c55e;
            color: white;
            border: 1px solid #16a34a;
        }
        div[data-testid="stButton"] > button[kind="secondary"] {
            background-color: white;
            color: #1f2937;
            border: 1px solid #d1d5db;
        }
        </style>
        """,
        unsafe_allow_html=True,
    )

    with st.container(border=True):
        title_col, rdf4j_button_col, cqels_button_col = st.columns([3,  1, 1])
        with title_col:
            st.subheader("Knowledge Layer")
        
        with rdf4j_button_col:
            st.button(
                "RDF4J",
                use_container_width=True,
                type="primary" if st.session_state.get("active_reasoner") == "RDF4J" else "secondary",
                disabled=not information_layer_active or "RDF4J" in LOCKED_REASONERS,
                key="rdf4j_activate_button",
                on_click = activate_rdf4j_reasoner,
            )
        with cqels_button_col:
            st.button(
                "CQELS",
                use_container_width=True,
                type="primary" if st.session_state.get("active_reasoner") == "CQELS" else "secondary",
                disabled=not information_layer_active or "CQELS" in LOCKED_REASONERS,
                key="cqels_activate_button",
                on_click = activate_cqels_reasoner,
            )

        active_reasoner = st.session_state.get("active_reasoner", " ")
        control_col, log_col = st.columns([2, 3])
        with control_col:
            if active_reasoner == "CQELS":
                render_reasoner_panel("CQELS", "cqels_rule", "cqels_results")
            elif active_reasoner == "RDF4J":
                render_reasoner_panel("RDF4J", "rdf4j_rule", "rdf4j_results")
            elif not information_layer_active:
                st.info("Start the Information Layer to enable the Knowledge Layer.")
            else:
                st.info("No reasoner selected.")
        with log_col:
            render_reasoner_log_panel(active_reasoner)
