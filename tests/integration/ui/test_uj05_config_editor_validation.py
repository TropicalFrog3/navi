"""
@file  test_uj05_config_editor_validation.py
@brief User journey test validating config editor input fields and save


UJ-05: Config Editor & Real-Time Validation Integration Tests.

Scenarios:
  1. Loading #sls-config-top-tab populates fields from GET /config.
  2. Editing booleans, UTF-8 international strings, and arrays with RequestBarrier
     holding in-flight save to assert intermediate saving state and single-submit lock.
  3. Boundary & invalid input handling: malformed DlcDataYaml returns HTTP 400 error
     without wiping user's form state.
  4. BackupRetentionCount clamping [1, 100] and tab switching state behavior (#sls-top-tab <-> #sls-config-top-tab).
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.import_config_page import ConfigEditorPage, ImportConfigPage


@pytest.fixture
def config_page(driver: BrowserDriver) -> ConfigEditorPage:
    return ConfigEditorPage(driver)


@pytest.fixture
def import_page(driver: BrowserDriver) -> ImportConfigPage:
    return ImportConfigPage(driver)


@pytest.mark.ui_p1
def test_uj05_config_loading_and_field_population(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Loading #sls-config-top-tab populates settings fields from GET /config."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")

    # Click config top tab
    config_page.open()
    assert config_page.is_open() is True

    # Assert populated fields match CallbackServerStub defaults
    driver.wait_for_condition(
        lambda: config_page.get_field_value("FakeName") == "Gordon Freeman",
        timeout=5.0,
        error_message="FakeName field did not populate with stub data",
    )
    assert config_page.get_field_value("FakeEmail") == "gordon@blackmesa.gov"
    assert config_page.is_toggle_checked("DisableFamilyShareLock") is False
    assert config_page.is_toggle_checked("SafeMode") is True
    assert config_page.get_field_value("CloudRetention") == "5"

    # Dirty pill is clean
    assert "saved" in config_page.get_dirty_pill_text().lower()
    assert config_page.is_dirty() is False
    assert config_page.is_discard_visible() is False


@pytest.mark.ui_p1
def test_uj05_config_editing_barrier_saving_state_and_lock(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Editing booleans, UTF-8 strings, and arrays with RequestBarrier asserting saving lock."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    # 1. Edit boolean, international UTF-8 name, and AppIds
    config_page.switch_tab("sls-cfg-sec-general")
    config_page.set_toggle("DisableFamilyShareLock", True)

    config_page.switch_tab("sls-cfg-sec-profile")
    unicode_name = "Гордон Фримен 日本語 🎮"
    config_page.set_field_value("FakeName", unicode_name)

    config_page.switch_tab("sls-cfg-sec-overrides")
    config_page.set_field_value("AppIds", "440, 730, 1443430")

    # Form is marked dirty
    assert config_page.is_dirty() is True
    assert config_page.is_discard_visible() is True

    discard_styles = driver.evaluate(
        """
        (() => {
            const el = document.getElementById('sls-cfg-discard-btn');
            if (!el) return null;
            const cs = window.getComputedStyle(el);
            return {
                hasClass: el.classList.contains('sls-cfg-btn'),
                borderTopWidth: parseFloat(cs.borderTopWidth) || 0,
                paddingTop: parseFloat(cs.paddingTop) || 0,
                paddingLeft: parseFloat(cs.paddingLeft) || 0,
                backgroundColor: cs.backgroundColor || ''
            };
        })()
        """,
        await_promise=False,
    )
    assert discard_styles is not None
    assert discard_styles["hasClass"] is True
    assert discard_styles["borderTopWidth"] >= 1
    assert discard_styles["paddingTop"] >= 4
    assert discard_styles["paddingLeft"] >= 8
    assert discard_styles["backgroundColor"] not in ("", "transparent", "rgba(0, 0, 0, 0)")

    # 2. Hold in-flight save request using RequestBarrier
    barrier = callback_server.hold_endpoint("/config/full")

    config_page.click_save()
    barrier.wait_until_reached(timeout=5.0)

    # 3. Assert intermediate saving state: single-submit lock is active
    assert config_page.is_save_disabled() is True
    assert "saving" in config_page.get_save_btn_text().lower()

    # 4. Release barrier to finish save
    barrier.release()

    # 5. Assert save completion
    config_page.wait_for_status_banner("saved", timeout=5.0)
    assert config_page.is_save_disabled() is True
    assert not config_page.is_dirty()
    assert config_page.is_discard_visible() is False

    # 6. Verify payload received by stub
    req = callback_server.last_request("/config/full", method="POST")
    assert req is not None
    body_data = json.loads(req["body"].decode("utf-8"))
    assert body_data.get("DisableFamilyShareLock") is True
    assert body_data.get("FakeName") == unicode_name
    assert 1443430 in body_data.get("AppIds", [])


@pytest.mark.ui_p1
def test_uj05_invalid_input_dlc_yaml_error_without_wiping_form(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Malformed DlcDataYaml returns HTTP 400 error without wiping user form edits."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    # Edit FakeName and enter invalid YAML
    config_page.switch_tab("sls-cfg-sec-profile")
    config_page.set_field_value("FakeName", "Custom Name Preserved")

    config_page.switch_tab("sls-cfg-sec-overrides")
    malformed_yaml = ": - malformed ::: [invalid {"
    config_page.set_field_value("DlcDataYaml", malformed_yaml)

    # Attempt to save
    config_page.click_save()

    # Stub rejects with 400 Bad Request
    config_page.wait_for_status_banner("Failed to parse DlcDataYaml", timeout=5.0)

    # Assert form state is preserved and NOT wiped out
    assert config_page.get_field_value("DlcDataYaml") == malformed_yaml
    config_page.switch_tab("sls-cfg-sec-profile")
    assert config_page.get_field_value("FakeName") == "Custom Name Preserved"
    assert config_page.is_dirty() is True

    # Confirm discarding changes resets dirty state and restores saved field values
    config_page.click_discard()
    driver.click("#sls-cfg-dialog-backdrop button:last-child")
    config_page.wait_for_status_banner("discarded", timeout=5.0)
    assert config_page.is_dirty() is False
    assert config_page.is_discard_visible() is False
    assert config_page.get_field_value("FakeName") == "Gordon Freeman"


@pytest.mark.ui_p1
def test_uj05_backup_retention_clamping_and_tab_switching(
    config_page: ConfigEditorPage,
    import_page: ImportConfigPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """BackupRetentionCount clamping [1, 100] and tab switching state behavior."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    # 1. Test lower bound clamping (0 -> 1)
    config_page.switch_tab("sls-cfg-sec-cloudsaves")
    config_page.set_field_value("CloudRetention", "0")
    config_page.click_save()
    config_page.wait_for_status_banner("saved", timeout=5.0)

    req = callback_server.last_request("/config/full", method="POST")
    assert req is not None
    body_data = json.loads(req["body"].decode("utf-8"))
    assert body_data["CloudSaves"]["BackupRetentionCount"] == 0  # input was 0
    # But server stub clamped it:
    assert callback_server.config_data["CloudSaves"]["BackupRetentionCount"] == 1

    # 2. Test upper bound clamping (999 -> 100)
    config_page.set_field_value("CloudRetention", "999")
    config_page.click_save()
    config_page.wait_for_status_banner("saved", timeout=5.0)
    assert callback_server.config_data["CloudSaves"]["BackupRetentionCount"] == 100

    # 3. Tab switching state behavior (#sls-top-tab <-> #sls-config-top-tab)
    # Switch to Import Lua tab
    driver.click("#sls-top-tab")
    import_page.wait_until_open()
    assert config_page.is_open() is False

    # Switch back to Navi Config tab
    driver.click("#sls-config-top-tab")
    config_page.wait_until_open()
    assert import_page.is_open() is False


