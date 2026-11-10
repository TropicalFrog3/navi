/**
 * @file  config-dialogs.js
 * @brief Modal confirmation dialogs and status alert banners for configuration UI
 */
function showStatusBanner(msg, color) {
            var banner = document.getElementById('sls-cfg-status-banner');
            if (!banner) return;
            banner.textContent = msg;
            banner.style.display = 'block';
            var isGreen = (color === '#2ecc71');
            var isBlue = (color === '#66c0f4' || color === 'rgb(26, 159, 255)');
            var isAmber = (color === '#f59e0b');
            banner.style.backgroundColor = isGreen ? 'rgba(46, 204, 113, 0.15)' : (isBlue ? 'rgba(26, 159, 255, 0.15)' : (isAmber ? 'rgba(245, 158, 11, 0.15)' : 'rgba(255, 77, 77, 0.15)'));
            banner.style.borderColor = isGreen ? '#2ecc71' : (isBlue ? 'rgb(26, 159, 255)' : (isAmber ? '#f59e0b' : '#ff4d4d'));
            banner.style.color = isGreen ? '#2ecc71' : (isBlue ? 'rgb(26, 159, 255)' : (isAmber ? '#fbbf24' : '#ff4d4d'));
            setTimeout(function () {
                if (banner) banner.style.display = 'none';
            }, 6000);
        }

        function hideStatusBanner() {
            var banner = document.getElementById('sls-cfg-status-banner');
            if (banner) banner.style.display = 'none';
        }

        function slsConfirmDialog(opts) {
            if (document.getElementById('sls-cfg-dialog-backdrop')) return null;
            opts = opts || {};

            var backdrop = document.createElement('div');
            backdrop.id = 'sls-cfg-dialog-backdrop';
            backdrop.style.cssText = 'position: fixed; top: 0; left: 0; right: 0; bottom: 0; z-index: 1000001; background: rgba(0, 0, 0, 0.6); display: flex; align-items: center; justify-content: center; padding: 16px; box-sizing: border-box;';

            var dialog = document.createElement('div');
            dialog.setAttribute('role', 'dialog');
            dialog.setAttribute('aria-modal', 'true');
            dialog.setAttribute('aria-label', opts.title || 'Confirm');
            dialog.style.cssText = 'background: #23262e; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; max-width: 460px; width: 100%; box-sizing: border-box; padding: 20px; display: flex; flex-direction: column; gap: 12px; box-shadow: 0 12px 40px rgba(0, 0, 0, 0.5);';
            if (opts.danger) dialog.style.borderLeft = '3px solid #ef4444';

            var titleEl = document.createElement('div');
            titleEl.style.cssText = 'color: #ffffff; font-size: 17px; font-weight: 600; line-height: 22px;';
            titleEl.textContent = opts.title || 'Confirm';
            dialog.appendChild(titleEl);

            if (opts.body) {
                var bodyEl = document.createElement('div');
                bodyEl.style.cssText = 'color: #94a3b8; font-size: 13px; line-height: 18px;';
                bodyEl.textContent = opts.body;
                dialog.appendChild(bodyEl);
            }

            var inputEl = null;
            var errorEl = null;
            if (opts.input) {
                var fieldLabel = document.createElement('label');
                fieldLabel.style.cssText = 'display: flex; flex-direction: column; gap: 4px; color: #8a929e; font-size: 12px;';
                var labelText = document.createElement('span');
                labelText.textContent = opts.input.label || '';
                fieldLabel.appendChild(labelText);
                inputEl = document.createElement('input');
                inputEl.type = 'text';
                inputEl.className = 'sls-cfg-input';
                inputEl.style.width = '100%';
                inputEl.autocomplete = 'off';
                inputEl.placeholder = opts.input.placeholder || '';
                inputEl.setAttribute('aria-label', opts.input.label || '');
                fieldLabel.appendChild(inputEl);
                dialog.appendChild(fieldLabel);

                errorEl = document.createElement('div');
                errorEl.className = 'sls-cfg-inline-error';
                errorEl.setAttribute('role', 'alert');
                errorEl.style.display = 'none';
                dialog.appendChild(errorEl);
            }

            var buttonRow = document.createElement('div');
            buttonRow.style.cssText = 'display: flex; justify-content: flex-end; gap: 8px; flex-wrap: wrap; margin-top: 4px;';

            var cancelBtn = document.createElement('button');
            cancelBtn.type = 'button';
            cancelBtn.className = 'sls-cfg-btn';
            cancelBtn.style.cssText = 'padding: 8px 14px;';
            cancelBtn.textContent = opts.cancelLabel || 'Cancel';

            var confirmBtn = document.createElement('button');
            confirmBtn.type = 'button';
            confirmBtn.className = 'sls-cfg-btn';
            confirmBtn.style.cssText = 'padding: 8px 14px; background: ' + (opts.danger ? 'rgba(239, 68, 68, 0.2)' : '#1a9fff') + '; border: 1px solid ' + (opts.danger ? 'rgba(239, 68, 68, 0.5)' : '#1a9fff') + '; color: #ffffff; font-weight: 600; border-radius: 6px;';
            confirmBtn.textContent = opts.confirmLabel || 'Confirm';
            confirmBtn.disabled = !!(opts.input && opts.input.expected);

            buttonRow.appendChild(cancelBtn);
            buttonRow.appendChild(confirmBtn);
            dialog.appendChild(buttonRow);
            backdrop.appendChild(dialog);

            function closeDialog() {
                document.removeEventListener('keydown', onKeyDown, true);
                if (backdrop.parentNode) backdrop.parentNode.removeChild(backdrop);
            }

            function tryConfirm() {
                if (opts.input && opts.input.expected) {
                    var expected = opts.input.expected || '';
                    if (inputEl.value !== expected) {
                        if (errorEl) {
                            errorEl.textContent = opts.mismatchMessage || '';
                            errorEl.style.display = 'block';
                        }
                        return;
                    }
                }
                closeDialog();
                if (typeof opts.onConfirm === 'function') opts.onConfirm(inputEl ? inputEl.value : undefined);
            }

            function onKeyDown(e) {
                if (e.key === 'Escape') {
                    e.preventDefault();
                    closeDialog();
                    return;
                }
                if (e.key !== 'Tab') return;
                var focusables = dialog.querySelectorAll('button, input');
                if (!focusables.length) return;
                var first = focusables[0];
                var last = focusables[focusables.length - 1];
                if (e.shiftKey && document.activeElement === first) {
                    e.preventDefault();
                    last.focus();
                } else if (!e.shiftKey && document.activeElement === last) {
                    e.preventDefault();
                    first.focus();
                }
            }

            cancelBtn.onclick = closeDialog;
            confirmBtn.onclick = tryConfirm;

            dialog.vgp_oncancel = closeDialog;
            backdrop.vgp_oncancel = closeDialog;

            if (inputEl) {
                inputEl.oninput = function () {
                    confirmBtn.disabled = opts.input.expected ? (inputEl.value !== opts.input.expected) : false;
                    if (errorEl) {
                        errorEl.textContent = '';
                        errorEl.style.display = 'none';
                    }
                };
                inputEl.onkeydown = function (e) {
                    if (e.key === 'Enter') {
                        e.preventDefault();
                        tryConfirm();
                    }
                };
            }

            document.body.appendChild(backdrop);
            document.addEventListener('keydown', onKeyDown, true);
            if (inputEl) inputEl.focus();
            else confirmBtn.focus();

            return backdrop;
        }
