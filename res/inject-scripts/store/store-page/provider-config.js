/**
 * @file  provider-config.js
 * @brief Lua provider settings, API key synchronization, and download request dispatcher
 */
// Cache of app unlock status: { appid: { exists, pending, onlineFixInstalled, autoCrackInstalled } }
var appUnlockStatus = {};

var luaProviders = [
    { name: 'Ryuu', key: 'sls-ryuu-key', url: 'https://generator.ryuu.lol' },
    { name: 'DepotBox', key: 'sls-dpbx-key', url: 'https://depotbox.org/' },
    { name: 'HubcapDB', key: 'sls-hubcap-key', url: 'https://hubcapmanifest.com/api-keys/stats' },
    { name: 'Spinoza', key: 'sls-spinoza-key', url: '#' }
];

function getProviderOrder() {
    if (localStorage.getItem('sls-lua-provider-order-v') !== '4') {
        var oldSaved = (localStorage.getItem('sls-lua-provider-order') || '').split(',');
        var migrated = [];
        var indexMap = { 0: 0, 1: 1, 2: 2, 3: 3, 5: 3 }; // Ryuu, DepotBox, HubcapDB, Spinoza
        oldSaved.forEach(function(value) {
            var idx = parseInt(value, 10);
            var target = indexMap[idx];
            if (target !== undefined && target < luaProviders.length && migrated.indexOf(target) === -1) {
                migrated.push(target);
            }
        });
        for (var j = 0; j < luaProviders.length; j++) {
            if (migrated.indexOf(j) === -1) migrated.push(j);
        }
        localStorage.setItem('sls-lua-provider-order', migrated.join(','));
        localStorage.removeItem('sls-ost-key');
        localStorage.removeItem('sls-wudrm-key');
        localStorage.removeItem('sls-steamrun-key');
        localStorage.removeItem('sls-morr-key');
        localStorage.removeItem('sls-sushi-key');
        localStorage.removeItem('sls-twentytwo-key');
        localStorage.setItem('sls-lua-provider-order-v', '4');
        return migrated;
    }

    var saved = (localStorage.getItem('sls-lua-provider-order') || '').split(',');
    var order = [];
    saved.forEach(function(value) {
        var index = parseInt(value, 10);
        if (index >= 0 && index < luaProviders.length && order.indexOf(index) === -1) order.push(index);
    });
    for (var i = 0; i < luaProviders.length; i++) {
        if (order.indexOf(i) === -1) order.push(i);
    }
    return order;
}

function saveProviderOrder(order) {
    localStorage.setItem('sls-lua-provider-order', order.join(','));
}

function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, function(character) {
        return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[character];
    });
}

function sanitizeSeedKey(val) {
    if (!val || val.indexOf('%') === 0) return '';
    return val;
}

var configApiKeys = {
    'sls-ryuu-key': sanitizeSeedKey('%RYUU_KEY%'),
    'sls-dpbx-key': sanitizeSeedKey('%DPBX_KEY%'),
    'sls-hubcap-key': sanitizeSeedKey('%HUBCAP_KEY%')
};

try {
    localStorage.removeItem('sls-ryuu-key');
    localStorage.removeItem('sls-dpbx-key');
    localStorage.removeItem('sls-hubcap-key');
} catch (_) {}

function fetchConfigApiKeys(onLoaded) {
    naviFetch('http://127.0.0.1:9001/api-keys')
        .then(function(res) {
            if (!res.ok) throw new Error('HTTP ' + res.status);
            return res.json();
        })
        .then(function(data) {
            if (data && typeof data === 'object') {
                if (typeof data.ryuu === 'string') configApiKeys['sls-ryuu-key'] = data.ryuu;
                if (typeof data.dpbx === 'string') configApiKeys['sls-dpbx-key'] = data.dpbx;
                if (typeof data.hubcap === 'string') configApiKeys['sls-hubcap-key'] = data.hubcap;
            }
            if (typeof onLoaded === 'function') onLoaded(configApiKeys);
        })
        .catch(function() {
            if (typeof onLoaded === 'function') onLoaded(configApiKeys);
        });
}

function sendApiKeys() {
    var ryuu = configApiKeys['sls-ryuu-key'] || '';
    var dpbx = configApiKeys['sls-dpbx-key'] || '';
    var hubcap = configApiKeys['sls-hubcap-key'] || '';

    naviFetch('http://127.0.0.1:9001/set-api-keys?ryuu=' + encodeURIComponent(ryuu) + '&dpbx=' + encodeURIComponent(dpbx) + '&hubcap=' + encodeURIComponent(hubcap)).catch(function() {});
}

function sendLuaRequest(appid, providerIndex, customOrder) {
    var params = { id: appid };
    if (providerIndex === null) params.order = (customOrder || getProviderOrder()).join(',');
    else params.provider = providerIndex;
    return naviRequest('/download', params);
}

