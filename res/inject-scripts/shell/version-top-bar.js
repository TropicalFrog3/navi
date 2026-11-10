/**
 * @file  version-top-bar.js
 * @brief Renders the Navi version banner in Steam client navigation header
 */
(function () {
	'use strict';

	if (window.__slsVersionTopBarInjected) return;
	window.__slsVersionTopBarInjected = true;

	var NAV_CONTAINER_CLASS = '_1Ky59qmywxOUtNcI1cgmkX';
	var VERSION_ATTRIBUTE = 'data-navi-version';
	var VERSION_TEXT = 'Navi %VERSION%';

	function addVersionLabel() {
		var nav = document.querySelector('.' + NAV_CONTAINER_CLASS);
		if (!nav || nav.querySelector('[' + VERSION_ATTRIBUTE + ']')) return;

		var versionLabel = document.createElement('div');
		versionLabel.setAttribute(VERSION_ATTRIBUTE, '');
		versionLabel.textContent = VERSION_TEXT;
		versionLabel.style.cssText = [
			'align-items: center',
			'color: rgba(216, 222, 233, 0.7)',
			'display: flex',
			'font-size: 11px',
			'margin-left: 10px',
			'white-space: nowrap'
		].join(';');
		nav.appendChild(versionLabel);
	}

	addVersionLabel();

	new MutationObserver(addVersionLabel).observe(document.documentElement, {
		childList: true,
		subtree: true
	});
})();
