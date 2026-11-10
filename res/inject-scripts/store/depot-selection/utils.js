/**
 * @file  utils.js
 * @brief Formatting utilities for byte sizes and OS badge generation
 */
function formatBytes(bytes, decimals) {
    if (bytes == 0) return '0 Bytes';
    if (decimals === undefined) decimals = 2;
    var k = 1024,
        dm = decimals < 0 ? 0 : decimals,
        sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'],
        i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

function getOsInfo(oslist) {
    if (!oslist) return null;
    var os = oslist.toLowerCase();
    if (os.indexOf('windows') !== -1) return { label: 'Windows', cls: 'sls-os-windows' };
    if (os.indexOf('linux') !== -1) return { label: 'Linux', cls: 'sls-os-linux' };
    if (os.indexOf('macos') !== -1 || os.indexOf('osx') !== -1) return { label: 'macOS', cls: 'sls-os-macos' };
    return null;
}

// Returns an HTML string for an OS badge (used during initial render)
function buildOsBadge(oslist) {
    var info = getOsInfo(oslist);
    if (!info) return '';
    return ' <span class="sls-os-badge ' + info.cls + '">' + info.label + '</span>';
}

// Returns a DOM element for an OS badge (used during async updates)
function buildOsBadgeElement(oslist) {
    var info = getOsInfo(oslist);
    if (!info) return null;
    var el = document.createElement('span');
    el.className = 'sls-os-badge ' + info.cls;
    el.innerText = info.label;
    return el;
}

function dismissResolvingPill() {
    var pill = document.getElementById('sls-resolving-pill');
    if (pill) {
        pill.style.opacity = '0';
        setTimeout(function() { if (pill && pill.parentNode) pill.remove(); }, 300);
    }
}
