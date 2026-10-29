// CEF Annotator Overlay (Injected into Steam CEF / Web targets)
// Injected into either Main world (Steam shell) or Isolated world (Remote store pages).
// Template placeholders (__*__) are substituted at injection time by annotator.py.

(function () {
  'use strict';

  try {
    // Clean up any stale overlay instances/APIs from earlier runs
    try {
      Object.keys(window).filter(k => k.startsWith('__cef_annot_api_')).forEach(k => {
        try { window[k].cleanup(); } catch (e) {}
        delete window[k];
      });
    } catch (e) {}

    const GUARD = '__GUARD_NAME__';
    if (window[GUARD]) {
      return; // Already injected in this context
    }
    window[GUARD] = true;

    const BINDING_NAME = '__BINDING_NAME__';
    const HOST_ID = '__HOST_ID__';
    const API_NAME = '__API_NAME__';
    const TARGET_ID = '__TARGET_ID__';
    const WORLD_MODE = '__WORLD_MODE__'; // 'main' or 'isolated'
    let currentRouteHint = '__ROUTE_HINT__';
    const INITIAL_ANNOTATIONS = __INITIAL_ANNOTATIONS__;

    // --- State ---
    let isAnnotateMode = false;
    let hoveredElement = null;
    let selectedElement = null;
    let originalInlineCss = '';
    let originalStyles = {}; // Baseline computed styles
    let pendingChanges = {}; // prop -> { original, new }
    let lastHeartbeat = Date.now();
    let isConnected = true;
    let activePins = Array.isArray(INITIAL_ANNOTATIONS) ? INITIAL_ANNOTATIONS : [];
    let mutationObserver = null;
    let repositionTimer = null;

    let activeTab = 'comment'; // 'comment', 'design', 'variants'
    let spacingMode = '2way'; // '2way' or '4way'
    let currentLayout = 'block'; // 'block', 'flex-col', 'flex-row', 'grid'

    // --- Host & Shadow Root Setup ---
    const host = document.createElement('div');
    host.id = HOST_ID;
    host.style.cssText = 'position:fixed; top:0; left:0; width:0; height:0; z-index:2147483647; pointer-events:none;';
    
    // Attach closed shadow root so page CSS does not leak into overlay and vice-versa
    const shadow = host.attachShadow({ mode: 'closed' });

    // Styles
    const cssText = `
      * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; font-size: 12px; }
      
      /* Floating Annotate Toolbar / Button (Movable) */
      #corner-btn {
        position: fixed;
        bottom: 20px;
        right: 20px;
        pointer-events: auto;
        display: flex;
        align-items: center;
        background: #101318;
        color: #c6d4df;
        border: 1px solid #283243;
        border-radius: 9999px;
        cursor: grab;
        box-shadow: 0 8px 24px rgba(0,0,0,0.65), 0 0 1px rgba(255,255,255,0.12) inset;
        user-select: none;
        transition: background 0.15s, border-color 0.15s, box-shadow 0.15s;
        z-index: 2147483647;
        padding: 6px 14px 6px 10px;
        height: 38px;
        box-sizing: border-box;
      }
      #corner-btn:hover {
        background: #161b24;
        border-color: #3b4961;
        box-shadow: 0 10px 28px rgba(0,0,0,0.75);
      }
      #corner-btn:active, #corner-btn.dragging {
        cursor: grabbing;
      }
      #corner-btn.active {
        background: #0f1217;
        border-color: #2b3545;
        box-shadow: 0 12px 36px rgba(0,0,0,0.85), 0 0 1px rgba(255,255,255,0.15) inset;
        padding: 0 12px 0 14px;
        height: 38px;
      }

      /* Toolbar Standby Mode */
      #toolbar-standby {
        display: flex;
        align-items: center;
        gap: 7px;
      }
      #corner-btn.active #toolbar-standby {
        display: none;
      }

      /* Toolbar Active Mode (Attachment Design) */
      #toolbar-active {
        display: none;
        align-items: center;
      }
      #corner-btn.active #toolbar-active {
        display: flex;
      }

      .toolbar-logo {
        flex-shrink: 0;
        filter: drop-shadow(0 0 4px rgba(244, 63, 94, 0.4));
      }
      .toolbar-divider {
        width: 1px;
        height: 16px;
        background: #283242;
        margin: 0 10px;
        flex-shrink: 0;
      }
      .toolbar-text {
        font-size: 12px;
        color: #8e9cae;
        font-weight: 500;
        white-space: nowrap;
        letter-spacing: 0.1px;
      }
      .toolbar-dot {
        color: #4b5563;
        margin: 0 6px;
        font-size: 10px;
        user-select: none;
      }
      .toolbar-keys {
        display: inline-flex;
        align-items: center;
        gap: 3px;
        margin-right: 5px;
      }
      .key-badge {
        display: inline-flex;
        align-items: center;
        justify-content: center;
        min-width: 19px;
        height: 20px;
        padding: 0 5px;
        background: #1b212c;
        border: 1px solid #2e394b;
        border-radius: 4px;
        color: #c6d4df;
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace;
        font-size: 11px;
        font-weight: 500;
        cursor: pointer;
        transition: all 0.15s;
        box-sizing: border-box;
      }
      .key-badge:hover {
        background: #283344;
        border-color: #4a5c78;
        color: #ffffff;
        transform: translateY(-1px);
      }
      .key-badge:active {
        background: #3b82f6;
        border-color: #60a5fa;
        color: #ffffff;
        transform: translateY(1px);
      }
      #key-badge-del:hover {
        background: #351c24;
        border-color: #f43f5e;
        color: #fda4af;
      }
      #key-badge-del:active {
        background: #e11d48;
        border-color: #fb7185;
        color: #ffffff;
      }
      #toolbar-close-btn {
        background: none;
        border: none;
        color: #718298;
        cursor: pointer;
        font-size: 13px;
        padding: 3px 6px;
        border-radius: 4px;
        display: flex;
        align-items: center;
        justify-content: center;
        transition: all 0.15s;
        line-height: 1;
      }
      #toolbar-close-btn:hover {
        color: #ffffff;
        background: rgba(255,255,255,0.08);
      }

      #corner-btn .status-dot { width: 8px; height: 8px; border-radius: 50%; background: #4cd964; }
      #corner-btn.disconnected .status-dot { background: #e0ae00; }
      #corner-btn .mode-badge { font-size: 10px; padding: 1px 4px; border-radius: 4px; background: rgba(255,255,255,0.1); text-transform: uppercase; letter-spacing: 0.5px; }

      /* Hover Highlight Box */
      #hover-highlight {
        position: fixed;
        display: none;
        pointer-events: none;
        border: 2px solid #66c0f4;
        background: rgba(102, 192, 244, 0.15);
        border-radius: 3px;
        box-shadow: 0 0 8px rgba(102, 192, 244, 0.4);
        z-index: 2147483640;
        transition: top 0.05s ease, left 0.05s ease, width 0.05s ease, height 0.05s ease;
      }
      #hover-badge {
        position: absolute;
        bottom: 100%;
        left: 0;
        margin-bottom: 4px;
        background: #171a21;
        color: #66c0f4;
        border: 1px solid #3b4654;
        border-radius: 4px;
        padding: 3px 6px;
        font-size: 11px;
        white-space: nowrap;
        box-shadow: 0 2px 6px rgba(0,0,0,0.5);
        pointer-events: none;
      }

      /* Fullscreen Crosshair Overlay */
      #crosshair-shield {
        position: fixed;
        top: 0;
        left: 0;
        width: 100vw;
        height: 100vh;
        cursor: crosshair;
        z-index: 2147483630;
        display: none;
        pointer-events: none;
      }

      /* Movable Popup Dialog */
      #popup-dialog {
        position: fixed;
        display: none;
        pointer-events: auto;
        width: 320px;
        min-width: 320px;
        max-width: calc(100vw - 20px);
        min-height: 340px;
        max-height: calc(100vh - 20px);
        background: #12151c;
        border: 1px solid #28303e;
        border-radius: 12px;
        box-shadow: 0 16px 40px rgba(0,0,0,0.85);
        z-index: 2147483646;
        color: #c6d4df;
        flex-direction: column;
        user-select: none;
        overflow: hidden;
        resize: both;
      }

      #popup-dialog::-webkit-resizer {
        background: transparent;
      }

      /* Resize Handles */
      #popup-resize-handle {
        position: absolute;
        right: 0;
        bottom: 0;
        width: 18px;
        height: 18px;
        cursor: nwse-resize;
        display: flex;
        align-items: flex-end;
        justify-content: flex-end;
        padding: 0 4px 4px 0;
        box-sizing: border-box;
        z-index: 25;
        touch-action: none;
      }
      #popup-resize-handle svg {
        pointer-events: none;
        opacity: 0.45;
        transition: opacity 0.15s, transform 0.15s;
      }
      #popup-resize-handle:hover svg {
        opacity: 1;
        transform: scale(1.15);
      }
      #popup-resize-r {
        position: absolute;
        top: 0;
        right: 0;
        width: 6px;
        height: calc(100% - 18px);
        cursor: ew-resize;
        z-index: 20;
      }
      #popup-resize-b {
        position: absolute;
        bottom: 0;
        left: 0;
        height: 6px;
        width: calc(100% - 18px);
        cursor: ns-resize;
        z-index: 20;
      }

      /* Drag Handle */
      #popup-drag-handle {
        width: 100%;
        padding: 7px 0 2px 0;
        cursor: grab;
        display: flex;
        justify-content: center;
        align-items: center;
        flex-shrink: 0;
      }
      #popup-drag-handle:active {
        cursor: grabbing;
      }
      #popup-drag-bar {
        width: 38px;
        height: 4px;
        background: #3a4556;
        border-radius: 2px;
      }

      /* Header */
      #popup-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 4px 14px 10px 14px;
        cursor: grab;
        flex-shrink: 0;
      }
      #popup-header:active {
        cursor: grabbing;
      }
      #popup-title {
        font-size: 13px;
        color: #8f9db3;
        display: flex;
        align-items: center;
        gap: 5px;
      }
      #popup-target-tag {
        color: #ffffff;
        font-weight: 600;
        font-size: 13px;
      }
      #popup-revert-btn {
        background: none;
        border: none;
        color: #7d8b9f;
        cursor: pointer;
        font-size: 15px;
        padding: 2px 4px;
        border-radius: 4px;
        transition: all 0.2s;
      }
      #popup-revert-btn:hover {
        color: #66c0f4;
        background: rgba(255,255,255,0.06);
      }

      /* Tab Row */
      #popup-tabs {
        display: flex;
        gap: 6px;
        padding: 0 12px 10px 12px;
        border-bottom: 1px solid #202734;
        flex-shrink: 0;
      }
      .popup-tab {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 5px;
        padding: 6px 4px;
        background: #171c26;
        color: #8f9db3;
        border: 1px solid #283243;
        border-radius: 6px;
        cursor: pointer;
        font-size: 11px;
        font-weight: 500;
        transition: all 0.15s;
        position: relative;
      }
      .popup-tab:hover {
        background: #1e2533;
        color: #ffffff;
      }
      .popup-tab.active {
        background: #232c3d;
        color: #ffffff;
        border-color: #43536b;
      }
      .change-dot {
        width: 5px;
        height: 5px;
        border-radius: 50%;
        background: #60a5fa;
        display: none;
      }
      .change-dot.active {
        display: inline-block;
      }

      /* Content Area */
      #popup-content {
        flex: 1 1 auto;
        min-height: 0;
        overflow-y: auto;
        padding: 12px;
      }
      #popup-content::-webkit-scrollbar {
        width: 5px;
      }
      #popup-content::-webkit-scrollbar-track {
        background: #12151c;
      }
      #popup-content::-webkit-scrollbar-thumb {
        background: #2b3545;
        border-radius: 3px;
      }

      /* Tab Panes */
      .tab-pane {
        display: none;
      }
      .tab-pane.active {
        display: block;
      }

      /* Textarea Box */
      .textarea-box {
        background: #0d1016;
        border: 1px solid #3b82f6;
        border-radius: 8px;
        padding: 10px;
        position: relative;
        box-shadow: 0 0 10px rgba(59, 130, 246, 0.2);
      }
      .custom-textarea {
        width: 100%;
        height: 95px;
        background: transparent;
        border: none;
        color: #fff;
        resize: vertical;
        outline: none;
        font-size: 12px;
        line-height: 1.4;
      }
      .textarea-bottom-bar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-top: 6px;
      }
      .attach-btn {
        background: #1c2330;
        border: 1px solid #2e394b;
        border-radius: 6px;
        color: #c6d4df;
        padding: 3px 8px;
        cursor: pointer;
        font-size: 12px;
      }
      .attach-btn:hover {
        background: #273245;
      }
      .attach-popover {
        position: absolute;
        bottom: 38px;
        left: 8px;
        background: #171d27;
        border: 1px solid #2e3b4f;
        border-radius: 8px;
        padding: 6px;
        display: none;
        flex-direction: column;
        gap: 4px;
        box-shadow: 0 6px 18px rgba(0,0,0,0.7);
        z-index: 20;
      }
      .attach-popover.active {
        display: flex;
      }
      .popover-item {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 5px 10px;
        background: none;
        border: none;
        color: #c6d4df;
        font-size: 11px;
        cursor: pointer;
        border-radius: 4px;
        text-align: left;
      }
      .popover-item:hover {
        background: #252e3e;
        color: #fff;
      }
      .popover-close {
        padding: 3px 6px;
        background: #222a38;
        border: 1px solid #364254;
        border-radius: 4px;
        color: #9ab;
        cursor: pointer;
        font-size: 10px;
        align-self: flex-start;
      }
      .shortcut-hint {
        font-size: 10px;
        color: #556275;
      }

      /* Accordion Styles */
      .acc-item {
        margin-bottom: 6px;
        border: 1px solid #202734;
        border-radius: 6px;
        overflow: hidden;
      }
      .acc-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 8px 10px;
        background: #161b24;
        cursor: pointer;
        color: #d0d7e3;
        font-weight: 500;
        font-size: 12px;
      }
      .acc-header:hover {
        background: #1c2330;
      }
      .acc-title-wrap {
        display: flex;
        align-items: center;
        gap: 6px;
      }
      .acc-caret {
        font-size: 9px;
        color: #7b899e;
        transition: transform 0.2s;
      }
      .acc-item.expanded .acc-caret {
        transform: rotate(90deg);
      }
      .acc-body {
        display: none;
        padding: 10px;
        background: #0d1016;
        border-top: 1px solid #1a202c;
      }
      .acc-item.expanded .acc-body {
        display: block;
      }

      /* Design Grid / Flex controls */
      .icon-btn-row {
        display: flex;
        gap: 4px;
        margin-bottom: 8px;
      }
      .icon-btn {
        flex: 1;
        padding: 6px 4px;
        background: #161b24;
        border: 1px solid #283243;
        border-radius: 4px;
        color: #8f9db3;
        cursor: pointer;
        font-size: 11px;
        display: flex;
        align-items: center;
        justify-content: center;
        transition: all 0.15s;
      }
      .icon-btn:hover {
        background: #1e2634;
        color: #fff;
      }
      .icon-btn.active {
        background: #232c3d;
        color: #fff;
        border-color: #66c0f4;
      }

      /* 3x3 Alignment Matrix */
      .align-matrix-wrap {
        display: flex;
        gap: 12px;
        margin-bottom: 8px;
      }
      .matrix-grid {
        width: 74px;
        height: 74px;
        background: #161c26;
        border: 1px solid #283243;
        border-radius: 6px;
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        grid-template-rows: repeat(3, 1fr);
        padding: 4px;
        gap: 2px;
      }
      .matrix-cell {
        display: flex;
        align-items: center;
        justify-content: center;
        cursor: pointer;
        border-radius: 3px;
      }
      .matrix-dot {
        width: 4px;
        height: 4px;
        border-radius: 50%;
        background: #5d6c82;
      }
      .matrix-cell:hover .matrix-dot {
        background: #66c0f4;
      }
      .matrix-cell.active {
        background: #283446;
      }
      .matrix-cell.active .matrix-dot {
        background: #66c0f4;
      }
      .matrix-options {
        display: flex;
        flex-direction: column;
        gap: 4px;
        font-size: 11px;
        justify-content: center;
      }
      .checkbox-label {
        display: flex;
        align-items: center;
        gap: 6px;
        cursor: pointer;
        font-size: 11px;
        color: #c0cbdb;
      }

      /* Inputs */
      .input-row {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 6px;
        font-size: 11px;
      }
      .field-label {
        color: #8f9db3;
      }
      .field-input {
        width: 60px;
        background: #141922;
        border: 1px solid #283243;
        border-radius: 4px;
        color: #fff;
        padding: 4px 6px;
        font-size: 11px;
        text-align: center;
        outline: none;
      }
      .field-input:focus {
        border-color: #66c0f4;
      }
      .spacing-quad {
        display: flex;
        gap: 4px;
        align-items: center;
      }
      .quad-item {
        display: flex;
        align-items: center;
        gap: 2px;
      }
      .quad-label {
        font-size: 9px;
        color: #7b889b;
      }
      .quad-input {
        width: 32px;
        background: #141922;
        border: 1px solid #283243;
        border-radius: 3px;
        color: #fff;
        padding: 3px 2px;
        font-size: 10px;
        text-align: center;
        outline: none;
      }
      .toggle-mode-btn {
        background: #161c26;
        border: 1px solid #283243;
        border-radius: 4px;
        color: #8f9db3;
        cursor: pointer;
        padding: 3px 6px;
        font-size: 11px;
      }
      .toggle-mode-btn:hover {
        color: #fff;
        border-color: #4a596e;
      }

      /* Color droplets & swatches */
      .color-row {
        display: flex;
        align-items: center;
        gap: 6px;
      }
      .color-swatch-btn {
        width: 22px;
        height: 22px;
        border-radius: 4px;
        border: 1px solid #455266;
        cursor: pointer;
      }
      .color-palette-popover {
        position: absolute;
        top: 40px;
        right: 10px;
        background: #171d27;
        border: 1px solid #2e3b4f;
        border-radius: 8px;
        padding: 8px;
        display: none;
        grid-template-columns: repeat(4, 22px);
        gap: 5px;
        box-shadow: 0 6px 18px rgba(0,0,0,0.8);
        z-index: 30;
      }
      .color-palette-popover.active {
        display: grid;
      }
      .palette-swatch {
        width: 22px;
        height: 22px;
        border-radius: 4px;
        border: 1px solid rgba(255,255,255,0.2);
        cursor: pointer;
      }

      /* Code textareas */
      .code-editor {
        width: 100%;
        height: 90px;
        background: #090c10;
        border: 1px solid #202734;
        border-radius: 4px;
        color: #9cdcfe;
        font-family: monospace;
        font-size: 11px;
        padding: 6px;
        resize: vertical;
        outline: none;
        white-space: pre;
      }
      .variants-hint {
        font-size: 11px;
        color: #7b889b;
        margin-top: 8px;
        line-height: 1.4;
      }

      /* Footer */
      #popup-footer {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 8px 12px 12px 12px;
        border-top: 1px solid #1a202c;
        flex-shrink: 0;
        position: relative;
      }
      #viewport-indicator {
        font-size: 10px;
        color: #556275;
        display: flex;
        align-items: center;
        gap: 4px;
      }
      .footer-actions {
        display: flex;
        align-items: center;
        gap: 8px;
      }
      #popup-cancel-btn {
        background: none;
        border: none;
        color: #8f9db3;
        cursor: pointer;
        font-size: 11px;
        font-weight: 500;
        padding: 6px 8px;
      }
      #popup-cancel-btn:hover {
        color: #fff;
      }
      #popup-action-btn {
        padding: 6px 14px;
        background: #eb4566;
        border: none;
        border-radius: 16px;
        color: #ffffff;
        font-weight: 600;
        font-size: 11px;
        cursor: pointer;
        transition: all 0.15s;
        box-shadow: 0 2px 8px rgba(235, 69, 102, 0.4);
      }
      #popup-action-btn:hover {
        background: #f05a78;
        transform: translateY(-1px);
      }
      #popup-action-btn:disabled {
        background: #3e4856;
        color: #8894a4;
        cursor: not-allowed;
        box-shadow: none;
      }

      /* Pin Badges */
      .annot-pin {
        position: fixed;
        pointer-events: auto;
        width: 22px;
        height: 22px;
        border-radius: 50%;
        background: #66c0f4;
        color: #171a21;
        font-size: 11px;
        font-weight: bold;
        display: flex;
        align-items: center;
        justify-content: center;
        cursor: pointer;
        box-shadow: 0 2px 6px rgba(0,0,0,0.5);
        z-index: 2147483635;
        border: 2px solid #fff;
        transition: transform 0.15s ease;
      }
      .annot-pin:hover {
        transform: scale(1.2);
        background: #a4d007;
      }
      .annot-pin-tooltip {
        position: absolute;
        bottom: 115%;
        left: 50%;
        transform: translateX(-50%);
        background: #171a21;
        color: #fff;
        padding: 6px 10px;
        border-radius: 4px;
        font-size: 11px;
        white-space: nowrap;
        max-width: 240px;
        overflow: hidden;
        text-overflow: ellipsis;
        box-shadow: 0 4px 10px rgba(0,0,0,0.6);
        border: 1px solid #3b4654;
        display: none;
        pointer-events: none;
        z-index: 2147483640;
      }
      .annot-pin:hover .annot-pin-tooltip {
        display: block;
      }
    `;

    try {
      const sheet = new CSSStyleSheet();
      sheet.replaceSync(cssText);
      shadow.adoptedStyleSheets = [sheet];
    } catch (e) {
      const styleEl = document.createElement('style');
      styleEl.textContent = cssText;
      shadow.appendChild(styleEl);
    }

    // Elements inside shadow root
    const container = document.createElement('div');
    container.innerHTML = `
      <div id="corner-btn" title="Toggle Annotator (Alt+Shift+A) • Drag to move">
        <!-- Standby Pill -->
        <div id="toolbar-standby">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" class="toolbar-logo">
            <defs>
              <linearGradient id="vibe-horn-left-sb" x1="0%" y1="0%" x2="100%" y2="100%">
                <stop offset="0%" stop-color="#ff6b4a"/>
                <stop offset="100%" stop-color="#e11d48"/>
              </linearGradient>
              <linearGradient id="vibe-horn-right-sb" x1="100%" y1="0%" x2="0%" y2="100%">
                <stop offset="0%" stop-color="#ff6b4a"/>
                <stop offset="100%" stop-color="#e11d48"/>
              </linearGradient>
              <linearGradient id="vibe-body-stroke-sb" x1="0%" y1="0%" x2="100%" y2="100%">
                <stop offset="0%" stop-color="#c084fc"/>
                <stop offset="50%" stop-color="#f43f5e"/>
                <stop offset="100%" stop-color="#fb923c"/>
              </linearGradient>
            </defs>
            <path d="M4.5 10L1.5 2.5L8.5 6.5Z" fill="url(#vibe-horn-left-sb)"/>
            <path d="M19.5 10L22.5 2.5L15.5 6.5Z" fill="url(#vibe-horn-right-sb)"/>
            <rect x="2.5" y="7" width="19" height="14" rx="7" fill="#1b122c" stroke="url(#vibe-body-stroke-sb)" stroke-width="1.8"/>
            <rect x="7" y="11.5" width="2.8" height="5" rx="1.4" fill="#ffffff"/>
            <rect x="14.2" y="11.5" width="2.8" height="5" rx="1.4" fill="#ffffff"/>
          </svg>
          <div class="status-dot"></div>
          <span style="font-weight:600; font-size:12px;">Annotate</span>
          <span class="mode-badge">${WORLD_MODE}</span>
        </div>

        <!-- Active Mode Toolbar (Matches Reference Mockup) -->
        <div id="toolbar-active">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" class="toolbar-logo">
            <defs>
              <linearGradient id="vibe-horn-left-act" x1="0%" y1="0%" x2="100%" y2="100%">
                <stop offset="0%" stop-color="#ff6b4a"/>
                <stop offset="100%" stop-color="#e11d48"/>
              </linearGradient>
              <linearGradient id="vibe-horn-right-act" x1="100%" y1="0%" x2="0%" y2="100%">
                <stop offset="0%" stop-color="#ff6b4a"/>
                <stop offset="100%" stop-color="#e11d48"/>
              </linearGradient>
              <linearGradient id="vibe-body-stroke-act" x1="0%" y1="0%" x2="100%" y2="100%">
                <stop offset="0%" stop-color="#c084fc"/>
                <stop offset="50%" stop-color="#f43f5e"/>
                <stop offset="100%" stop-color="#fb923c"/>
              </linearGradient>
            </defs>
            <path d="M4.5 10L1.5 2.5L8.5 6.5Z" fill="url(#vibe-horn-left-act)"/>
            <path d="M19.5 10L22.5 2.5L15.5 6.5Z" fill="url(#vibe-horn-right-act)"/>
            <rect x="2.5" y="7" width="19" height="14" rx="7" fill="#1b122c" stroke="url(#vibe-body-stroke-act)" stroke-width="1.8"/>
            <rect x="7" y="11.5" width="2.8" height="5" rx="1.4" fill="#ffffff"/>
            <rect x="14.2" y="11.5" width="2.8" height="5" rx="1.4" fill="#ffffff"/>
          </svg>
          <div class="toolbar-divider"></div>
          <span class="toolbar-text">Click to capture</span>
          <span class="toolbar-dot">•</span>
          <div class="toolbar-keys">
            <span class="key-badge" id="key-badge-up" title="Walk up to parent element (↑)">↑</span>
            <span class="key-badge" id="key-badge-down" title="Walk down to child element (↓)">↓</span>
            <span class="key-badge" id="key-badge-enter" title="Capture target (Enter)">↵</span>
          </div>
          <span class="toolbar-text">to fine-tune target</span>
          <span class="toolbar-dot">•</span>
          <span class="key-badge" id="key-badge-del" title="Delete all annotations (Del / Shift+Backspace)">Del</span>
          <span class="toolbar-text" style="margin-left: 5px;">to delete all</span>
          <span class="toolbar-dot">•</span>
          <span class="key-badge" id="key-badge-esc" title="Exit capture mode (Esc)">Esc</span>
          <span class="toolbar-text" style="margin-left: 5px;">to stop</span>
          <div class="toolbar-divider"></div>
          <button type="button" id="toolbar-close-btn" title="Exit capture mode (Esc)">✕</button>
        </div>
      </div>
      <div id="crosshair-shield"></div>
      <div id="hover-highlight">
        <div id="hover-badge"></div>
      </div>
      
      <!-- Movable Popup Dialog -->
      <div id="popup-dialog">
        <div id="popup-drag-handle">
          <div id="popup-drag-bar"></div>
        </div>
        
        <div id="popup-header">
          <div id="popup-title">Editing <span id="popup-target-tag">element</span></div>
          <button type="button" id="popup-revert-btn" title="Revert changes">↻</button>
        </div>

        <div id="popup-tabs">
          <button type="button" class="popup-tab active" data-tab="comment">💬 Comment</button>
          <button type="button" class="popup-tab" data-tab="design">⛶ Design <span class="change-dot" id="dot-tab-design"></span></button>
          <button type="button" class="popup-tab" data-tab="variants">⊞ Variants</button>
        </div>

        <div id="popup-content">
          <!-- 1. Comment Tab -->
          <div class="tab-pane active" id="tab-comment">
            <div class="textarea-box">
              <textarea class="custom-textarea" id="comment-textarea" placeholder="Describe the change for your AI agent..."></textarea>
              <div class="textarea-bottom-bar">
                <button type="button" class="attach-btn" id="comment-attach-trigger">📎</button>
                <span class="shortcut-hint">Ctrl+Enter to save</span>
              </div>
              <div class="attach-popover" id="comment-attach-popover">
                <button type="button" class="popover-item" id="btn-attach-file">📎 Attach file</button>
                <button type="button" class="popover-item" id="btn-take-shot">📷 Take a screenshot</button>
                <button type="button" class="popover-close" id="btn-close-popover">✕</button>
              </div>
            </div>
          </div>

          <!-- 2. Design Tab -->
          <div class="tab-pane" id="tab-design">
            <!-- Accordion: Layout -->
            <div class="acc-item expanded" id="acc-layout">
              <div class="acc-header">
                <div class="acc-title-wrap">
                  <span class="acc-caret">▶</span>
                  <span>Layout</span>
                  <span class="change-dot" id="dot-acc-layout"></span>
                </div>
              </div>
              <div class="acc-body">
                <div class="icon-btn-row">
                  <button type="button" class="icon-btn active" data-layout="block" title="Block">▢</button>
                  <button type="button" class="icon-btn" data-layout="flex-col" title="Flex Column">昌</button>
                  <button type="button" class="icon-btn" data-layout="flex-row" title="Flex Row">00</button>
                  <button type="button" class="icon-btn" data-layout="grid" title="Grid">88</button>
                </div>
                
                <!-- Flex alignment & options (visible when flex-col or flex-row) -->
                <div id="flex-options-wrap" style="display:none;">
                  <div class="align-matrix-wrap">
                    <div class="matrix-grid" id="align-matrix">
                      <div class="matrix-cell" data-j="flex-start" data-a="flex-start"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="center" data-a="flex-start"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="flex-end" data-a="flex-start"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="flex-start" data-a="center"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell active" data-j="center" data-a="center"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="flex-end" data-a="center"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="flex-start" data-a="flex-end"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="center" data-a="flex-end"><div class="matrix-dot"></div></div>
                      <div class="matrix-cell" data-j="flex-end" data-a="flex-end"><div class="matrix-dot"></div></div>
                    </div>
                    <div class="matrix-options">
                      <label class="checkbox-label"><input type="checkbox" id="chk-reverse"> Reverse order</label>
                      <label class="checkbox-label"><input type="checkbox" id="chk-wrap"> Wrap items</label>
                      <label class="checkbox-label"><input type="checkbox" id="chk-space-auto"> Space auto</label>
                    </div>
                  </div>
                  <div class="input-row">
                    <span class="field-label">Gap space</span>
                    <div><input type="number" class="field-input" id="input-gap" value="0"> px</div>
                  </div>
                </div>

                <!-- Grid options (visible when grid) -->
                <div id="grid-options-wrap" style="display:none;">
                  <div class="input-row">
                    <span class="field-label">Cols</span>
                    <div><input type="number" class="field-input" id="input-grid-cols" value="1"> || <input type="number" class="field-input" id="input-col-gap" value="0"> px</div>
                  </div>
                  <div class="input-row">
                    <span class="field-label">Rows</span>
                    <div><input type="number" class="field-input" id="input-grid-rows" value="1"> = <input type="number" class="field-input" id="input-row-gap" value="0"> px</div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Accordion: Spacing -->
            <div class="acc-item" id="acc-spacing">
              <div class="acc-header">
                <div class="acc-title-wrap">
                  <span class="acc-caret">▶</span>
                  <span>Spacing</span>
                  <span class="change-dot" id="dot-acc-spacing"></span>
                </div>
                <button type="button" class="toggle-mode-btn" id="btn-toggle-spacing" title="Toggle 2-way / 4-way">⊞</button>
              </div>
              <div class="acc-body">
                <!-- Padding -->
                <div class="field-label" style="margin-bottom:4px;">Padding</div>
                <div id="padding-2way" class="input-row">
                  <div>↕ <input type="number" class="field-input" id="pad-v" value="0"> px</div>
                  <div>↔ <input type="number" class="field-input" id="pad-h" value="0"> px</div>
                </div>
                <div id="padding-4way" class="input-row" style="display:none;">
                  <div class="spacing-quad">
                    <div class="quad-item"><span class="quad-label">T</span><input type="number" class="quad-input" id="pad-t" value="0"></div>
                    <div class="quad-item"><span class="quad-label">R</span><input type="number" class="quad-input" id="pad-r" value="0"></div>
                    <div class="quad-item"><span class="quad-label">B</span><input type="number" class="quad-input" id="pad-b" value="0"></div>
                    <div class="quad-item"><span class="quad-label">L</span><input type="number" class="quad-input" id="pad-l" value="0"></div>
                  </div>
                </div>

                <!-- Margin -->
                <div class="field-label" style="margin: 8px 0 4px 0;">Margin</div>
                <div id="margin-2way" class="input-row">
                  <div>↕ <input type="number" class="field-input" id="mar-v" value="0"> px</div>
                  <div>↔ <input type="number" class="field-input" id="mar-h" value="0"> px</div>
                </div>
                <div id="margin-4way" class="input-row" style="display:none;">
                  <div class="spacing-quad">
                    <div class="quad-item"><span class="quad-label">T</span><input type="number" class="quad-input" id="mar-t" value="0"></div>
                    <div class="quad-item"><span class="quad-label">R</span><input type="number" class="quad-input" id="mar-r" value="0"></div>
                    <div class="quad-item"><span class="quad-label">B</span><input type="number" class="quad-input" id="mar-b" value="0"></div>
                    <div class="quad-item"><span class="quad-label">L</span><input type="number" class="quad-input" id="mar-l" value="0"></div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Accordion: Appearance -->
            <div class="acc-item" id="acc-appearance">
              <div class="acc-header">
                <div class="acc-title-wrap">
                  <span class="acc-caret">▶</span>
                  <span>Appearance</span>
                  <span class="change-dot" id="dot-acc-appearance"></span>
                </div>
              </div>
              <div class="acc-body" style="position:relative;">
                <div class="field-label" style="margin-bottom:4px;">Border</div>
                <div class="input-row">
                  <div>◳ <input type="number" class="field-input" id="border-radius" value="0" style="width:40px;"> px</div>
                  <div>— <input type="number" class="field-input" id="border-width" value="0" style="width:40px;"> px</div>
                  <div class="color-row">
                    <div class="color-swatch-btn" id="swatch-border" style="background:#e3e3e3;"></div>
                    <input type="text" class="field-input" id="border-color" value="#e3e3e3" style="width:68px;">
                  </div>
                </div>

                <div class="field-label" style="margin: 8px 0 4px 0;">Background</div>
                <div class="input-row">
                  <div class="color-row" style="width:100%;">
                    <div class="color-swatch-btn" id="swatch-bg" style="background:transparent;"></div>
                    <input type="text" class="field-input" id="bg-color" value="transparent" style="flex:1;">
                  </div>
                </div>

                <!-- Palette popover -->
                <div class="color-palette-popover" id="color-palette"></div>
              </div>
            </div>

            <!-- Accordion: Sizing -->
            <div class="acc-item" id="acc-sizing">
              <div class="acc-header">
                <div class="acc-title-wrap">
                  <span class="acc-caret">▶</span>
                  <span>Sizing</span>
                  <span class="change-dot" id="dot-acc-sizing"></span>
                </div>
              </div>
              <div class="acc-body">
                <div class="field-label" style="margin-bottom:4px;">Width</div>
                <div class="input-row">
                  <div>W <input type="text" class="field-input" id="size-w" value="auto" style="width:64px;"></div>
                  <div>Min <input type="text" class="field-input" id="size-min-w" value="—" style="width:50px;"></div>
                  <div>Max <input type="text" class="field-input" id="size-max-w" value="—" style="width:50px;"></div>
                </div>

                <div class="field-label" style="margin: 8px 0 4px 0;">Height</div>
                <div class="input-row">
                  <div>H <input type="text" class="field-input" id="size-h" value="auto" style="width:64px;"></div>
                  <div>Min <input type="text" class="field-input" id="size-min-h" value="—" style="width:50px;"></div>
                  <div>Max <input type="text" class="field-input" id="size-max-h" value="—" style="width:50px;"></div>
                </div>
              </div>
            </div>

            <!-- Accordion: CSS -->
            <div class="acc-item" id="acc-css">
              <div class="acc-header">
                <div class="acc-title-wrap">
                  <span class="acc-caret">▶</span>
                  <span>CSS</span>
                  <span class="change-dot" id="dot-acc-css"></span>
                </div>
              </div>
              <div class="acc-body">
                <div class="field-label" style="margin-bottom:4px;">Inline overrides</div>
                <textarea class="code-editor" id="css-inline-editor"></textarea>

                <div class="field-label" style="margin: 8px 0 4px 0;">CSS rules :hover, ::before, @media...</div>
                <textarea class="code-editor" id="css-rules-editor" placeholder=".element:hover {\n  opacity: 0.8;\n}"></textarea>
              </div>
            </div>
          </div>

          <!-- 3. Variants Tab -->
          <div class="tab-pane" id="tab-variants">
            <div class="textarea-box">
              <textarea class="custom-textarea" id="variants-textarea" placeholder="What variants do you want?"></textarea>
              <div class="textarea-bottom-bar">
                <button type="button" class="attach-btn" id="variants-attach-trigger">➕</button>
                <span class="shortcut-hint">Ctrl+Enter to save</span>
              </div>
            </div>
            <div class="variants-hint">Your agent builds these in your codebase, you'll preview and pick your favorite right here.</div>
          </div>
        </div>

        <div id="popup-footer">
          <div id="viewport-indicator">🖵 <span id="vp-width">1920</span>w</div>
          <div class="footer-actions">
            <button type="button" id="popup-cancel-btn">Cancel</button>
            <button type="button" id="popup-action-btn">Save as pointer</button>
          </div>
        </div>

        <!-- Resize Handles (min: 320px width, 340px height) -->
        <div id="popup-resize-r"></div>
        <div id="popup-resize-b"></div>
        <div id="popup-resize-handle" title="Drag to resize (min: 320x340)">
          <svg width="10" height="10" viewBox="0 0 10 10" fill="none">
            <path d="M8.5 1.5L1.5 8.5M8.5 5.5L5.5 8.5M8.5 8.5L8.5 8.5" stroke="#718298" stroke-width="1.3" stroke-linecap="round"/>
          </svg>
        </div>
      </div>
      
      <div id="pins-container"></div>
    `;
    shadow.appendChild(container);

    const cornerBtn = shadow.getElementById('corner-btn');
    const shield = shadow.getElementById('crosshair-shield');
    const hoverBox = shadow.getElementById('hover-highlight');
    const hoverBadge = shadow.getElementById('hover-badge');
    const popup = shadow.getElementById('popup-dialog');
    const popupDragHandle = shadow.getElementById('popup-drag-handle');
    const popupHeader = shadow.getElementById('popup-header');
    const popupTargetTag = shadow.getElementById('popup-target-tag');
    const popupRevertBtn = shadow.getElementById('popup-revert-btn');
    const popupCancelBtn = shadow.getElementById('popup-cancel-btn');
    const popupActionBtn = shadow.getElementById('popup-action-btn');
    const commentTextarea = shadow.getElementById('comment-textarea');
    const variantsTextarea = shadow.getElementById('variants-textarea');
    const vpWidth = shadow.getElementById('vp-width');
    const pinsContainer = shadow.getElementById('pins-container');

    // Attach menu
    const attachTrigger = shadow.getElementById('comment-attach-trigger');
    const attachPopover = shadow.getElementById('comment-attach-popover');
    const popoverClose = shadow.getElementById('btn-close-popover');

    // Palette popover
    const colorPalette = shadow.getElementById('color-palette');
    const PALETTE_COLORS = [
      '#f97316', '#f59e0b', '#261b11', '#ef4444',
      '#64748b', '#0f172a', '#1e293b', '#e2e8f0',
      '#334155', '#475569', '#8b5cf6', '#10b981',
      '#a855f7', '#06b6d4', '#ffffff', 'transparent'
    ];
    let activeColorTarget = null; // 'border' or 'bg'

    PALETTE_COLORS.forEach(color => {
      const sw = document.createElement('div');
      sw.className = 'palette-swatch';
      sw.style.background = color === 'transparent' ? 'repeating-conic-gradient(#333 0% 25%, #222 0% 50%) 50% / 8px 8px' : color;
      sw.title = color;
      sw.addEventListener('click', () => {
        if (activeColorTarget === 'border') {
          shadow.getElementById('border-color').value = color;
          shadow.getElementById('swatch-border').style.background = color;
          applyStyle('borderColor', color);
        } else if (activeColorTarget === 'bg') {
          shadow.getElementById('bg-color').value = color;
          shadow.getElementById('swatch-bg').style.background = color;
          applyStyle('backgroundColor', color);
        }
        colorPalette.classList.remove('active');
      });
      colorPalette.appendChild(sw);
    });

    shadow.getElementById('swatch-border').addEventListener('click', (e) => {
      e.stopPropagation();
      activeColorTarget = 'border';
      colorPalette.classList.toggle('active');
    });

    shadow.getElementById('swatch-bg').addEventListener('click', (e) => {
      e.stopPropagation();
      activeColorTarget = 'bg';
      colorPalette.classList.toggle('active');
    });

    // Close popovers on click outside
    popup.addEventListener('click', (e) => {
      if (!e.target.closest('#swatch-border') && !e.target.closest('#swatch-bg') && !e.target.closest('#color-palette')) {
        colorPalette.classList.remove('active');
      }
      if (!e.target.closest('#comment-attach-trigger') && !e.target.closest('#comment-attach-popover')) {
        attachPopover.classList.remove('active');
      }
    });

    attachTrigger.addEventListener('click', (e) => {
      e.stopPropagation();
      attachPopover.classList.toggle('active');
    });
    popoverClose.addEventListener('click', () => attachPopover.classList.remove('active'));

    // --- Tab Switching ---
    shadow.querySelectorAll('.popup-tab').forEach(tabBtn => {
      tabBtn.addEventListener('click', () => {
        shadow.querySelectorAll('.popup-tab').forEach(b => b.classList.remove('active'));
        tabBtn.classList.add('active');
        activeTab = tabBtn.dataset.tab;

        shadow.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
        shadow.getElementById('tab-' + activeTab).classList.add('active');
        updateActionButtonText();
      });
    });

    // --- Accordion Toggles ---
    shadow.querySelectorAll('.acc-header').forEach(header => {
      header.addEventListener('click', (e) => {
        if (e.target.closest('.toggle-mode-btn')) return;
        const item = header.closest('.acc-item');
        item.classList.toggle('expanded');
      });
    });

    // --- Layout Flow Switcher ---
    shadow.querySelectorAll('[data-layout]').forEach(btn => {
      btn.addEventListener('click', () => {
        shadow.querySelectorAll('[data-layout]').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentLayout = btn.dataset.layout;

        const flexOptions = shadow.getElementById('flex-options-wrap');
        const gridOptions = shadow.getElementById('grid-options-wrap');

        if (currentLayout === 'flex-col') {
          flexOptions.style.display = 'block';
          gridOptions.style.display = 'none';
          applyStyle('display', 'flex');
          applyStyle('flexDirection', 'column');
        } else if (currentLayout === 'flex-row') {
          flexOptions.style.display = 'block';
          gridOptions.style.display = 'none';
          applyStyle('display', 'flex');
          applyStyle('flexDirection', 'row');
        } else if (currentLayout === 'grid') {
          flexOptions.style.display = 'none';
          gridOptions.style.display = 'block';
          applyStyle('display', 'grid');
        } else {
          flexOptions.style.display = 'none';
          gridOptions.style.display = 'none';
          applyStyle('display', 'block');
        }
      });
    });

    // 3x3 Matrix
    shadow.querySelectorAll('.matrix-cell').forEach(cell => {
      cell.addEventListener('click', () => {
        shadow.querySelectorAll('.matrix-cell').forEach(c => c.classList.remove('active'));
        cell.classList.add('active');
        applyStyle('justifyContent', cell.dataset.j);
        applyStyle('alignItems', cell.dataset.a);
      });
    });

    shadow.getElementById('chk-reverse').addEventListener('change', (e) => {
      if (currentLayout === 'flex-col') {
        applyStyle('flexDirection', e.target.checked ? 'column-reverse' : 'column');
      } else if (currentLayout === 'flex-row') {
        applyStyle('flexDirection', e.target.checked ? 'row-reverse' : 'row');
      }
    });

    shadow.getElementById('chk-wrap').addEventListener('change', (e) => {
      applyStyle('flexWrap', e.target.checked ? 'wrap' : 'nowrap');
    });

    shadow.getElementById('chk-space-auto').addEventListener('change', (e) => {
      applyStyle('justifyContent', e.target.checked ? 'space-between' : 'center');
    });

    shadow.getElementById('input-gap').addEventListener('input', (e) => {
      applyStyle('gap', e.target.value + 'px');
    });

    // Grid inputs
    shadow.getElementById('input-grid-cols').addEventListener('input', (e) => {
      applyStyle('gridTemplateColumns', `repeat(${e.target.value}, 1fr)`);
    });
    shadow.getElementById('input-grid-rows').addEventListener('input', (e) => {
      applyStyle('gridTemplateRows', `repeat(${e.target.value}, 1fr)`);
    });
    shadow.getElementById('input-col-gap').addEventListener('input', (e) => {
      applyStyle('columnGap', e.target.value + 'px');
    });
    shadow.getElementById('input-row-gap').addEventListener('input', (e) => {
      applyStyle('rowGap', e.target.value + 'px');
    });

    // --- Spacing 2-way vs 4-way Toggle ---
    const btnToggleSpacing = shadow.getElementById('btn-toggle-spacing');
    btnToggleSpacing.addEventListener('click', () => {
      if (spacingMode === '2way') {
        spacingMode = '4way';
        btnToggleSpacing.textContent = '□';
        shadow.getElementById('padding-2way').style.display = 'none';
        shadow.getElementById('padding-4way').style.display = 'flex';
        shadow.getElementById('margin-2way').style.display = 'none';
        shadow.getElementById('margin-4way').style.display = 'flex';
      } else {
        spacingMode = '2way';
        btnToggleSpacing.textContent = '⊞';
        shadow.getElementById('padding-2way').style.display = 'flex';
        shadow.getElementById('padding-4way').style.display = 'none';
        shadow.getElementById('margin-2way').style.display = 'flex';
        shadow.getElementById('margin-4way').style.display = 'none';
      }
    });

    // Padding & Margin inputs
    shadow.getElementById('pad-v').addEventListener('input', (e) => {
      applyStyle('paddingTop', e.target.value + 'px');
      applyStyle('paddingBottom', e.target.value + 'px');
    });
    shadow.getElementById('pad-h').addEventListener('input', (e) => {
      applyStyle('paddingLeft', e.target.value + 'px');
      applyStyle('paddingRight', e.target.value + 'px');
    });
    shadow.getElementById('pad-t').addEventListener('input', (e) => applyStyle('paddingTop', e.target.value + 'px'));
    shadow.getElementById('pad-r').addEventListener('input', (e) => applyStyle('paddingRight', e.target.value + 'px'));
    shadow.getElementById('pad-b').addEventListener('input', (e) => applyStyle('paddingBottom', e.target.value + 'px'));
    shadow.getElementById('pad-l').addEventListener('input', (e) => applyStyle('paddingLeft', e.target.value + 'px'));

    shadow.getElementById('mar-v').addEventListener('input', (e) => {
      applyStyle('marginTop', e.target.value + 'px');
      applyStyle('marginBottom', e.target.value + 'px');
    });
    shadow.getElementById('mar-h').addEventListener('input', (e) => {
      applyStyle('marginLeft', e.target.value + 'px');
      applyStyle('marginRight', e.target.value + 'px');
    });
    shadow.getElementById('mar-t').addEventListener('input', (e) => applyStyle('marginTop', e.target.value + 'px'));
    shadow.getElementById('mar-r').addEventListener('input', (e) => applyStyle('marginRight', e.target.value + 'px'));
    shadow.getElementById('mar-b').addEventListener('input', (e) => applyStyle('marginBottom', e.target.value + 'px'));
    shadow.getElementById('mar-l').addEventListener('input', (e) => applyStyle('marginLeft', e.target.value + 'px'));

    // Appearance inputs
    shadow.getElementById('border-radius').addEventListener('input', (e) => applyStyle('borderRadius', e.target.value + 'px'));
    shadow.getElementById('border-width').addEventListener('input', (e) => {
      applyStyle('borderWidth', e.target.value + 'px');
      applyStyle('borderStyle', 'solid');
    });
    shadow.getElementById('border-color').addEventListener('input', (e) => {
      shadow.getElementById('swatch-border').style.background = e.target.value;
      applyStyle('borderColor', e.target.value);
    });
    shadow.getElementById('bg-color').addEventListener('input', (e) => {
      shadow.getElementById('swatch-bg').style.background = e.target.value;
      applyStyle('backgroundColor', e.target.value);
    });

    // Sizing inputs
    shadow.getElementById('size-w').addEventListener('input', (e) => applyStyle('width', e.target.value));
    shadow.getElementById('size-min-w').addEventListener('input', (e) => applyStyle('minWidth', e.target.value === '—' ? '' : e.target.value));
    shadow.getElementById('size-max-w').addEventListener('input', (e) => applyStyle('maxWidth', e.target.value === '—' ? '' : e.target.value));
    shadow.getElementById('size-h').addEventListener('input', (e) => applyStyle('height', e.target.value));
    shadow.getElementById('size-min-h').addEventListener('input', (e) => applyStyle('minHeight', e.target.value === '—' ? '' : e.target.value));
    shadow.getElementById('size-max-h').addEventListener('input', (e) => applyStyle('maxHeight', e.target.value === '—' ? '' : e.target.value));

    // Inline CSS Code Editor
    const inlineCssEditor = shadow.getElementById('css-inline-editor');
    inlineCssEditor.addEventListener('input', () => {
      if (!selectedElement) return;
      try {
        selectedElement.style.cssText = inlineCssEditor.value;
        pendingChanges['inlineCss'] = { original: originalInlineCss, new: inlineCssEditor.value };
        updateChangeDots();
        updateActionButtonText();
      } catch (e) {}
    });

    // --- Movable Dragging Implementation ---
    let isDragging = false;
    let dragStartX = 0;
    let dragStartY = 0;
    let popupInitialLeft = 0;
    let popupInitialTop = 0;

    function startDrag(e) {
      if (e.target.closest('#popup-revert-btn')) return;
      isDragging = true;
      dragStartX = e.clientX;
      dragStartY = e.clientY;
      const rect = popup.getBoundingClientRect();
      popupInitialLeft = rect.left;
      popupInitialTop = rect.top;
      window.addEventListener('pointermove', onDragMove, true);
      window.addEventListener('pointerup', endDrag, true);
    }

    function onDragMove(e) {
      if (!isDragging) return;
      const dx = e.clientX - dragStartX;
      const dy = e.clientY - dragStartY;
      let newLeft = popupInitialLeft + dx;
      let newTop = popupInitialTop + dy;

      const pad = 10;
      const maxLeft = window.innerWidth - popup.offsetWidth - pad;
      const maxTop = window.innerHeight - popup.offsetHeight - pad;
      newLeft = Math.max(pad, Math.min(maxLeft, newLeft));
      newTop = Math.max(pad, Math.min(maxTop, newTop));

      popup.style.left = newLeft + 'px';
      popup.style.top = newTop + 'px';
    }

    function endDrag() {
      isDragging = false;
      window.removeEventListener('pointermove', onDragMove, true);
      window.removeEventListener('pointerup', endDrag, true);
    }

    popupDragHandle.addEventListener('pointerdown', startDrag);
    popupHeader.addEventListener('pointerdown', startDrag);

    // --- Resizing Implementation (min: 320px width, 340px height) ---
    let isResizing = false;
    let resizeStartX = 0;
    let resizeStartY = 0;
    let popupInitialW = 0;
    let popupInitialH = 0;
    let resizeDir = 'both';

    function startResize(e, dir) {
      e.preventDefault();
      e.stopPropagation();
      isResizing = true;
      resizeDir = dir || 'both';
      resizeStartX = e.clientX;
      resizeStartY = e.clientY;
      const rect = popup.getBoundingClientRect();
      popupInitialW = rect.width;
      popupInitialH = rect.height;
      window.addEventListener('pointermove', onResizeMove, true);
      window.addEventListener('pointerup', endResize, true);
    }

    function onResizeMove(e) {
      if (!isResizing) return;
      e.preventDefault();
      e.stopPropagation();
      const dx = e.clientX - resizeStartX;
      const dy = e.clientY - resizeStartY;

      const minW = 320;
      const minH = 340;
      const pad = 10;
      const maxW = window.innerWidth - popup.offsetLeft - pad;
      const maxH = window.innerHeight - popup.offsetTop - pad;

      if (resizeDir === 'both' || resizeDir === 'x') {
        const newW = Math.max(minW, Math.min(maxW, popupInitialW + dx));
        popup.style.width = newW + 'px';
      }
      if (resizeDir === 'both' || resizeDir === 'y') {
        const newH = Math.max(minH, Math.min(maxH, popupInitialH + dy));
        popup.style.height = newH + 'px';
        popup.style.maxHeight = 'none';
      }
    }

    function endResize(e) {
      if (isResizing) {
        isResizing = false;
        window.removeEventListener('pointermove', onResizeMove, true);
        window.removeEventListener('pointerup', endResize, true);
      }
    }

    const resizeHandle = shadow.getElementById('popup-resize-handle');
    const resizeR = shadow.getElementById('popup-resize-r');
    const resizeB = shadow.getElementById('popup-resize-b');

    if (resizeHandle) {
      resizeHandle.addEventListener('pointerdown', (e) => startResize(e, 'both'));
      resizeHandle.addEventListener('mousedown', (e) => startResize(e, 'both'));
    }
    if (resizeR) {
      resizeR.addEventListener('pointerdown', (e) => startResize(e, 'x'));
      resizeR.addEventListener('mousedown', (e) => startResize(e, 'x'));
    }
    if (resizeB) {
      resizeB.addEventListener('pointerdown', (e) => startResize(e, 'y'));
      resizeB.addEventListener('mousedown', (e) => startResize(e, 'y'));
    }

    // --- Live Style Changes Tracking ---
    function applyStyle(prop, val) {
      if (!selectedElement) return;
      const orig = originalStyles[prop] || '';
      if (!pendingChanges[prop]) {
        pendingChanges[prop] = { original: orig, new: val };
      } else {
        pendingChanges[prop].new = val;
      }
      try {
        selectedElement.style[prop] = val;
      } catch (e) {}
      updateInlineCssEditor();
      updateChangeDots();
      updateActionButtonText();
    }

    function updateInlineCssEditor() {
      if (!selectedElement) return;
      inlineCssEditor.value = selectedElement.style.cssText;
    }

    function updateChangeDots() {
      const hasChanges = Object.keys(pendingChanges).length > 0;
      shadow.getElementById('dot-tab-design').classList.toggle('active', hasChanges);
      shadow.getElementById('dot-acc-layout').classList.toggle('active', !!(pendingChanges.display || pendingChanges.flexDirection || pendingChanges.gap || pendingChanges.justifyContent));
      shadow.getElementById('dot-acc-spacing').classList.toggle('active', !!(pendingChanges.paddingTop || pendingChanges.paddingLeft || pendingChanges.marginTop || pendingChanges.marginLeft));
      shadow.getElementById('dot-acc-appearance').classList.toggle('active', !!(pendingChanges.borderRadius || pendingChanges.borderWidth || pendingChanges.borderColor || pendingChanges.backgroundColor));
      shadow.getElementById('dot-acc-sizing').classList.toggle('active', !!(pendingChanges.width || pendingChanges.height || pendingChanges.minWidth || pendingChanges.maxWidth));
      shadow.getElementById('dot-acc-css').classList.toggle('active', !!pendingChanges.inlineCss);
    }

    function updateActionButtonText() {
      if (activeTab === 'variants') {
        popupActionBtn.textContent = 'Create variants';
      } else if (activeTab === 'design' || Object.keys(pendingChanges).length > 0 || commentTextarea.value.trim().length > 0) {
        popupActionBtn.textContent = 'Save annotation';
      } else {
        popupActionBtn.textContent = 'Save as pointer';
      }
    }

    commentTextarea.addEventListener('input', updateActionButtonText);

    // Revert Button
    popupRevertBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      revertAllChanges();
    });

    function revertAllChanges() {
      if (!selectedElement) return;
      selectedElement.style.cssText = originalInlineCss;
      pendingChanges = {};
      populateElementValues(selectedElement);
      updateChangeDots();
      updateInlineCssEditor();
      updateActionButtonText();
    }

    // --- Selectors & React Helper Functions ---
    function getStableAttributes(el) {
      if (!el || el.nodeType !== 1) return null;
      if (el.id && !/^[0-9a-f-]{8,}$/i.test(el.id) && !/\d{5,}/.test(el.id)) {
        return '#' + CSS.escape(el.id);
      }
      for (const attr of ['data-testid', 'data-component', 'data-panel', 'aria-label', 'role']) {
        const val = el.getAttribute(attr);
        if (val) return `[${attr}="${CSS.escape(val)}"]`;
      }
      return null;
    }

    function getReadableClassSelector(el) {
      if (!el || !el.className || typeof el.className !== 'string') return null;
      const classes = el.className.trim().split(/\s+/).filter(Boolean);
      for (const cls of classes) {
        if (/^(btn|game|store|nav|header|footer|panel|card|item|content|tab|view|modal|menu|dialog|list)_/i.test(cls)) {
          return `[class*="${cls.split('_')[0]}_"]`;
        }
        if (/^[a-zA-Z][a-zA-Z0-9-]{3,24}$/.test(cls) && !/^[0-9a-f]{8,}$/i.test(cls) && !/^[A-Z0-9]{8,}$/.test(cls)) {
          return '.' + CSS.escape(cls);
        }
      }
      return null;
    }

    function getNthOfTypePath(el) {
      if (!el || el.nodeType !== 1 || el === document.body) return '';
      let path = '';
      let cur = el;
      while (cur && cur.nodeType === 1 && cur !== document.body && cur !== document.documentElement) {
        const tag = cur.tagName.toLowerCase();
        let idx = 1;
        let sib = cur.previousElementSibling;
        while (sib) {
          if (sib.tagName.toLowerCase() === tag) idx++;
          sib = sib.previousElementSibling;
        }
        const step = `${tag}:nth-of-type(${idx})`;
        path = path ? `${step} > ${path}` : step;
        cur = cur.parentElement;
      }
      return path ? `body > ${path}` : '';
    }

    function getXPath(el) {
      if (!el || el.nodeType !== 1) return '';
      if (el.id && !/\d{4,}/.test(el.id)) {
        return `//*[@id="${el.id}"]`;
      }
      const parts = [];
      while (el && el.nodeType === 1) {
        let count = 1;
        let sibling = el.previousSibling;
        while (sibling) {
          if (sibling.nodeType === 1 && sibling.tagName === el.tagName) count++;
          sibling = sibling.previousSibling;
        }
        const tag = el.tagName.toLowerCase();
        parts.unshift(count > 1 ? `${tag}[${count}]` : tag);
        el = el.parentNode;
      }
      return '/' + parts.join('/');
    }

    function getAncestorChain(el, depth = 3) {
      const chain = [];
      let cur = el ? el.parentElement : null;
      while (cur && chain.length < depth && cur !== document.body && cur !== document.documentElement) {
        chain.push({
          tag: cur.tagName.toLowerCase(),
          id: cur.id || null,
          className: typeof cur.className === 'string' ? cur.className.slice(0, 100) : '',
          textSnippet: cur.innerText ? cur.innerText.trim().slice(0, 40) : ''
        });
        cur = cur.parentElement;
      }
      return chain;
    }

    function getReactInfo(el) {
      if (WORLD_MODE !== 'main' || !el) return null;
      try {
        const key = Object.keys(el).find(k => k.startsWith('__reactFiber$') || k.startsWith('__reactInternalInstance$'));
        if (!key) return null;
        let fiber = el[key];
        while (fiber) {
          const type = fiber.type;
          if (type && typeof type === 'function') {
            const name = type.displayName || type.name;
            if (name && !/^[a-z]+$/.test(name)) return name;
          } else if (type && typeof type === 'object') {
            const name = type.displayName || type.name || (type.render && (type.render.displayName || type.render.name));
            if (name && !/^[a-z]+$/.test(name)) return name;
          }
          fiber = fiber.return;
        }
      } catch (e) {}
      return null;
    }

    function getElementContext(el) {
      if (!el) return null;
      const rect = el.getBoundingClientRect();
      const style = window.getComputedStyle(el);

      const stable = getStableAttributes(el);
      const readableClass = getReadableClassSelector(el);
      const nthPath = getNthOfTypePath(el);
      const xpath = getXPath(el);
      const ancestors = getAncestorChain(el, 3);
      const reactName = getReactInfo(el);

      const prioritySelectors = [stable, readableClass, nthPath].filter(Boolean);

      return {
        tag: el.tagName.toLowerCase(),
        text: el.innerText ? el.innerText.trim().slice(0, 80) : '',
        outerHTML: el.outerHTML ? el.outerHTML.slice(0, 1500) : '',
        selectors: {
          priority: prioritySelectors,
          stableIdOrAttr: stable,
          readableClass: readableClass,
          nthOfTypePath: nthPath,
          xpath: xpath,
          ancestorChain: ancestors
        },
        react: {
          componentName: reactName
        },
        rect: {
          x: Math.round(rect.x),
          y: Math.round(rect.y),
          width: Math.round(rect.width),
          height: Math.round(rect.height),
          top: Math.round(rect.top),
          left: Math.round(rect.left),
          right: Math.round(rect.right),
          bottom: Math.round(rect.bottom)
        },
        viewport: {
          width: window.innerWidth,
          height: window.innerHeight
        },
        devicePixelRatio: window.devicePixelRatio || 1,
        styles: {
          color: style.color,
          backgroundColor: style.backgroundColor,
          fontFamily: style.fontFamily,
          fontSize: style.fontSize,
          display: style.display,
          position: style.position,
          zIndex: style.zIndex
        },
        pageContext: {
          url: window.location.href,
          title: document.title,
          targetId: TARGET_ID,
          worldMode: WORLD_MODE,
          routeHint: currentRouteHint
        }
      };
    }

    // Populate inspector values from element
    function populateElementValues(el) {
      try {
        const comp = window.getComputedStyle(el);
        originalStyles = {};
        for (let i = 0; i < comp.length; i++) {
          const p = comp[i];
          originalStyles[p] = comp.getPropertyValue(p);
        }
        originalInlineCss = el.style.cssText;
        pendingChanges = {};

        // Tag name in header
        if (popupTargetTag) popupTargetTag.textContent = (el.tagName || 'element').toLowerCase();

        // Viewport width
        if (vpWidth) vpWidth.textContent = window.innerWidth;

        // Layout
        const disp = comp.display || 'block';
        shadow.querySelectorAll('[data-layout]').forEach(b => b.classList.remove('active'));
        const flexOptions = shadow.getElementById('flex-options-wrap');
        const gridOptions = shadow.getElementById('grid-options-wrap');

        if (disp === 'flex' || disp === 'inline-flex') {
          const isCol = (comp.flexDirection || '').includes('column');
          currentLayout = isCol ? 'flex-col' : 'flex-row';
          shadow.querySelector(`[data-layout="${currentLayout}"]`)?.classList.add('active');
          if (flexOptions) flexOptions.style.display = 'block';
          if (gridOptions) gridOptions.style.display = 'none';
        } else if (disp === 'grid' || disp === 'inline-grid') {
          currentLayout = 'grid';
          shadow.querySelector('[data-layout="grid"]')?.classList.add('active');
          if (flexOptions) flexOptions.style.display = 'none';
          if (gridOptions) gridOptions.style.display = 'block';
        } else {
          currentLayout = 'block';
          shadow.querySelector('[data-layout="block"]')?.classList.add('active');
          if (flexOptions) flexOptions.style.display = 'none';
          if (gridOptions) gridOptions.style.display = 'none';
        }

        // Spacing
        const padTop = parseInt(comp.paddingTop) || 0;
        const padRight = parseInt(comp.paddingRight) || 0;
        const padBottom = parseInt(comp.paddingBottom) || 0;
        const padLeft = parseInt(comp.paddingLeft) || 0;
        const padV = shadow.getElementById('pad-v'); if (padV) padV.value = padTop;
        const padH = shadow.getElementById('pad-h'); if (padH) padH.value = padLeft;
        const padT = shadow.getElementById('pad-t'); if (padT) padT.value = padTop;
        const padR = shadow.getElementById('pad-r'); if (padR) padR.value = padRight;
        const padB = shadow.getElementById('pad-b'); if (padB) padB.value = padBottom;
        const padL = shadow.getElementById('pad-l'); if (padL) padL.value = padLeft;

        const marTop = parseInt(comp.marginTop) || 0;
        const marRight = parseInt(comp.marginRight) || 0;
        const marBottom = parseInt(comp.marginBottom) || 0;
        const marLeft = parseInt(comp.marginLeft) || 0;
        const marV = shadow.getElementById('mar-v'); if (marV) marV.value = marTop;
        const marH = shadow.getElementById('mar-h'); if (marH) marH.value = marLeft;
        const marT = shadow.getElementById('mar-t'); if (marT) marT.value = marTop;
        const marR = shadow.getElementById('mar-r'); if (marR) marR.value = marRight;
        const marB = shadow.getElementById('mar-b'); if (marB) marB.value = marBottom;
        const marL = shadow.getElementById('mar-l'); if (marL) marL.value = marLeft;

        // Appearance
        const bRad = shadow.getElementById('border-radius'); if (bRad) bRad.value = parseInt(comp.borderRadius) || 0;
        const bWidth = shadow.getElementById('border-width'); if (bWidth) bWidth.value = parseInt(comp.borderWidth) || 0;
        const bCol = shadow.getElementById('border-color'); if (bCol) bCol.value = comp.borderColor || '#e3e3e3';
        const sBorder = shadow.getElementById('swatch-border'); if (sBorder) sBorder.style.background = comp.borderColor || '#e3e3e3';

        const bgCol = shadow.getElementById('bg-color'); if (bgCol) bgCol.value = comp.backgroundColor || 'transparent';
        const sBg = shadow.getElementById('swatch-bg'); if (sBg) sBg.style.background = comp.backgroundColor || 'transparent';

        // Sizing
        const sW = shadow.getElementById('size-w'); if (sW) sW.value = comp.width || '';
        const sMinW = shadow.getElementById('size-min-w'); if (sMinW) sMinW.value = comp.minWidth === '0px' ? '—' : (comp.minWidth || '—');
        const sMaxW = shadow.getElementById('size-max-w'); if (sMaxW) sMaxW.value = comp.maxWidth === 'none' ? '—' : (comp.maxWidth || '—');
        const sH = shadow.getElementById('size-h'); if (sH) sH.value = comp.height || '';
        const sMinH = shadow.getElementById('size-min-h'); if (sMinH) sMinH.value = comp.minHeight === '0px' ? '—' : (comp.minHeight || '—');
        const sMaxH = shadow.getElementById('size-max-h'); if (sMaxH) sMaxH.value = comp.maxHeight === 'none' ? '—' : (comp.maxHeight || '—');

        // CSS editor
        if (inlineCssEditor) inlineCssEditor.value = el.style.cssText;
        updateChangeDots();
        updateActionButtonText();
      } catch (err) {
        console.warn('[CEF Annotator] Error populating element values:', err);
      }
    }

    // --- Mode Management ---
    function setAnnotateMode(active) {
      isAnnotateMode = active;
      if (active) {
        cornerBtn.classList.add('active');
        shield.style.display = 'block';
        hoverBox.style.display = 'none';
        try { document.documentElement.style.cursor = 'crosshair'; } catch (e) {}
        attachCaptureListeners();
      } else {
        cornerBtn.classList.remove('active');
        shield.style.display = 'none';
        hoverBox.style.display = 'none';
        popup.style.display = 'none';
        hoveredElement = null;
        selectedElement = null;
        try { document.documentElement.style.cursor = ''; } catch (e) {}
        removeCaptureListeners();
      }
    }

    // --- Hover Highlight & DOM Fine-Tuning Helpers ---
    function updateHoverBoxForElement(el) {
      if (!el || el === host || host.contains(el)) return;
      hoveredElement = el;
      const rect = el.getBoundingClientRect();
      hoverBox.style.top = rect.top + 'px';
      hoverBox.style.left = rect.left + 'px';
      hoverBox.style.width = rect.width + 'px';
      hoverBox.style.height = rect.height + 'px';
      hoverBox.style.display = 'block';

      const tag = el.tagName.toLowerCase();
      const cls = typeof el.className === 'string' && el.className ? '.' + el.className.trim().split(/\s+/)[0] : '';
      const txt = el.innerText ? ` "${el.innerText.trim().slice(0, 20)}"` : '';
      hoverBadge.textContent = `<${tag}${cls}>${txt}`;
    }

    function fineTuneUp() {
      if (!hoveredElement) return;
      let parent = hoveredElement.parentElement;
      while (parent && (parent === host || host.contains(parent) || parent === document.documentElement)) {
        if (parent === document.documentElement) break;
        parent = parent.parentElement;
      }
      if (parent && parent !== document.documentElement && parent !== document.body.parentElement) {
        updateHoverBoxForElement(parent);
      }
    }

    function fineTuneDown() {
      if (!hoveredElement) return;
      let child = hoveredElement.firstElementChild;
      while (child && (child === host || host.contains(child))) {
        child = child.nextElementSibling;
      }
      if (child) {
        updateHoverBoxForElement(child);
      }
    }

    function deleteAllAnnotations() {
      // 1. Instantly clear pins from the DOM
      activePins = [];
      renderPins();

      // 2. Transmit delete_all payload over CDP binding
      const payload = {
        action: 'delete_all',
        url: window.location.href,
        route_hint: currentRouteHint,
        created_at: new Date().toISOString()
      };

      try {
        if (typeof window[BINDING_NAME] === 'function') {
          window[BINDING_NAME](JSON.stringify(payload));
        } else {
          console.error('[CEF Annotator] Binding function not found:', BINDING_NAME);
        }
      } catch (err) {
        console.error('[CEF Annotator] Failed calling delete binding:', err);
      }

      // 3. Visual feedback on the Del key badge
      const delBadge = shadow.getElementById('key-badge-del');
      if (delBadge) {
        const origText = delBadge.textContent;
        const origBg = delBadge.style.background;
        const origColor = delBadge.style.color;
        delBadge.textContent = '✓';
        delBadge.style.background = '#e11d48';
        delBadge.style.borderColor = '#fb7185';
        delBadge.style.color = '#ffffff';
        setTimeout(() => {
          delBadge.textContent = origText;
          delBadge.style.background = origBg;
          delBadge.style.borderColor = '';
          delBadge.style.color = origColor;
        }, 1200);
      }
    }

    // --- Capture-Phase Event Suppression (Annotate mode only) ---
    function onPointerMove(e) {
      if (!isAnnotateMode || popup.style.display === 'flex' || isToolbarDragging) return;

      let el = e.target;
      if (!el || el === host || host.contains(el)) {
        el = document.elementFromPoint(e.clientX, e.clientY);
      }

      if (!el || el === host || host.contains(el)) {
        hoverBox.style.display = 'none';
        hoveredElement = null;
        return;
      }

      updateHoverBoxForElement(el);
    }

    function onCaptureEvent(e) {
      if (!isAnnotateMode) return;

      // Allow interactions inside our host's shadow root (corner button, popup dialog, pins)
      if (e.target === host || (e.target && host.contains(e.target))) {
        return;
      }

      // If clicking outside while popup is open, close popup
      if (popup.style.display === 'flex') {
        e.preventDefault();
        e.stopPropagation();
        closePopup();
        return;
      }

      // Suppress Steam UI reaction
      e.preventDefault();
      e.stopPropagation();

      if (e.type === 'click') {
        const el = (hoveredElement && hoveredElement !== host && !host.contains(hoveredElement))
          ? hoveredElement
          : ((e.target && e.target !== host && !host.contains(e.target)) ? e.target : document.elementFromPoint(e.clientX, e.clientY));
        if (el && el !== host && !host.contains(el)) {
          openPopup(el);
        }
      }
    }

    function attachCaptureListeners() {
      window.addEventListener('pointermove', onPointerMove, true);
      window.addEventListener('mousemove', onPointerMove, true);
      window.addEventListener('pointerdown', onCaptureEvent, true);
      window.addEventListener('mousedown', onCaptureEvent, true);
      window.addEventListener('mouseup', onCaptureEvent, true);
      window.addEventListener('click', onCaptureEvent, true);
    }

    function removeCaptureListeners() {
      window.removeEventListener('pointermove', onPointerMove, true);
      window.removeEventListener('mousemove', onPointerMove, true);
      window.removeEventListener('pointerdown', onCaptureEvent, true);
      window.removeEventListener('mousedown', onCaptureEvent, true);
      window.removeEventListener('mouseup', onCaptureEvent, true);
      window.removeEventListener('click', onCaptureEvent, true);
    }

    // --- Popup Dialog Logic ---
    function openPopup(el) {
      selectedElement = el;
      hoverBox.style.display = 'none';

      populateElementValues(el);

      const rect = el.getBoundingClientRect();
      const popW = popup.offsetWidth || 320;
      const popH = popup.offsetHeight || 340;
      let left = rect.left;
      let top = rect.bottom + 10;

      if (left + popW > window.innerWidth - 20) left = window.innerWidth - popW - 20;
      if (left < 20) left = 20;

      if (top + popH > window.innerHeight - 20) {
        top = rect.top - popH - 10;
      }
      if (top < 20) top = 20;

      popup.style.left = left + 'px';
      popup.style.top = top + 'px';
      popup.style.display = 'flex';

      popupActionBtn.disabled = !isConnected;
      if (!isConnected) {
        popupActionBtn.title = "Annotator disconnected";
      } else {
        popupActionBtn.title = "";
      }

      commentTextarea.value = '';
      variantsTextarea.value = '';
      setTimeout(() => commentTextarea.focus(), 50);
    }

    function closePopup() {
      if (selectedElement && originalInlineCss !== undefined) {
        // Roll back any uncommitted preview styles
        selectedElement.style.cssText = originalInlineCss;
      }
      popup.style.display = 'none';
      selectedElement = null;
      setAnnotateMode(false);
    }

    function onSaveClicked() {
      if (!isConnected) {
        alert("Cannot save: Annotator daemon is disconnected.");
        return;
      }
      if (!selectedElement) return;

      const comment = commentTextarea.value.trim();
      const variantPrompt = variantsTextarea.value.trim();
      const context = getElementContext(selectedElement);

      let mode = activeTab;
      let finalComment = comment;
      let variantInstructions = null;

      if (activeTab === 'variants' || variantPrompt) {
        mode = 'variants';
        finalComment = variantPrompt || comment;
        variantInstructions = {
          prompt: variantPrompt || comment,
          status: 'pending'
        };
      } else if (Object.keys(pendingChanges).length > 0) {
        mode = 'design';
      }

      const payload = {
        action: 'save_annotation',
        comment: finalComment,
        mode: mode,
        pending_changes: Object.keys(pendingChanges).length > 0 ? pendingChanges : null,
        variant_instructions: variantInstructions,
        element_context: context,
        created_at: new Date().toISOString()
      };

      try {
        if (typeof window[BINDING_NAME] === 'function') {
          window[BINDING_NAME](JSON.stringify(payload));
        } else {
          console.error('[CEF Annotator] Binding function not found:', BINDING_NAME);
        }
      } catch (err) {
        console.error('[CEF Annotator] Failed calling binding:', err);
      }

      // Close without rollback because change is confirmed
      originalInlineCss = selectedElement.style.cssText;
      popup.style.display = 'none';
      selectedElement = null;
      setAnnotateMode(false);
    }

    popupCancelBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      closePopup();
    });

    popupActionBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      onSaveClicked();
    });

    // --- Pins Rendering ---
    function matchesCurrentPage(annot) {
      if (!annot || !annot.url) return false;
      try {
        if (currentRouteHint && annot.element_context?.pageContext?.routeHint) {
          return annot.element_context.pageContext.routeHint === currentRouteHint;
        }
        const curUrl = new URL(window.location.href);
        const annotUrl = new URL(annot.url);
        return curUrl.origin === annotUrl.origin && curUrl.pathname === annotUrl.pathname;
      } catch (e) {
        return false;
      }
    }

    function resolveElementForAnnotation(annot) {
      const ctx = annot.element_context;
      if (!ctx || !ctx.selectors) return null;

      if (ctx.selectors.priority && Array.isArray(ctx.selectors.priority)) {
        for (const sel of ctx.selectors.priority) {
          try {
            const el = document.querySelector(sel);
            if (el) return el;
          } catch (e) {}
        }
      }

      if (ctx.selectors.nthOfTypePath) {
        try {
          const el = document.querySelector(ctx.selectors.nthOfTypePath);
          if (el) return el;
        } catch (e) {}
      }

      return null;
    }

    function renderPins() {
      pinsContainer.innerHTML = '';
      const relevant = activePins.filter(matchesCurrentPage);

      relevant.forEach((annot, index) => {
        const pin = document.createElement('div');
        pin.className = 'annot-pin';
        pin.textContent = String(index + 1);

        const tip = document.createElement('div');
        tip.className = 'annot-pin-tooltip';
        tip.textContent = annot.comment || '(No comment)';
        pin.appendChild(tip);

        const el = resolveElementForAnnotation(annot);
        let x = 0, y = 0;
        if (el) {
          const r = el.getBoundingClientRect();
          x = r.left + 4;
          y = r.top + 4;
        } else if (annot.element_context?.rect) {
          x = annot.element_context.rect.left + 4;
          y = annot.element_context.rect.top + 4;
        }

        x = Math.max(10, Math.min(window.innerWidth - 30, x));
        y = Math.max(10, Math.min(window.innerHeight - 30, y));

        pin.style.left = x + 'px';
        pin.style.top = y + 'px';
        pinsContainer.appendChild(pin);
      });
    }

    function schedulePinsReposition() {
      if (repositionTimer) return;
      repositionTimer = setTimeout(() => {
        repositionTimer = null;
        renderPins();
      }, 300);
    }

    // --- Heartbeat & Connection Check ---
    function checkHeartbeat() {
      const diff = Date.now() - lastHeartbeat;
      const prevConnected = isConnected;
      isConnected = (diff < 7000);

      if (isConnected !== prevConnected) {
        if (!isConnected) {
          cornerBtn.classList.add('disconnected');
          cornerBtn.title = "Annotator disconnected";
          popupActionBtn.disabled = true;
          popupActionBtn.title = "Annotator disconnected";
        } else {
          cornerBtn.classList.remove('disconnected');
          cornerBtn.title = "Toggle Annotator (Alt+Shift+A)";
          popupActionBtn.disabled = false;
          popupActionBtn.title = "";
        }
      }
    }

    // --- Global Keydown & Event Listeners ---
    function onKeyDown(e) {
      // Ctrl+Enter: Save
      if ((e.ctrlKey || e.metaKey) && (e.key === 'Enter' || e.keyCode === 13)) {
        if (popup.style.display === 'flex') {
          e.preventDefault();
          onSaveClicked();
          return;
        }
      }
      // Alt+Shift+A: Toggle annotate mode
      if (e.altKey && e.shiftKey && (e.key === 'A' || e.key === 'a' || e.keyCode === 65)) {
        e.preventDefault();
        setAnnotateMode(!isAnnotateMode);
        return;
      }
      // Esc: Exit annotate mode or close popup
      if (e.key === 'Escape' || e.keyCode === 27) {
        if (popup.style.display === 'flex') {
          closePopup();
        } else if (isAnnotateMode) {
          setAnnotateMode(false);
        }
        return;
      }
      // Target fine-tuning and shortcuts when in annotate mode and popup is closed
      if (isAnnotateMode && popup.style.display !== 'flex') {
        if (e.key === 'Delete' || e.keyCode === 46 || (e.shiftKey && (e.key === 'Backspace' || e.keyCode === 8))) {
          e.preventDefault();
          e.stopPropagation();
          deleteAllAnnotations();
          return;
        }
        if (e.key === 'ArrowUp' || e.keyCode === 38) {
          e.preventDefault();
          e.stopPropagation();
          fineTuneUp();
          return;
        }
        if (e.key === 'ArrowDown' || e.keyCode === 40) {
          e.preventDefault();
          e.stopPropagation();
          fineTuneDown();
          return;
        }
        if (e.key === 'Enter' || e.keyCode === 13) {
          if (hoveredElement && hoveredElement !== host && !host.contains(hoveredElement)) {
            e.preventDefault();
            e.stopPropagation();
            openPopup(hoveredElement);
            return;
          }
        }
      }
    }

    // --- Movable Corner Toolbar Dragging ---
    let isToolbarDragging = false;
    let toolbarStartX = 0;
    let toolbarStartY = 0;
    let toolbarInitialLeft = 0;
    let toolbarInitialTop = 0;
    let toolbarHasMoved = false;
    let toolbarJustDragged = false;

    function onToolbarPointerDown(e) {
      if (e.target.closest('#toolbar-close-btn') || e.target.closest('.key-badge')) {
        return;
      }
      isToolbarDragging = true;
      toolbarHasMoved = false;
      toolbarStartX = e.clientX;
      toolbarStartY = e.clientY;

      const rect = cornerBtn.getBoundingClientRect();
      toolbarInitialLeft = rect.left;
      toolbarInitialTop = rect.top;

      window.addEventListener('pointermove', onToolbarPointerMove, true);
      window.addEventListener('pointerup', onToolbarPointerUp, true);
    }

    function onToolbarPointerMove(e) {
      if (!isToolbarDragging) return;
      const dx = e.clientX - toolbarStartX;
      const dy = e.clientY - toolbarStartY;

      if (!toolbarHasMoved && (Math.abs(dx) > 3 || Math.abs(dy) > 3)) {
        toolbarHasMoved = true;
        cornerBtn.classList.add('dragging');
      }

      if (toolbarHasMoved) {
        const pad = 10;
        const maxLeft = window.innerWidth - cornerBtn.offsetWidth - pad;
        const maxTop = window.innerHeight - cornerBtn.offsetHeight - pad;
        const newLeft = Math.max(pad, Math.min(maxLeft, toolbarInitialLeft + dx));
        const newTop = Math.max(pad, Math.min(maxTop, toolbarInitialTop + dy));

        cornerBtn.style.left = newLeft + 'px';
        cornerBtn.style.top = newTop + 'px';
        cornerBtn.style.right = 'auto';
        cornerBtn.style.bottom = 'auto';
      }
    }

    function onToolbarPointerUp(e) {
      if (isToolbarDragging) {
        isToolbarDragging = false;
        cornerBtn.classList.remove('dragging');
        window.removeEventListener('pointermove', onToolbarPointerMove, true);
        window.removeEventListener('pointerup', onToolbarPointerUp, true);

        if (toolbarHasMoved) {
          toolbarJustDragged = true;
          setTimeout(() => { toolbarJustDragged = false; }, 80);
        }
      }
    }

    cornerBtn.addEventListener('pointerdown', onToolbarPointerDown);

    cornerBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (toolbarJustDragged) return;

      if (e.target.closest('#toolbar-close-btn') || e.target.closest('#key-badge-esc')) {
        setAnnotateMode(false);
        return;
      }
      if (e.target.closest('#key-badge-up')) {
        fineTuneUp();
        return;
      }
      if (e.target.closest('#key-badge-down')) {
        fineTuneDown();
        return;
      }
      if (e.target.closest('#key-badge-del')) {
        deleteAllAnnotations();
        return;
      }
      if (e.target.closest('#key-badge-enter')) {
        if (hoveredElement && hoveredElement !== host && !host.contains(hoveredElement)) {
          openPopup(hoveredElement);
        }
        return;
      }

      if (!isAnnotateMode) {
        setAnnotateMode(true);
      }
    });

    window.addEventListener('keydown', onKeyDown, true);
    window.addEventListener('popstate', schedulePinsReposition);
    window.addEventListener('hashchange', schedulePinsReposition);
    window.addEventListener('resize', schedulePinsReposition);

    try {
      mutationObserver = new MutationObserver(() => schedulePinsReposition());
    } catch (e) {}

    const heartbeatInterval = setInterval(checkHeartbeat, 2000);

    // --- Safely attach Host to Page DOM once available ---
    function initDOM() {
      const root = document.body || document.documentElement;
      if (!root) {
        if (document.readyState === 'loading') {
          document.addEventListener('DOMContentLoaded', initDOM, { once: true });
        } else {
          setTimeout(initDOM, 50);
        }
        return;
      }

      try {
        document.querySelectorAll('[id^=__cef_annot_host], [id^=__cef_annotator_host]').forEach(el => {
          if (el !== host) el.remove();
        });
      } catch (e) {}

      if (!document.contains(host)) {
        root.appendChild(host);
        renderPins();
      }

      if (mutationObserver) {
        try {
          mutationObserver.observe(root, {
            childList: true,
            subtree: true,
            attributes: false
          });
        } catch (e) {}
      }
    }

    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', initDOM, { once: true });
    } else {
      initDOM();
    }

    // --- External API Interface for annotator.py ---
    const externalApi = {
      toggle: () => setAnnotateMode(!isAnnotateMode),
      setAnnotateMode: (val) => setAnnotateMode(!!val),
      heartbeat: () => {
        lastHeartbeat = Date.now();
        checkHeartbeat();
      },
      updatePins: (newPins) => {
        activePins = Array.isArray(newPins) ? newPins : [];
        renderPins();
      },
      clearPins: () => {
        activePins = [];
        renderPins();
      },
      deleteAll: () => deleteAllAnnotations(),
      setRouteHint: (newRoute) => {
        currentRouteHint = newRoute;
        renderPins();
      },
      hideForScreenshot: () => {
        host.style.display = 'none';
      },
      restoreAfterScreenshot: () => {
        host.style.display = 'block';
      },
      cleanup: () => {
        clearInterval(heartbeatInterval);
        if (mutationObserver) mutationObserver.disconnect();
        window.removeEventListener('keydown', onKeyDown, true);
        window.removeEventListener('popstate', schedulePinsReposition);
        window.removeEventListener('hashchange', schedulePinsReposition);
        window.removeEventListener('resize', schedulePinsReposition);
        window.removeEventListener('pointermove', onDragMove, true);
        window.removeEventListener('pointerup', endDrag, true);
        window.removeEventListener('pointermove', onResizeMove, true);
        window.removeEventListener('pointerup', endResize, true);
        window.removeEventListener('pointermove', onToolbarPointerMove, true);
        window.removeEventListener('pointerup', onToolbarPointerUp, true);
        removeCaptureListeners();
        host.remove();
        delete window[GUARD];
      }
    };

    window[API_NAME] = externalApi;

  } catch (err) {
    console.error('[CEF Annotator] Overlay initialization error:', err);
  }
})();
