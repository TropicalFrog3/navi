/**
 * @file  depot-resolver.js
 * @brief Async SteamCMD info resolver for DLC names, sizes, and OS configurations
 */
function resolveSteamCmdMetadata(appid, depotItems, updateButtonState, getUserInteracted) {
    fetch('https://api.steamcmd.net/v1/info/' + appid)
                        .then(function(r) { return r.json(); })
                        .then(function(steamData) {
                            dismissResolvingPill();
                            if (!steamData || !steamData.data || !steamData.data[appid]) return;
                            var appData = steamData.data[appid];
                            
                            for (var m = 0; m < depotItems.length; m++) {
                                var el = depotItems[m];
                                var nameEl = el.querySelector('.sls-depot-name');
                                var metaEl = el.querySelector('.sls-depot-meta');
                                var sizeSpan = metaEl.querySelectorAll('span')[1];
                                var cb = el.querySelector('.sls-depot-checkbox');
                                var did = cb.value;
                                var hasOs = cb.getAttribute('data-os');
                                
                                // Resolve DLC names
                                if (nameEl.innerText.indexOf('Additional Content') === 0) {
                                    var depotMeta = appData.depots && appData.depots[did];
                                    if (depotMeta && depotMeta.dlcappid) {
                                        // Fetch the DLC app name
                                        (function(nEl, dlcId) {
                                            fetch('https://api.steamcmd.net/v1/info/' + dlcId)
                                                .then(function(r2) { return r2.json(); })
                                                .then(function(dlcData) {
                                                    if (dlcData && dlcData.data && dlcData.data[dlcId] && dlcData.data[dlcId].common && dlcData.data[dlcId].common.name) {
                                                        // Preserve any OS badge already appended
                                                        var existingBadge = nEl.querySelector('.sls-os-badge');
                                                        nEl.innerText = dlcData.data[dlcId].common.name;
                                                        if (existingBadge) nEl.appendChild(existingBadge);
                                                    }
                                                }).catch(function(){});
                                        })(nameEl, depotMeta.dlcappid);
                                    }
                                }
                                
                                // Resolve unknown sizes
                                if (sizeSpan.innerText === 'Unknown size' && appData.depots && appData.depots[did] && appData.depots[did].manifests && appData.depots[did].manifests.public && appData.depots[did].manifests.public.size) {
                                    sizeSpan.innerText = formatBytes(parseInt(appData.depots[did].manifests.public.size, 10));
                                }
                                
                                // Resolve missing OS info from SteamCMD
                                if (!hasOs && appData.depots && appData.depots[did] && appData.depots[did].config && appData.depots[did].config.oslist) {
                                    var oslist = appData.depots[did].config.oslist;
                                    cb.setAttribute('data-os', oslist);
                                    var badge = buildOsBadgeElement(oslist);
                                    if (badge) nameEl.appendChild(badge);
                                    
                                    // If this was labeled Additional Content but it's actually a base game OS depot, fix the name
                                    if (nameEl.innerText.indexOf('Additional Content') === 0) {
                                        var depotIdNum = parseInt(did, 10);
                                        var appIdNum = parseInt(appid, 10);
                                        if (depotIdNum >= appIdNum && depotIdNum <= appIdNum + 10) {
                                            nameEl.innerText = 'Base Game Content';
                                            nameEl.appendChild(badge);
                                        }
                                    }
                                }
                            }
    
                            var userInteracted = typeof getUserInteracted === 'function' ? getUserInteracted() : false;
                if (!userInteracted) {
                                var anyLinuxFound = false;
                                for (var n = 0; n < depotItems.length; n++) {
                                    var cBox = depotItems[n].querySelector('.sls-depot-checkbox');
                                    var cOs = (cBox.getAttribute('data-os') || '').toLowerCase();
                                    if (cOs.indexOf('linux') !== -1) {
                                        anyLinuxFound = true;
                                        break;
                                    }
                                }
                                for (var n = 0; n < depotItems.length; n++) {
                                    var cBox = depotItems[n].querySelector('.sls-depot-checkbox');
                                    var cOs = (cBox.getAttribute('data-os') || '').toLowerCase();
                                    if (anyLinuxFound) {
                                        cBox.checked = (!cOs || cOs.indexOf('linux') !== -1);
                                    } else {
                                        cBox.checked = (!cOs || cOs.indexOf('windows') !== -1);
                                    }
                                }
                                updateButtonState();
                            }
                        }).catch(function() {
                            dismissResolvingPill();
                        });
}