@pytest.mark.ui_p1
def test_uj05_cloudredirect_ux_parity_controls(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Verify CloudRedirect v2.6.5 UX parity cards (Notifications, Stats Sync, R2/S3 Creds, Migration, Orphans, Backups)."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "cloud_manager.js")
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))
    config_page.switch_tab("sls-cfg-sec-cloudsaves")

    # 1. Verify Launch Notifications & Stats Sync checkboxes exist and round-trip through /config/full
    assert config_page.is_toggle_checked("CloudSaves-NotificationsEnabled") is True
    assert config_page.is_toggle_checked("StatsSync-Enabled") is False

    config_page.set_toggle("CloudSaves-NotificationsEnabled", False)
    config_page.set_toggle("StatsSync-Enabled", True)
    config_page.set_toggle("StatsSync-Playtime", False)
    config_page.click_save()
    config_page.wait_for_status_banner("saved", timeout=5.0)

    req = callback_server.last_request("/config/full", method="POST")
    assert req is not None
    body_data = json.loads(req["body"].decode("utf-8"))
    assert body_data["CloudSaves"]["NotificationsEnabled"] is False
    assert body_data["CloudSaves"]["StatsSync"]["Enabled"] is True
    assert body_data["CloudSaves"]["StatsSync"]["SyncPlaytime"] is False

    # 2. Verify R2 / S3 Credentials form visibility when selecting r2 vs s3
    driver.evaluate(
        """
        (() => {
            const sel = document.getElementById('sls-cfg-cs-provider');
            sel.value = 'r2';
            sel.dispatchEvent(new Event('change', { bubbles: true }));
        })()
        """
    )
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-cfg-cs-creds-form').style.display !== 'none'"),
        timeout=5.0,
    )
    assert driver.evaluate("document.getElementById('sls-cfg-cs-cred-account-id-wrap').style.display !== 'none'") is True

    # 3. Verify Migration Scan button updates status
    driver.click("#sls-cfg-cs-migrate-scan-btn")
    driver.wait_for_condition(
        lambda: "ready to migrate" in (driver.evaluate("document.getElementById('sls-cfg-cs-migrate-status').textContent") or ""),
        timeout=5.0,
    )

    migrate_spacing = driver.evaluate(
        """
        (() => {
            const srcLabel = document.querySelector('label[for="sls-cfg-cs-migrate-src"]');
            const dstLabel = document.querySelector('label[for="sls-cfg-cs-migrate-dst"]');
            const srcSelect = document.getElementById('sls-cfg-cs-migrate-src');
            const dstSelect = document.getElementById('sls-cfg-cs-migrate-dst');
            const scanBtn = document.getElementById('sls-cfg-cs-migrate-scan-btn');
            const startBtn = document.getElementById('sls-cfg-cs-migrate-start-btn');
            const item = srcSelect ? srcSelect.closest('.sls-cfg-item') : null;
            const card = item ? item.closest('.sls-cfg-card') : null;
            const subtitle = card ? card.children[1] : null;
            if (!srcLabel || !dstLabel || !srcSelect || !dstSelect || !scanBtn || !startBtn || !item || !subtitle) return null;
            const subRect = subtitle.getBoundingClientRect();
            const srcLabelRect = srcLabel.getBoundingClientRect();
            const dstLabelRect = dstLabel.getBoundingClientRect();
            const srcSelRect = srcSelect.getBoundingClientRect();
            const dstSelRect = dstSelect.getBoundingClientRect();
            const scanRect = scanBtn.getBoundingClientRect();
            const startRect = startBtn.getBoundingClientRect();
            return {
                hasMigrateClass: item.classList.contains('sls-cfg-migrate-item'),
                itemMarginTop: parseFloat(window.getComputedStyle(item).marginTop || '0'),
                gapFromSubtitleToLabel: srcLabelRect.top - subRect.bottom,
                labelsTopDiff: Math.abs(srcLabelRect.top - dstLabelRect.top),
                selectsBottomDiff: Math.abs(srcSelRect.bottom - dstSelRect.bottom),
                buttonsAlignedWithSelects: Math.abs(scanRect.bottom - srcSelRect.bottom) <= 4 && Math.abs(startRect.bottom - srcSelRect.bottom) <= 4,
            };
        })()
        """,
        await_promise=False,
    )
    assert migrate_spacing is not None
    assert migrate_spacing["hasMigrateClass"] is True
    assert migrate_spacing["itemMarginTop"] == 0
    assert migrate_spacing["gapFromSubtitleToLabel"] < 26
    assert migrate_spacing["labelsTopDiff"] <= 1
    assert migrate_spacing["selectsBottomDiff"] <= 1
    assert migrate_spacing["buttonsAlignedWithSelects"] is True

    # 4. Verify Scan Orphans button renders orphan panel
    driver.click("#sls-cfg-cs-scan-orphans-btn")
    driver.wait_for_condition(
        lambda: "480" in (driver.evaluate("document.getElementById('sls-cfg-cs-orphans-panel').textContent") or ""),
        timeout=5.0,
    )

    # 5. Verify Global Backups table renders stub backup entry
    driver.wait_for_condition(
        lambda: "backup_107410_auto_1.tar.gz" in (driver.evaluate("document.getElementById('sls-cfg-cs-backups-list').textContent") or ""),
        timeout=5.0,
    )

    # 6. Regression: long backup row titles/filenames stay within table and cell bounds
    long_title = "Very Long Game Title Regression Overflow Check Edition 2026 With Extra Words"
    long_file = "backup_107410_very_long_archive_filename_overflow_regression_check_0123456789.tar.gz"
    callback_server.set_response(
        "/cloud/backups",
        {
            "success": True,
            "backups": [
                {
                    "appId": 107410,
                    "gameTitle": long_title,
                    "fileName": long_file,
                    "fullPath": f"/storage/107410/backups/{long_file}",
                    "sizeBytes": 524288,
                    "timestamp": 1730582400,
                }
            ],
        },
    )
    driver.click("#sls-cfg-cs-backups-refresh")
    driver.wait_for_condition(
        lambda: long_file in (driver.evaluate("document.getElementById('sls-cfg-cs-backups-list').textContent") or ""),
        timeout=5.0,
    )
    geometry = driver.evaluate(
        """
        (() => {
            const list = document.getElementById('sls-cfg-cs-backups-list');
            const table = list ? list.querySelector('table.navi-steam-cloud-table') : null;
            const row = table ? table.querySelector('tr.navi-cloud-backup-row') : null;
            if (!table || !row) return null;
            const tableRect = table.getBoundingClientRect();
            const cells = Array.from(row.querySelectorAll('td')).map(td => {
                const r = td.getBoundingClientRect();
                return {
                    label: td.getAttribute('data-label') || '',
                    left: r.left,
                    right: r.right,
                    title: td.getAttribute('title') || ''
                };
            });
            const actionsTd = row.querySelector('td[data-label="ACTIONS"]');
            const actionsRect = actionsTd ? actionsTd.getBoundingClientRect() : null;
            const buttons = actionsTd
                ? Array.from(actionsTd.querySelectorAll('button')).map(btn => {
                    const r = btn.getBoundingClientRect();
                    return { text: btn.textContent || '', left: r.left, right: r.right };
                })
                : [];
            return {
                tableRect: { left: tableRect.left, right: tableRect.right },
                cells,
                actionsRect: actionsRect ? { left: actionsRect.left, right: actionsRect.right } : null,
                buttons
            };
        })()
        """,
        await_promise=False,
    )
    assert geometry is not None
    table_rect = geometry["tableRect"]
    for cell in geometry["cells"]:
        assert cell["right"] <= table_rect["right"] + 1, f"Cell {cell['label']} overflowed table right edge"
    titles_by_label = {c["label"]: c["title"] for c in geometry["cells"]}
    assert titles_by_label.get("GAME"), "GAME cell missing hover title tooltip"
    assert titles_by_label.get("BACKUP ARCHIVE") == long_file
    assert titles_by_label.get("DATE"), "DATE cell missing hover title tooltip"

    actions_rect = geometry["actionsRect"]
    assert actions_rect is not None
    assert len(geometry["buttons"]) == 2
    for btn in geometry["buttons"]:
        assert btn["left"] >= actions_rect["left"] - 1, f"Button {btn['text']} overflowed ACTIONS left edge"
        assert btn["right"] <= actions_rect["right"] + 1, f"Button {btn['text']} overflowed ACTIONS right edge"


