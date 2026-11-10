"""
@file  test_phase4_diagnostics_and_sla.py
@brief UI tests verifying diagnostic modal rendering and latency SLAs


Phase 4: Diagnostic Artifact Bundle Generation and Real-Time SLA Integration Tests.

Verifies:
  1. Failure artifact bundle generation produces:
     - Valid PNG screenshot starting with standard '\\x89PNG\\r\\n\\x1a\\n' header.
     - 'dom-snapshot.html' containing live '#sls-reset-modal' markup.
     - 'console-and-network.json' containing console logs, network requests, and window.__naviQueue state.
  2. Real-time input SLA:
     - In-browser performance.now() delta for #sls-reset-input input handler enabling #sls-modal-confirm <= 50ms.
     - Local loopback round-trip latency <= 200ms.
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.cloud_settings_page import CloudSettingsPage


@pytest.mark.ui_p0
def test_phase4_failure_artifact_bundle_generation(
    driver: BrowserDriver,
    callback_server: CallbackServerStub,
    repo_paths: dict[str, Path],
    tmp_path: Path,
) -> None:
    """
    Verify capture_failure_artifacts generates a PNG screenshot, DOM snapshot
    containing #sls-reset-modal, and console-and-network.json with logs, requests, and naviQueue.
    """
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    page = CloudSettingsPage(driver)
    page.inject_cloud_manager(repo_paths["scripts"] / "cloud_manager.js")

    # Open the reset modal so it is live in the DOM
    modal = page.reset_modal
    modal.open(1443430)
    assert modal.is_open()

    # Populate window.__naviQueue state, emit a console log, and execute a network request
    driver.evaluate("window.__naviQueue = ['check 1443430', 'get-depots 1443430'];", await_promise=False)
    driver.evaluate("console.log('phase4-diagnostic-artifact-marker');", await_promise=False)
    driver.evaluate("fetch('http://127.0.0.1:9001/cloud/status');", await_promise=False)

    # Wait for the network event to be registered by the driver
    driver.wait_for_condition(
        lambda: any("/cloud/status" in evt.get("url", "") for evt in driver.network_events),
        timeout=5.0,
        error_message="Network event for /cloud/status was not recorded in driver",
    )

    # Capture failure artifacts into the isolated tmp_path
    artifacts = driver.capture_failure_artifacts("phase4_diag_test", output_dir=tmp_path)

    # 1. Validate PNG screenshot header
    screenshot_file = Path(artifacts["screenshot"])
    assert screenshot_file.is_file(), f"Screenshot file missing: {screenshot_file}"
    png_bytes = screenshot_file.read_bytes()
    assert png_bytes.startswith(b"\x89PNG\r\n\x1a\n"), f"Invalid PNG magic bytes: {png_bytes[:16]!r}"
    assert len(png_bytes) > 500, f"Screenshot suspiciously small: {len(png_bytes)} bytes"

    # 2. Validate DOM snapshot containing live #sls-reset-modal markup
    dom_file = Path(artifacts["dom"])
    assert dom_file.is_file(), f"DOM snapshot file missing: {dom_file}"
    dom_text = dom_file.read_text(encoding="utf-8")
    assert "sls-reset-modal" in dom_text, "DOM snapshot missing #sls-reset-modal markup"
    assert "sls-reset-input" in dom_text, "DOM snapshot missing #sls-reset-input markup"
    assert "sls-modal-confirm" in dom_text, "DOM snapshot missing #sls-modal-confirm markup"

    # 3. Validate console-and-network.json contains logs, requests, and window.__naviQueue
    json_file = Path(artifacts["logs"])
    assert json_file.is_file(), f"Logs JSON file missing: {json_file}"
    logs_data = json.loads(json_file.read_text(encoding="utf-8"))

    # Console verification
    assert "console" in logs_data and isinstance(logs_data["console"], list)
    assert any("phase4-diagnostic-artifact-marker" in str(item) for item in logs_data["console"]), (
        f"Console marker not found in logs: {logs_data['console']}"
    )

    # Network verification
    assert "network" in logs_data and isinstance(logs_data["network"], list)
    assert any("/cloud/status" in evt.get("url", "") for evt in logs_data["network"]), (
        f"Expected /cloud/status network request not found in {logs_data['network']}"
    )

    # window.__naviQueue state verification
    queue_state = logs_data.get("naviQueue") or logs_data.get("window.__naviQueue")
    assert queue_state == ["check 1443430", "get-depots 1443430"], (
        f"window.__naviQueue state not correctly recorded: {queue_state}"
    )


@pytest.mark.ui_p0
def test_phase4_realtime_input_handler_sla(
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """
    Measure in-browser performance.now() delta for #sls-reset-input input event handler
    enabling #sls-modal-confirm (must be <= 50ms).
    """
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    page = CloudSettingsPage(driver)
    page.inject_cloud_manager(repo_paths["scripts"] / "cloud_manager.js")

    modal = page.reset_modal
    modal.open(1443430)
    assert modal.is_open()

    benchmark_script = """
    (function() {
        var input = document.getElementById('sls-reset-input');
        var confirmBtn = document.getElementById('sls-modal-confirm');
        if (!input || !confirmBtn) {
            return { success: false, error: 'Elements #sls-reset-input or #sls-modal-confirm missing' };
        }

        // Baseline verification: empty input keeps button disabled
        input.value = '';
        input.dispatchEvent(new Event('input', { bubbles: true }));
        if (!confirmBtn.disabled) {
            return { success: false, error: 'Confirm button was not initially disabled' };
        }

        var measurements = [];
        var iterations = 10;
        for (var i = 0; i < iterations; i++) {
            input.value = '';
            input.dispatchEvent(new Event('input', { bubbles: true }));

            var t0 = performance.now();
            input.value = 'RESET';
            input.dispatchEvent(new Event('input', { bubbles: true }));
            var t1 = performance.now();

            if (confirmBtn.disabled) {
                return { success: false, error: 'Button failed to enable on iteration ' + i };
            }
            measurements.push(t1 - t0);
        }

        var maxDelta = Math.max.apply(null, measurements);
        var avgDelta = measurements.reduce(function(a, b) { return a + b; }, 0) / measurements.length;

        // Character-by-character incremental typing SLA check
        var incrementalDeltas = [];
        var partials = ['R', 'RE', 'RES', 'RESE', 'RESET'];
        input.value = '';
        input.dispatchEvent(new Event('input', { bubbles: true }));

        for (var j = 0; j < partials.length; j++) {
            var stepStart = performance.now();
            input.value = partials[j];
            input.dispatchEvent(new Event('input', { bubbles: true }));
            var stepEnd = performance.now();
            incrementalDeltas.push(stepEnd - stepStart);
        }
        var maxIncrementalDelta = Math.max.apply(null, incrementalDeltas);

        return {
            success: true,
            measurements: measurements,
            maxDeltaMs: maxDelta,
            avgDeltaMs: avgDelta,
            maxIncrementalDeltaMs: maxIncrementalDelta,
            buttonEnabled: !confirmBtn.disabled
        };
    })()
    """
    res = driver.evaluate(benchmark_script, await_promise=False)
    assert res["success"] is True, f"Benchmark execution failed: {res.get('error')}"
    assert res["buttonEnabled"] is True, "Button should be enabled after typing RESET"

    # Enforce <= 50ms SLA
    assert res["maxDeltaMs"] <= 50.0, (
        f"Input event handler max delta {res['maxDeltaMs']:.3f}ms exceeded 50ms SLA"
    )
    assert res["maxIncrementalDeltaMs"] <= 50.0, (
        f"Incremental typing max delta {res['maxIncrementalDeltaMs']:.3f}ms exceeded 50ms SLA"
    )


@pytest.mark.ui_p0
def test_phase4_local_loopback_roundtrip_latency_sla(
    driver: BrowserDriver,
    callback_server: CallbackServerStub,
    repo_paths: dict[str, Path],
) -> None:
    """
    Measure in-browser performance.now() round-trip latency for local loopback requests
    (GET and POST against callback_server) ensuring latency <= 200ms.
    """
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    # 1. Measure GET /cloud/status loopback round-trip latency
    get_benchmark_script = """
    (async function() {
        // Warm-up to eliminate cold startup or ARP/DNS resolution latency
        try {
            await fetch('http://127.0.0.1:9001/cloud/status');
        } catch (e) {}

        var measurements = [];
        var iterations = 5;
        for (var i = 0; i < iterations; i++) {
            var t0 = performance.now();
            var resp = await fetch('http://127.0.0.1:9001/cloud/status');
            var data = await resp.json();
            var t1 = performance.now();

            if (resp.status !== 200 || !data || data.enabled !== true) {
                return { success: false, error: 'Unexpected response on iteration ' + i };
            }
            measurements.push(t1 - t0);
        }

        var maxDelta = Math.max.apply(null, measurements);
        var avgDelta = measurements.reduce(function(a, b) { return a + b; }, 0) / measurements.length;

        return {
            success: true,
            measurements: measurements,
            maxDeltaMs: maxDelta,
            avgDeltaMs: avgDelta
        };
    })()
    """
    res_get = driver.evaluate(get_benchmark_script, await_promise=True)
    assert res_get["success"] is True, f"GET loopback benchmark failed: {res_get.get('error')}"
    assert res_get["maxDeltaMs"] <= 200.0, (
        f"GET loopback roundtrip max latency {res_get['maxDeltaMs']:.2f}ms exceeded 200ms SLA"
    )

    # 2. Measure POST /cloud/apps/1443430/reset loopback round-trip latency
    post_benchmark_script = """
    (async function() {
        var measurements = [];
        var iterations = 3;
        for (var i = 0; i < iterations; i++) {
            var t0 = performance.now();
            var resp = await fetch('http://127.0.0.1:9001/cloud/apps/1443430/reset', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ appid: 1443430, iteration: i })
            });
            var data = await resp.json();
            var t1 = performance.now();

            if (resp.status !== 200 || !data || data.success !== true) {
                return { success: false, error: 'Unexpected POST response on iteration ' + i };
            }
            measurements.push(t1 - t0);
        }

        var maxDelta = Math.max.apply(null, measurements);
        var avgDelta = measurements.reduce(function(a, b) { return a + b; }, 0) / measurements.length;

        return {
            success: true,
            measurements: measurements,
            maxDeltaMs: maxDelta,
            avgDeltaMs: avgDelta
        };
    })()
    """
    res_post = driver.evaluate(post_benchmark_script, await_promise=True)
    assert res_post["success"] is True, f"POST loopback benchmark failed: {res_post.get('error')}"
    assert res_post["maxDeltaMs"] <= 200.0, (
        f"POST loopback roundtrip max latency {res_post['maxDeltaMs']:.2f}ms exceeded 200ms SLA"
    )