@pytest.mark.ui_p1
def test_uj05_library_quick_add_buttons_and_header_spacing(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Verify Library quick-add button spacing, column layout, and dialog insertion across all Library cards."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))
    config_page.switch_tab("sls-cfg-sec-overrides")

    geometry = driver.evaluate(
        """
        (() => {
            const textarea = document.getElementById('sls-cfg-AppIds');
            const btn = document.getElementById('sls-cfg-add-app-btn');
            const item = textarea ? textarea.closest('.sls-cfg-item') : null;
            if (!textarea || !btn || !item) return null;
            const headerRow = item.firstElementChild;
            const textCol = headerRow ? headerRow.firstElementChild : null;
            const titleEl = textCol ? textCol.children[0] : null;
            const subtitleEl = textCol ? textCol.children[1] : null;
            if (!titleEl || !subtitleEl) return null;
            const btnRect = btn.getBoundingClientRect();
            const titleRect = titleEl.getBoundingClientRect();
            const subtitleRect = subtitleEl.getBoundingClientRect();
            const textareaRect = textarea.getBoundingClientRect();
            return {
                flexDirection: window.getComputedStyle(item).flexDirection,
                btnLeft: btnRect.left,
                btnBottom: btnRect.bottom,
                titleRight: titleRect.right,
                subtitleRight: subtitleRect.right,
                textareaTop: textareaRect.top,
            };
        })()
        """,
        await_promise=False,
    )
    assert geometry is not None
    assert geometry["flexDirection"] == "column"
    assert geometry["btnLeft"] - geometry["titleRight"] >= 16
    assert geometry["btnLeft"] - geometry["subtitleRight"] >= 16
    assert geometry["btnBottom"] <= geometry["textareaTop"]

    expected_btn_ids = [
        "sls-cfg-add-app-btn",
        "sls-cfg-add-extra-app-btn",
        "sls-cfg-add-offline-btn",
        "sls-cfg-add-fakeappid-btn",
        "sls-cfg-add-gametitle-btn",
        "sls-cfg-add-manifest-btn",
        "sls-cfg-add-depot-blacklist-btn",
        "sls-cfg-add-apptoken-btn",
        "sls-cfg-add-cdkey-btn",
    ]
    missing_btns = driver.evaluate(
        f"""
        (() => {json.dumps(expected_btn_ids)}.filter(id => !document.getElementById(id)))()
        """,
        await_promise=False,
    )
    assert missing_btns == [], f"Missing Library quick-add buttons: {missing_btns}"

    driver.click("#sls-cfg-add-fakeappid-btn")
    driver.wait_for_condition(
        lambda: bool(driver.evaluate("document.getElementById('sls-cfg-dialog-backdrop')", await_promise=False)),
        timeout=5.0,
    )
    driver.evaluate(
        """
        (() => {
            const k = document.querySelector('#sls-cfg-dialog-backdrop input[data-input-key="k"]');
            const v = document.querySelector('#sls-cfg-dialog-backdrop input[data-input-key="v"]');
            if (k) { k.value = '480'; k.dispatchEvent(new Event('input', { bubbles: true })); }
            if (v) { v.value = '440'; v.dispatchEvent(new Event('input', { bubbles: true })); }
        })()
        """,
        await_promise=False,
    )
    driver.click("#sls-cfg-dialog-backdrop button:last-child")

    driver.wait_for_condition(
        lambda: "480: 440" in config_page.get_field_value("FakeAppIds"),
        timeout=5.0,
        error_message="Expected '480: 440' to be appended to FakeAppIds",
    )
    assert config_page.is_dirty() is True


@pytest.mark.ui_p1
def test_uj05_supernav_non_library_overlay_navigation(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Opening Navi Config from a non-library SuperNav route navigates to /library/home and handles dismiss cleanly."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")

    # Attach mock React Fiber history on the first SuperNav tab at /store
    attached = driver.evaluate(
        """
        (() => {
            const tabEl = document.querySelector('._2D64jIEK7wpUR_NlObDW76 ._2Lu3d-5qLmW4i19ysTt2jT');
            if (!tabEl) return false;
            const pushedPaths = [];
            const listeners = [];
            const state = {
                pushedPaths,
                listeners,
                unlistenCount: 0,
            };
            const mockHistory = {
                location: { pathname: '/store' },
                push(path) {
                    pushedPaths.push(path);
                    mockHistory.location = { pathname: path };
                    listeners.slice().forEach((cb) => cb({ pathname: path }));
                },
                listen(cb) {
                    listeners.push(cb);
                    return function () {
                        const idx = listeners.indexOf(cb);
                        if (idx >= 0) {
                            listeners.splice(idx, 1);
                            state.unlistenCount += 1;
                        }
                    };
                },
            };
            state.history = mockHistory;
            state.simulateNavigate = (path) => {
                mockHistory.location = { pathname: path };
                listeners.slice().forEach((cb) => cb({ pathname: path }));
            };
            window.__testMockHistory = state;
            tabEl.__reactFiber$test = {
                memoizedProps: { history: mockHistory },
                return: null,
            };
            return true;
        })()
        """,
        await_promise=False,
    )
    assert attached is True

    inactive_colors_before = driver.evaluate(
        """
        (() => {
            const storeText = document.querySelector('#steam-supernav ._2Lu3d-5qLmW4i19ysTt2jT:nth-child(1) ._19axKcqYRuaJ8vdYKYmtTQ');
            const commText = document.querySelector('#steam-supernav ._2Lu3d-5qLmW4i19ysTt2jT:nth-child(3) ._19axKcqYRuaJ8vdYKYmtTQ');
            if (!storeText || !commText) return null;
            return {
                store: window.getComputedStyle(storeText).color,
                community: window.getComputedStyle(commText).color,
            };
        })()
        """,
        await_promise=False,
    )
    assert inactive_colors_before is not None

    # 1. Open Navi Config from /store -> pushes /library/home and stays open
    config_page.open()
    assert config_page.is_open() is True
    assert driver.evaluate("window.__testMockHistory.pushedPaths", await_promise=False) == ["/library/home"]

    tab_styles_after = driver.evaluate(
        """
        (() => {
            const storeText = document.querySelector('#steam-supernav ._2Lu3d-5qLmW4i19ysTt2jT:nth-child(1) ._19axKcqYRuaJ8vdYKYmtTQ');
            const libText = document.querySelector('#steam-supernav ._2Lu3d-5qLmW4i19ysTt2jT:nth-child(2) ._19axKcqYRuaJ8vdYKYmtTQ');
            const commText = document.querySelector('#steam-supernav ._2Lu3d-5qLmW4i19ysTt2jT:nth-child(3) ._19axKcqYRuaJ8vdYKYmtTQ');
            if (!storeText || !libText || !commText) return null;
            return {
                store: window.getComputedStyle(storeText).color,
                community: window.getComputedStyle(commText).color,
                libraryAfterDisplay: window.getComputedStyle(libText, '::after').display,
            };
        })()
        """,
        await_promise=False,
    )
    assert tab_styles_after is not None
    assert tab_styles_after["store"] == inactive_colors_before["store"]
    assert tab_styles_after["community"] == inactive_colors_before["community"]
    assert tab_styles_after["store"] != "rgb(150, 150, 150)"
    assert tab_styles_after["community"] != "rgb(150, 150, 150)"
    assert tab_styles_after["libraryAfterDisplay"] == "none"

    # Wait for navigation guard window.__slsNavigating to clear
    driver.wait_for_condition(
        lambda: not driver.evaluate("Boolean(window.__slsNavigating)", await_promise=False),
        timeout=5.0,
        error_message="window.__slsNavigating guard did not clear",
    )

    # 2. Simulate React Router navigating to /store -> hides Navi Config and detaches listener
    driver.evaluate("window.__testMockHistory.simulateNavigate('/store')", await_promise=False)
    driver.wait_for_condition(
        lambda: config_page.is_open() is False,
        timeout=5.0,
        error_message="Navi Config did not hide when React Router navigated to /store",
    )
    assert driver.evaluate("window.__testMockHistory.unlistenCount", await_promise=False) >= 1

    # 3. When already on /library/app/440, opening Navi Config does NOT push /library/home again
    prev_unlisten = int(driver.evaluate("window.__testMockHistory.unlistenCount", await_promise=False) or 0)
    driver.evaluate(
        """
        (() => {
            window.__testMockHistory.pushedPaths.length = 0;
            window.__testMockHistory.history.location = { pathname: '/library/app/440' };
        })()
        """,
        await_promise=False,
    )
    config_page.open()
    assert config_page.is_open() is True
    assert driver.evaluate("window.__testMockHistory.pushedPaths", await_promise=False) == []

    # 4. Clicking native LIBRARY SuperNav tab closes Navi Config (capture-phase dismissal)
    driver.click("#steam-supernav ._2Lu3d-5qLmW4i19ysTt2jT:nth-child(2)")
    driver.wait_for_condition(
        lambda: config_page.is_open() is False,
        timeout=5.0,
        error_message="Clicking native LIBRARY SuperNav tab did not close Navi Config",
    )
    assert driver.evaluate("window.__testMockHistory.unlistenCount", await_promise=False) > prev_unlisten



