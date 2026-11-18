/**
 * Smart Route Planner - Hyderabad Metropolitan Navigation Engine
 * High-Performance Client Application
 * Communicates with the C routing engine via Node.js backend.
 */

// ==========================================================================
// Application State
// ==========================================================================
let map;
let mapData = null;
let routingMode = 'time'; // 'time', 'distance', or 'eco'
let activeWaypoints = [];
let simulatedTraffic = new Map(); // key: "u-v", value: congestionFactor
let currentRouteData = null;

// Leaflet Layer Groups
let networkLayerGroup;
let routeLayerGroup;
let markersLayerGroup;
let primaryGlowLayer;
let primaryCoreLayer;
let vehicleLayerGroup;

// Simulation & Live Navigation State
let vehicleMarker = null;
let isSimulating = false;
let isPaused = false;
let simSpeed = 1; // 1, 2, 5
let simProgressKm = 0; // Current distance traveled along polyline
let totalRouteKm = 0;
let routeSegments = []; // Array of segment geometry, distance, speed, and heading
let animFrameId = null;
let lastAnimTimestamp = null;
let isReroutingInProgress = false;
let rerouteDebounceTimer = null;

// Initialize on DOM load
document.addEventListener('DOMContentLoaded', async () => {
    initLeafletMap();
    await fetchMapDataset();
    setupEventListeners();
});

/**
 * Initialize Leaflet map instance
 */
function initLeafletMap() {
    map = L.map('map', {
        center: [17.4250, 78.4680], // Centered over Central Hyderabad & Hussain Sagar Lake
        zoom: 12,
        zoomControl: true,
        attributionControl: true
    });

    // Reliable standard OpenStreetMap tiles with sleek dark mode wrap
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors | Hyderabad Traffic Model',
        subdomains: ['a', 'b', 'c'],
        maxZoom: 19
    }).addTo(map);

    networkLayerGroup = L.layerGroup().addTo(map);
    routeLayerGroup = L.layerGroup().addTo(map);
    markersLayerGroup = L.layerGroup().addTo(map);
    vehicleLayerGroup = L.layerGroup().addTo(map);
}

/**
 * Fetch map dataset from backend GET /api/map
 */
async function fetchMapDataset() {
    try {
        const response = await fetch('/api/map');
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        mapData = await response.json();

        // Initialize Searchable Dropdowns for Source and Destination
        initSearchableDropdown('search-source', 'menu-source', 'select-source', 0, () => {
            updateMarkerHighlights();
        });

        initSearchableDropdown('search-destination', 'menu-destination', 'select-destination', 31, () => {
            updateMarkerHighlights();
        });

        populateCorridorSelector(mapData.edges, mapData.nodes);
        renderRoadNetwork(mapData);
        renderLandmarkMarkers(mapData.nodes);

        // Select Ameerpet Circle <-> Panjagutta bottleneck by default
        selectCorridor(9, 10);

        document.getElementById('status-text').textContent = 'C Engine Online (32 Nodes, 51 Roads)';
        document.getElementById('status-dot').style.backgroundColor = '#10b981';
    } catch (err) {
        showToast('Error loading map network: ' + err.message);
        document.getElementById('status-text').textContent = 'Server Disconnected';
        document.getElementById('status-dot').style.backgroundColor = '#f43f5e';
    }
}

/**
 * Custom Searchable Dropdown with autocomplete filtering
 */
function initSearchableDropdown(inputId, menuId, hiddenId, defaultId, onSelectCallback) {
    const input = document.getElementById(inputId);
    const menu = document.getElementById(menuId);
    const hidden = document.getElementById(hiddenId);

    if (!input || !menu || !hidden || !mapData) return;

    if (defaultId !== null && defaultId !== undefined) {
        const defaultNode = mapData.nodes.find(n => n.id === defaultId);
        if (defaultNode) {
            input.value = `[${defaultNode.id}] ${defaultNode.name}`;
            hidden.value = defaultNode.id;
        }
    }

    function renderMenuItems(filterText = '') {
        menu.innerHTML = '';
        const lowerFilter = filterText.toLowerCase().trim();

        const filtered = mapData.nodes.filter(n => {
            if (!lowerFilter) return true;
            return n.name.toLowerCase().includes(lowerFilter) ||
                   n.id.toString().includes(lowerFilter) ||
                   (n.category && n.category.toLowerCase().includes(lowerFilter));
        });

        if (filtered.length === 0) {
            const noMatch = document.createElement('div');
            noMatch.className = 'search-item';
            noMatch.style.color = '#94a3b8';
            noMatch.style.cursor = 'default';
            noMatch.textContent = 'No matching Hyderabad landmarks';
            menu.appendChild(noMatch);
            return;
        }

        filtered.forEach(node => {
            const item = document.createElement('div');
            item.className = 'search-item' + (parseInt(hidden.value) === node.id ? ' selected' : '');
            item.innerHTML = `
                <div class="search-item-left">
                    <span class="search-item-id">#${node.id}</span>
                    <span style="font-weight: 500;">${node.name}</span>
                </div>
                <span class="search-item-cat">${node.category}</span>
            `;
            item.addEventListener('click', () => {
                selectNode(node);
            });
            menu.appendChild(item);
        });
    }

    function selectNode(node) {
        input.value = `[${node.id}] ${node.name}`;
        hidden.value = node.id;
        menu.classList.remove('open');
        if (onSelectCallback) onSelectCallback(node.id);
    }

    input.addEventListener('focus', () => {
        renderMenuItems(input.value.replace(/^\[\d+\]\s*/, ''));
        menu.classList.add('open');
    });

    input.addEventListener('input', (e) => {
        renderMenuItems(e.target.value);
        menu.classList.add('open');
    });

    document.addEventListener('click', (e) => {
        if (!input.contains(e.target) && !menu.contains(e.target)) {
            menu.classList.remove('open');
            const currentNode = mapData.nodes.find(n => n.id === parseInt(hidden.value));
            if (currentNode && !input.value.trim()) {
                input.value = `[${currentNode.id}] ${currentNode.name}`;
            }
        }
    });
}

/**
 * Populate corridor selector with major bottlenecks
 */
function populateCorridorSelector(edges, nodes) {
    const nodeMap = new Map(nodes.map(n => [n.id, n]));
    const corridorSelect = document.getElementById('select-corridor');
    corridorSelect.innerHTML = '';

    // Prominently highlight major bottlenecks
    const majorCorridors = [
        { u: 9, v: 10, label: '★ Ameerpet Circle (Ameerpet ↔ Panjagutta Corridor)' },
        { u: 8, v: 9,  label: '★ Panjagutta Junction (Panjagutta ↔ Banjara Hills Rd 1)' },
        { u: 30, v: 31, label: '★ LB Nagar Junction (Dilsukhnagar ↔ LB Nagar NH65)' },
        { u: 20, v: 21, label: '★ Tank Bund Road (Telugu Thalli ↔ Tank Bund East Shore)' },
        { u: 12, v: 15, label: '★ PVNR Marg / Necklace Road (Khairatabad ↔ West Shore)' },
        { u: 22, v: 23, label: '★ Charminar / Nayapul Corridor (Charminar ↔ Koti)' },
        { u: 24, v: 25, label: '★ Chaderghat Musi River Bridge (Chaderghat ↔ Malakpet)' },
        { u: 2, v: 4,  label: '★ HITEC City Cyber Towers (Cyber Towers ↔ Madhapur)' },
        { u: 5, v: 6,  label: '★ Jubilee Hills Checkpost (Road No. 36 ↔ Checkpost)' }
    ];

    const optGroupFeatured = document.createElement('optgroup');
    optGroupFeatured.label = '🔥 Major Bottlenecks';

    majorCorridors.forEach(item => {
        const val = `${item.u}-${item.v}`;
        optGroupFeatured.appendChild(new Option(item.label, val));
    });
    corridorSelect.appendChild(optGroupFeatured);

    const optGroupAll = document.createElement('optgroup');
    optGroupAll.label = 'All Hyderabad Corridors (51 Roads)';

    edges.forEach((edge) => {
        const u = nodeMap.get(edge.source);
        const v = nodeMap.get(edge.target);
        const uName = u ? u.name : `Node ${edge.source}`;
        const vName = v ? v.name : `Node ${edge.target}`;
        const label = `${edge.road_name} (${uName} ↔ ${vName})`;
        const val = `${edge.source}-${edge.target}`;
        optGroupAll.appendChild(new Option(label, val));
    });
    corridorSelect.appendChild(optGroupAll);
}

/**
 * Render all road segments on the Leaflet map with live traffic colors
 */
function renderRoadNetwork(data) {
    networkLayerGroup.clearLayers();
    const nodeMap = new Map(data.nodes.map(n => [n.id, n]));

    data.edges.forEach(edge => {
        const u = nodeMap.get(edge.source);
        const v = nodeMap.get(edge.target);
        if (!u || !v) return;

        const edgeKey = getEdgeKey(edge.source, edge.target);
        const currentCongestion = simulatedTraffic.get(edgeKey) || edge.congestion;

        let color = '#10b981'; // Green: Free flow
        let opacity = 0.50;
        let weight = 3;

        if (currentCongestion >= 2.2) {
            color = '#f43f5e'; // Red: Gridlock
            opacity = 0.85;
            weight = 4.5;
        } else if (currentCongestion >= 1.3) {
            color = '#f59e0b'; // Amber: Moderate / Heavy
            opacity = 0.70;
            weight = 3.5;
        }

        let edgeLatLngs;
        if (edge.pathCoordinates && edge.pathCoordinates.length > 0) {
            edgeLatLngs = edge.pathCoordinates.map(pt => pt[0] > 50 ? [pt[1], pt[0]] : [pt[0], pt[1]]);
        } else {
            edgeLatLngs = [
                [u.lat, u.lng],
                [v.lat, v.lng]
            ];
        }

        const poly = L.polyline(edgeLatLngs, {
            color: color,
            weight: weight,
            opacity: opacity,
            dashArray: currentCongestion >= 2.0 ? '4, 4' : null
        }).addTo(networkLayerGroup);

        poly.bindPopup(`
            <div style="font-family: var(--font-sans, sans-serif); font-size: 12px; line-height: 1.5; color: #0f172a;">
                <div style="font-weight: 700; font-size: 13px; color: #1e293b; margin-bottom: 2px;">${edge.road_name}</div>
                <div style="color: #475569; font-weight: 500;">${u.name} &harr; ${v.name}</div>
                <div style="margin-top: 4px; display: flex; gap: 8px; font-size: 11px; color: #64748b;">
                    <span>Dist: <b>${edge.distance} km</b></span>
                    <span>Speed: <b>${edge.speed} km/h</b></span>
                </div>
                <div style="margin-top: 4px; font-size: 11px;">
                    Traffic Congestion: <b style="color: ${color};">${currentCongestion.toFixed(2)}x</b>
                    ${currentCongestion >= 2.0 ? '<span style="color: #e11d48; font-weight: 700;"> (GRIDLOCK)</span>' : ''}
                </div>
                <button onclick="selectCorridor(${edge.source}, ${edge.target})" 
                        style="margin-top: 8px; width: 100%; padding: 5px 8px; font-size: 11px; font-weight: 600; background: #2563eb; color: white; border: none; border-radius: 4px; cursor: pointer;">
                    ⚡ Simulate Bottleneck Here
                </button>
            </div>
        `);
    });
}

/**
 * Helper to get all connected branches for a specific node
 */
function getNodeConnectingBranches(nodeId) {
    if (!mapData) return [];
    const branches = [];
    const nodeMap = new Map(mapData.nodes.map(n => [n.id, n]));

    mapData.edges.forEach(edge => {
        if (edge.source === nodeId || edge.target === nodeId) {
            const otherId = edge.source === nodeId ? edge.target : edge.source;
            const otherNode = nodeMap.get(otherId);
            const edgeKey = getEdgeKey(edge.source, edge.target);
            const congestion = simulatedTraffic.get(edgeKey) || edge.congestion;

            branches.push({
                neighborId: otherId,
                neighborName: otherNode ? otherNode.name : `Node ${otherId}`,
                roadName: edge.road_name,
                distance: edge.distance,
                speed: edge.speed,
                congestion: congestion
            });
        }
    });

    return branches;
}

/**
 * Render interactive markers for each landmark intersection
 * Distinctive custom markers: Start (Green), Destination (Red), Waypoints (Purple)
 */
function renderLandmarkMarkers(nodes) {
    markersLayerGroup.clearLayers();

    const srcId = parseInt(document.getElementById('select-source')?.value ?? -1);
    const dstId = parseInt(document.getElementById('select-destination')?.value ?? -1);
    const wpIds = activeWaypoints.map(w => parseInt(w.selectEl.value));

    nodes.forEach(node => {
        let pinClass = 'marker-node';
        let innerHtml = `${node.id}`;
        let pulseHtml = '';
        let iconSize = [24, 24];

        if (node.id === srcId) {
            pinClass = 'marker-start';
            innerHtml = 'A';
            pulseHtml = '<div class="pulse-ring ring-green"></div>';
            iconSize = [34, 34];
        } else if (node.id === dstId) {
            pinClass = 'marker-dest';
            innerHtml = 'B';
            pulseHtml = '<div class="pulse-ring ring-red"></div>';
            iconSize = [34, 34];
        } else {
            const wpIndex = wpIds.indexOf(node.id);
            if (wpIndex !== -1) {
                pinClass = 'marker-waypoint';
                innerHtml = `W${wpIndex + 1}`;
                iconSize = [30, 30];
            }
        }

        const icon = L.divIcon({
            className: 'custom-pin-wrapper',
            html: `
                <div class="custom-pin ${pinClass}">
                    <div class="marker-inner">${innerHtml}</div>
                    ${pulseHtml}
                </div>
            `,
            iconSize: iconSize,
            iconAnchor: [iconSize[0] / 2, iconSize[1] / 2]
        });

        const marker = L.marker([node.lat, node.lng], { icon: icon }).addTo(markersLayerGroup);

        const branches = getNodeConnectingBranches(node.id);
        let branchesListHtml = branches.map(b => {
            let congColor = '#10b981';
            if (b.congestion >= 2.0) congColor = '#f43f5e';
            else if (b.congestion >= 1.3) congColor = '#f59e0b';

            return `
                <div style="font-size: 11px; padding: 3px 0; border-bottom: 1px solid rgba(0,0,0,0.06); display: flex; justify-content: space-between; align-items: center;">
                    <div>
                        <span style="font-weight: 600; color: #1e293b;">&rarr; ${b.neighborName}</span>
                        <div style="font-size: 10px; color: #64748b;">${b.roadName} (${b.distance} km @ ${b.speed} km/h)</div>
                    </div>
                    <span style="font-weight: 700; color: ${congColor}; font-size: 11px; margin-left: 6px;">${b.congestion.toFixed(1)}x</span>
                </div>
            `;
        }).join('');

        marker.bindPopup(`
            <div style="font-family: var(--font-sans, sans-serif); width: 260px; line-height: 1.4; color: #0f172a;">
                <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 4px;">
                    <strong style="font-size: 13.5px; color: #0f172a;">[${node.id}] ${node.name}</strong>
                    <span style="font-size: 10px; background: #e2e8f0; color: #334155; padding: 2px 6px; border-radius: 4px; font-weight: 600;">${node.category}</span>
                </div>
                
                <div style="font-size: 11px; font-family: monospace; color: #64748b; margin-bottom: 8px;">
                    Lat: ${node.lat.toFixed(5)}, Lng: ${node.lng.toFixed(5)}
                </div>

                <div style="font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.05em; color: #475569; margin-bottom: 4px;">
                    Connecting Road Branches (${branches.length}):
                </div>
                <div style="max-height: 120px; overflow-y: auto; margin-bottom: 10px; padding-right: 4px;">
                    ${branchesListHtml}
                </div>

                <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 4px;">
                    <button onclick="setEndpointFromMap(${node.id}, 'source')" style="padding: 5px 4px; font-size: 10.5px; font-weight: 600; background: #10b981; color: white; border: none; border-radius: 4px; cursor: pointer;">
                        Set Origin
                    </button>
                    <button onclick="setEndpointFromMap(${node.id}, 'dest')" style="padding: 5px 4px; font-size: 10.5px; font-weight: 600; background: #f43f5e; color: white; border: none; border-radius: 4px; cursor: pointer;">
                        Set Dest
                    </button>
                    <button onclick="addWaypointWithId(${node.id})" style="padding: 5px 4px; font-size: 10.5px; font-weight: 600; background: #8b5cf6; color: white; border: none; border-radius: 4px; cursor: pointer;">
                        + Waypoint
                    </button>
                </div>
            </div>
        `, { maxWidth: 300 });
    });
}

/**
 * Re-render markers when endpoints or waypoints change
 */
function updateMarkerHighlights() {
    if (mapData && mapData.nodes) {
        renderLandmarkMarkers(mapData.nodes);
    }
}

/**
 * Helper to get canonical edge key
 */
function getEdgeKey(u, v) {
    return u < v ? `${u}-${v}` : `${v}-${u}`;
}

/**
 * Set Origin or Destination from Map popup
 */
window.setEndpointFromMap = function(nodeId, type) {
    if (!mapData) return;
    const node = mapData.nodes.find(n => n.id === nodeId);
    if (!node) return;

    if (type === 'source') {
        document.getElementById('select-source').value = nodeId;
        document.getElementById('search-source').value = `[${node.id}] ${node.name}`;
    } else {
        document.getElementById('select-destination').value = nodeId;
        document.getElementById('search-destination').value = `[${node.id}] ${node.name}`;
    }

    updateMarkerHighlights();
    map.closePopup();
};

/**
 * Swap origin and destination endpoints
 */
window.swapEndpoints = function() {
    const srcHidden = document.getElementById('select-source');
    const dstHidden = document.getElementById('select-destination');
    const srcInput = document.getElementById('search-source');
    const dstInput = document.getElementById('search-destination');

    const tempVal = srcHidden.value;
    const tempText = srcInput.value;

    srcHidden.value = dstHidden.value;
    srcInput.value = dstInput.value;

    dstHidden.value = tempVal;
    dstInput.value = tempText;

    updateMarkerHighlights();
};

/**
 * Toggle routing mode: 'time', 'distance', or 'eco'
 */
window.setRoutingMode = function(mode) {
    routingMode = mode;
    document.getElementById('btn-mode-time').classList.toggle('active', mode === 'time');
    document.getElementById('btn-mode-distance').classList.toggle('active', mode === 'distance');
    document.getElementById('btn-mode-eco').classList.toggle('active', mode === 'eco');

    const labels = {
        time: 'Fastest Travel Time (Traffic-Aware)',
        distance: 'Shortest Physical Distance (Ignores Congestion)',
        eco: 'Eco-Friendly & Fuel Saver (Minimizes Stop-and-Go Idling)'
    };
    showToast(`Mode switched to: ${labels[mode] || mode}`);
};

/**
 * Add a dynamic multi-stop waypoint selector (TSP Multi-Stop Routing)
 */
window.addWaypoint = function() {
    addWaypointWithId(null);
};

window.addWaypointWithId = function(defaultId) {
    if (!mapData) return;
    const wpId = Date.now() + Math.floor(Math.random() * 1000);
    const container = document.getElementById('waypoint-container');

    const card = document.createElement('div');
    card.className = 'waypoint-card';
    card.id = `wp-card-${wpId}`;

    const wpIndex = activeWaypoints.length + 1;

    const select = document.createElement('select');
    select.className = 'input-select';
    select.style.flex = '1';
    select.style.fontSize = '0.8rem';
    select.style.padding = '5px 8px';
    select.style.background = '#0f172a';
    select.style.color = '#e2e8f0';
    select.style.border = '1px solid rgba(139, 92, 246, 0.4)';

    mapData.nodes.forEach(node => {
        select.add(new Option(`[${node.id}] ${node.name} (${node.category})`, node.id));
    });

    if (defaultId !== null && defaultId !== undefined) {
        select.value = defaultId;
    } else {
        select.value = '7'; // default to Banjara Hills
    }

    select.addEventListener('change', () => {
        updateMarkerHighlights();
    });

    const labelSpan = document.createElement('span');
    labelSpan.style.fontFamily = 'var(--font-mono)';
    labelSpan.style.fontSize = '0.75rem';
    labelSpan.style.fontWeight = '700';
    labelSpan.style.color = '#c084fc';
    labelSpan.textContent = `W${wpIndex}:`;

    const removeBtn = document.createElement('button');
    removeBtn.className = 'btn-remove-wp';
    removeBtn.innerHTML = '&times;';
    removeBtn.title = 'Remove Stop';
    removeBtn.onclick = () => {
        card.remove();
        activeWaypoints = activeWaypoints.filter(w => w.id !== wpId);
        updateMarkerHighlights();
    };

    card.appendChild(labelSpan);
    card.appendChild(select);
    card.appendChild(removeBtn);
    container.appendChild(card);

    activeWaypoints.push({ id: wpId, selectEl: select });
    updateMarkerHighlights();
    map.closePopup();
};

/**
 * Select corridor in traffic simulation slider
 */
window.selectCorridor = function(u, v) {
    const select = document.getElementById('select-corridor');
    const targetVal = `${u}-${v}`;
    const reverseVal = `${v}-${u}`;

    for (let opt of select.options) {
        if (opt.value === targetVal || opt.value === reverseVal) {
            select.value = opt.value;
            break;
        }
    }
    updateSliderFromSelectedCorridor();
    map.closePopup();
};

/**
 * Sync slider value to currently selected corridor
 */
function updateSliderFromSelectedCorridor() {
    const select = document.getElementById('select-corridor');
    if (!select.value) return;

    const [u, v] = select.value.split('-').map(Number);
    const key = getEdgeKey(u, v);
    let val = simulatedTraffic.get(key);

    if (val === undefined && mapData) {
        const edge = mapData.edges.find(e => 
            (e.source === u && e.target === v) || (e.source === v && e.target === u)
        );
        val = edge ? edge.congestion : 1.0;
    }

    val = val || 1.0;
    const slider = document.getElementById('traffic-slider');
    slider.value = val.toFixed(1);
    updateTrafficBadge(val);
}

/**
 * Update the traffic badge label & style
 */
function updateTrafficBadge(val) {
    const badge = document.getElementById('traffic-badge');
    const num = parseFloat(val);
    let statusText = 'Clear';
    let badgeColor = '#10b981';

    if (num >= 2.5) {
        statusText = 'Gridlock';
        badgeColor = '#f43f5e';
    } else if (num >= 1.8) {
        statusText = 'Peak Hour';
        badgeColor = '#fb7185';
    } else if (num >= 1.3) {
        statusText = 'Moderate';
        badgeColor = '#f59e0b';
    }

    badge.textContent = `${num.toFixed(1)}x (${statusText})`;
    badge.style.color = badgeColor;
}

/**
 * Apply traffic slider change to active corridor
 * Also triggers mid-journey dynamic reroute if driving simulation is active!
 */
function onTrafficSliderInput(e) {
    const val = parseFloat(e.target.value);
    updateTrafficBadge(val);

    const select = document.getElementById('select-corridor');
    if (!select.value) return;

    const [u, v] = select.value.split('-').map(Number);
    const key = getEdgeKey(u, v);
    simulatedTraffic.set(key, val);

    // Re-render road network colors
    renderRoadNetwork(mapData);

    // Mid-journey Live Recalculation Check
    if (isSimulating && currentRouteData) {
        triggerDebouncedReroute('Traffic Congestion Adjusted Mid-Drive');
    }
}

/**
 * Preset buttons for traffic simulator
 */
window.setTrafficPreset = function(factor) {
    const slider = document.getElementById('traffic-slider');
    slider.value = factor.toFixed(1);
    slider.dispatchEvent(new Event('input'));
    showToast(`Applied ${factor.toFixed(1)}x traffic congestion to selected corridor.`);
};

window.resetAllTraffic = function() {
    simulatedTraffic.clear();
    updateSliderFromSelectedCorridor();
    renderRoadNetwork(mapData);
    showToast('Reset all traffic conditions back to base dataset.');
};

/**
 * Calculate optimal route by invoking backend API
 */
window.computeRoute = async function() {
    const srcId = parseInt(document.getElementById('select-source').value);
    const dstId = parseInt(document.getElementById('select-destination').value);
    const checkAlts = document.getElementById('check-alternatives').checked;

    if (isNaN(srcId) || isNaN(dstId)) {
        showToast('Please select valid origin and destination locations.');
        return;
    }

    if (srcId === dstId) {
        showToast('Origin and Destination cannot be the same landmark.');
        return;
    }

    // Stop existing simulation if active
    stopDriveSimulation();

    const waypoints = activeWaypoints.map(w => parseInt(w.selectEl.value)).filter(id => !isNaN(id));

    const trafficUpdates = [];
    simulatedTraffic.forEach((congestion, key) => {
        const [u, v] = key.split('-').map(Number);
        trafficUpdates.push({ u, v, congestion });
    });

    const btn = document.getElementById('btn-compute');
    const spinner = document.getElementById('loading-spinner');
    const btnText = document.getElementById('btn-compute-text');

    btn.disabled = true;
    spinner.style.display = 'inline-block';
    btnText.textContent = 'Calculating C Engine Route...';

    try {
        const response = await fetch('/api/route', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                source: srcId,
                destination: dstId,
                mode: routingMode,
                waypoints: waypoints,
                alternatives: checkAlts,
                traffic_updates: trafficUpdates
            })
        });

        const data = await response.json();

        if (!response.ok || !data.success) {
            throw new Error(data.error || 'Failed to compute route.');
        }

        currentRouteData = data;
        displayRouteResults(data);

        // Prepare simulation path structures
        prepareDriveSimulation(data.route);
    } catch (err) {
        showToast(err.message);
    } finally {
        btn.disabled = false;
        spinner.style.display = 'none';
        btnText.textContent = 'Compute Optimal Route';
    }
};

/**
 * Extracts the full curved road geometry coordinates along a list of route nodes.
 */
function getRouteDetailedLatLngs(nodes) {
    if (!nodes || nodes.length < 2) return (nodes || []).map(n => [n.lat, n.lng]);
    const fullLatLngs = [];
    for (let i = 0; i < nodes.length - 1; i++) {
        const uId = nodes[i].id;
        const vId = nodes[i + 1].id;
        const edge = mapData && mapData.edges && mapData.edges.find(e => 
            (e.source === uId && e.target === vId) || (e.source === vId && e.target === uId)
        );
        let segmentPts;
        if (edge && edge.pathCoordinates && edge.pathCoordinates.length > 0) {
            let pts = edge.pathCoordinates.map(pt => pt[0] > 50 ? [pt[1], pt[0]] : [pt[0], pt[1]]);
            if (edge.source !== uId) {
                pts = pts.slice().reverse();
            }
            segmentPts = pts;
        } else {
            segmentPts = [
                [nodes[i].lat, nodes[i].lng],
                [nodes[i + 1].lat, nodes[i + 1].lng]
            ];
        }
        if (i > 0 && fullLatLngs.length > 0) {
            segmentPts.slice(1).forEach(p => fullLatLngs.push(p));
        } else {
            segmentPts.forEach(p => fullLatLngs.push(p));
        }
    }
    return fullLatLngs;
}

/**
 * Render calculated route on the map and populate Route Summary & Analytics HUD
 */
function displayRouteResults(data) {
    routeLayerGroup.clearLayers();
    document.getElementById('results-card').style.display = 'block';

    const primary = data.route;

    // 1. Draw Alternative Routes (Amber Dashed Polylines)
    const altsContainer = document.getElementById('alternatives-container');
    altsContainer.innerHTML = '';

    if (data.alternatives && data.alternatives.length > 0) {
        document.getElementById('alts-heading').style.display = 'block';

        data.alternatives.forEach((alt, idx) => {
            const latlngs = getRouteDetailedLatLngs(alt.nodes);

            const altLine = L.polyline(latlngs, {
                color: '#f59e0b',
                weight: 5,
                dashArray: '10, 8',
                opacity: 0.90
            }).addTo(routeLayerGroup);

            altLine.bindPopup(`
                <div style="font-family: var(--font-sans, sans-serif); font-size: 12px;">
                    <strong style="color: #f59e0b; font-size: 13px;">Alternative Route #${idx + 1} (2.5x Penalty)</strong><br>
                    <span>Distance: <b>${alt.total_distance_km.toFixed(2)} km</b></span><br>
                    <span>Est. Time: <b>${alt.estimated_time_minutes.toFixed(1)} mins</b></span><br>
                    <span>Waypoints: <b>${alt.node_count} intersections</b></span>
                </div>
            `);

            const altCard = document.createElement('div');
            altCard.className = 'alt-route-card';
            altCard.innerHTML = `
                <div>
                    <strong style="color: #f59e0b;">Alternative #${idx + 1} (Bypasses Main Corridor)</strong><br>
                    <span style="color: #94a3b8; font-size: 0.76rem;">${alt.total_distance_km.toFixed(2)} km &bull; ${alt.estimated_time_minutes.toFixed(1)} mins</span>
                </div>
                <span style="font-size: 0.72rem; color: #fbbf24; font-weight: 600;">Focus &rarr;</span>
            `;
            altCard.onclick = () => {
                map.fitBounds(altLine.getBounds(), { padding: [50, 50] });
                altLine.openPopup();
            };
            altsContainer.appendChild(altCard);
        });
    } else {
        document.getElementById('alts-heading').style.display = 'none';
    }

    // 2. Draw Primary Route as a Vibrant Glowing Blue Polyline (Multi-Layer Neon Halo)
    redrawPrimaryRoutePolyline(primary);

    // 3. Update HUD statistics and natural language turn-by-turn itinerary
    updateHUDAndItinerary(primary);

    // Re-render markers
    updateMarkerHighlights();
}

/**
 * Redraw the glowing blue polyline layers for the primary route
 */
function redrawPrimaryRoutePolyline(route) {
    if (primaryGlowLayer) routeLayerGroup.removeLayer(primaryGlowLayer);
    if (primaryCoreLayer) routeLayerGroup.removeLayer(primaryCoreLayer);

    const primaryLatLngs = getRouteDetailedLatLngs(route.nodes);

    // Outer neon ambient glow
    L.polyline(primaryLatLngs, {
        color: '#0284c7',
        weight: 22,
        opacity: 0.22,
        lineCap: 'round',
        lineJoin: 'round'
    }).addTo(routeLayerGroup);

    // Mid vibrant glow polyline
    primaryGlowLayer = L.polyline(primaryLatLngs, {
        color: '#38bdf8',
        weight: 12,
        opacity: 0.55,
        lineCap: 'round',
        lineJoin: 'round'
    }).addTo(routeLayerGroup);

    // Core solid high-performance polyline
    primaryCoreLayer = L.polyline(primaryLatLngs, {
        color: '#1d4ed8',
        weight: 5,
        opacity: 1.0,
        lineCap: 'round',
        lineJoin: 'round'
    }).addTo(routeLayerGroup);

    primaryCoreLayer.bindPopup(`
        <div style="font-family: var(--font-sans, sans-serif); font-size: 12px; color: #0f172a;">
            <strong style="color: #2563eb; font-size: 13px;">Optimal Primary Route (${routingMode.toUpperCase()})</strong><br>
            <span>Total Length: <b>${route.total_distance_km.toFixed(2)} km</b></span><br>
            <span>Travel Time: <b>${route.estimated_time_minutes.toFixed(1)} mins</b></span>
        </div>
    `);

    if (!isSimulating) {
        map.fitBounds(primaryCoreLayer.getBounds(), { padding: [60, 60] });
    }
}

/**
 * Update the 4-Card Analytics HUD & Turn-by-Turn Driving Itinerary
 */
function updateHUDAndItinerary(route) {
    const distKm = route.total_distance_km;
    const timeMins = route.estimated_time_minutes;
    const avgSpeedKmH = timeMins > 0 ? (distKm / (timeMins / 60)) : 0;

    let avgCongestion = 1.0;
    if (route.legs && route.legs.length > 0) {
        const totalCong = route.legs.reduce((acc, leg) => acc + (leg.congestion_factor || 1.0), 0);
        avgCongestion = totalCong / route.legs.length;
    }
    const carbonKg = distKm * 0.120 * (1.0 + Math.max(0, avgCongestion - 1.0) * 1.8);

    document.getElementById('stat-distance').textContent = distKm.toFixed(2);
    document.getElementById('stat-time').textContent = timeMins.toFixed(1);
    document.getElementById('stat-speed').textContent = avgSpeedKmH.toFixed(1);
    document.getElementById('stat-co2').textContent = carbonKg.toFixed(2);

    const ecoSub = document.getElementById('stat-eco-savings');
    if (ecoSub) {
        if (routingMode === 'eco') {
            ecoSub.textContent = '🌱 Eco-Optimized Path';
            ecoSub.style.color = '#10b981';
        } else if (routingMode === 'distance') {
            ecoSub.textContent = '📏 Shortest Geometric';
            ecoSub.style.color = '#38bdf8';
        } else {
            ecoSub.textContent = '⚡ Fastest Corridor';
            ecoSub.style.color = '#60a5fa';
        }
    }

    renderTurnByTurnItinerary(route);
}

/**
 * Generate natural language Turn-by-Turn direction steps
 */
function renderTurnByTurnItinerary(route) {
    const itineraryList = document.getElementById('itinerary-list');
    itineraryList.innerHTML = '';

    let cumulativeDist = 0.0;
    let cumulativeTime = 0.0;

    route.legs.forEach((leg, idx) => {
        cumulativeDist += leg.distance_km;
        cumulativeTime += leg.time_minutes;

        let congClass = 'tag-clear';
        let congLabel = 'Clear Flow';
        if (leg.congestion_factor >= 2.2) {
            congClass = 'tag-heavy';
            congLabel = 'Gridlock';
        } else if (leg.congestion_factor >= 1.3) {
            congClass = 'tag-moderate';
            congLabel = 'Moderate Traffic';
        }

        let instruction = '';
        if (idx === 0) {
            instruction = `Depart from <b>${leg.from_name}</b> and head onto <b>${leg.road_name}</b> toward <b>${leg.to_name}</b>.`;
        } else if (idx === route.legs.length - 1) {
            instruction = `Take <b>${leg.road_name}</b> and arrive at your destination: <b>${leg.to_name}</b>.`;
        } else {
            if (leg.road_name.includes('Tank Bund') || leg.road_name.includes('Necklace Road') || leg.road_name.includes('PVNR')) {
                instruction = `Drive along the scenic perimeter corridor on <b>${leg.road_name}</b> toward <b>${leg.to_name}</b>.`;
            } else if (leg.road_name.includes('Flyover') || leg.road_name.includes('Expressway')) {
                instruction = `Elevate onto <b>${leg.road_name}</b> bypassing surface intersections toward <b>${leg.to_name}</b>.`;
            } else {
                instruction = `Continue on <b>${leg.road_name}</b> toward <b>${leg.to_name}</b>.`;
            }
        }

        const step = document.createElement('div');
        step.className = 'itinerary-item';
        step.id = `itinerary-step-${idx}`;
        step.innerHTML = `
            <div class="step-circle">${idx + 1}</div>
            <div class="step-details">
                <div class="step-instruction">${instruction}</div>
                <div class="step-road-name">Road: ${leg.road_name}</div>
                <div class="step-leg-path">${leg.from_name} &rarr; ${leg.to_name}</div>
                <div class="step-stats">
                    <span>${leg.distance_km.toFixed(2)} km &bull; ${leg.time_minutes.toFixed(1)} min</span>
                    <span class="${congClass}">${congLabel} (${leg.congestion_factor.toFixed(1)}x)</span>
                </div>
                <div style="margin-top: 5px; font-size: 0.72rem; color: #94a3b8;">
                    Cumulative: <span class="cumulative-badge">${cumulativeDist.toFixed(2)} km</span> &bull; <span class="cumulative-badge">${cumulativeTime.toFixed(1)} min ETA</span>
                </div>
            </div>
        `;

        step.style.cursor = 'pointer';
        step.addEventListener('click', () => {
            const uNode = route.nodes.find(n => n.id === leg.from_id);
            const vNode = route.nodes.find(n => n.id === leg.to_id);
            if (uNode && vNode) {
                map.fitBounds([
                    [uNode.lat, uNode.lng],
                    [vNode.lat, vNode.lng]
                ], { padding: [80, 80], maxZoom: 16 });
            }
        });

        itineraryList.appendChild(step);
    });
}

// ==========================================================================
// Interactive Drive Simulation & Dynamic Rerouting Engine
// ==========================================================================

/**
 * Calculate forward azimuth / bearing in degrees between two coordinates
 */
function calculateBearing(lat1, lon1, lat2, lon2) {
    const toRad = Math.PI / 180;
    const toDeg = 180 / Math.PI;
    const dLon = (lon2 - lon1) * toRad;
    const y = Math.sin(dLon) * Math.cos(lat2 * toRad);
    const x = Math.cos(lat1 * toRad) * Math.sin(lat2 * toRad) -
              Math.sin(lat1 * toRad) * Math.cos(lat2 * toRad) * Math.cos(dLon);
    let brng = Math.atan2(y, x) * toDeg;
    return (brng + 360) % 360;
}

/**
 * Haversine formula for distance in kilometers
 */
function calculateHaversineKm(lat1, lon1, lat2, lon2) {
    const R = 6371;
    const dLat = (lat2 - lat1) * Math.PI / 180;
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
              Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
              Math.sin(dLon / 2) * Math.sin(dLon / 2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return R * c;
}

/**
 * Convert angle in degrees to compass direction string
 */
function getCompassDirection(bearing) {
    const directions = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
    const idx = Math.round(bearing / 45) % 8;
    return directions[idx];
}

/**
 * Build geometry segments for smooth vehicle interpolation
 */
function buildRouteSegments(route) {
    const segments = [];
    let cumDist = 0;

    for (let i = 0; i < route.nodes.length - 1; i++) {
        const u = route.nodes[i];
        const v = route.nodes[i + 1];
        const leg = (route.legs && route.legs[i]) || {};
        const distKm = leg.distance_km || calculateHaversineKm(u.lat, u.lng, v.lat, v.lng);
        const heading = calculateBearing(u.lat, u.lng, v.lat, v.lng);
        const speed = leg.speed || 40;
        const congestion = leg.congestion_factor || 1.0;
        const roadName = leg.road_name || `Corridor ${u.name} → ${v.name}`;

        segments.push({
            index: i,
            fromNode: u,
            toNode: v,
            distKm: distKm,
            startCumDist: cumDist,
            endCumDist: cumDist + distKm,
            heading: heading,
            speed: speed,
            congestion: congestion,
            roadName: roadName
        });
        cumDist += distKm;
    }

    return { segments, totalDistKm: cumDist };
}

/**
 * Create custom vehicle marker with Auto-Rickshaw SVG & headlights cone
 */
function createVehicleMarker(lat, lng, heading) {
    const icon = L.divIcon({
        className: 'vehicle-marker-container',
        html: `
            <div class="vehicle-marker">
                <div class="vehicle-halo"></div>
                <div class="vehicle-wrapper" id="sim-vehicle-rotator" style="transform: rotate(${heading.toFixed(1)}deg);">
                    <div class="headlight-cone"></div>
                    <svg class="vehicle-svg" viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg">
                        <!-- Auto-Rickshaw Chassis & Wheels -->
                        <rect x="4" y="21" width="3.2" height="6.5" rx="1.6" fill="#0f172a"/>
                        <rect x="24.8" y="21" width="3.2" height="6.5" rx="1.6" fill="#0f172a"/>
                        <rect x="14.4" y="0.8" width="3.2" height="5.5" rx="1.6" fill="#0f172a"/>
                        <!-- Dark Green Lower Body -->
                        <path d="M7 8 C7 4, 25 4, 25 8 L27 24 C27 26.5, 5 26.5, 5 24 Z" fill="#047857" stroke="#064e3b" stroke-width="1"/>
                        <!-- Bright Yellow Auto Canopy -->
                        <path d="M8 7 C8 4.2, 24 4.2, 24 7 L26 18 C26 20.5, 6 20.5, 6 18 Z" fill="#facc15" stroke="#ca8a04" stroke-width="1"/>
                        <!-- Windshield Glass with Reflection -->
                        <path d="M9.5 8.5 L22.5 8.5 L21.5 13.5 L10.5 13.5 Z" fill="#38bdf8" opacity="0.9"/>
                        <!-- Front Center Headlight -->
                        <circle cx="16" cy="3.5" r="2.2" fill="#fef08a"/>
                        <!-- Forward Direction Indicator -->
                        <polygon points="16,0.5 19,4.5 13,4.5" fill="#f59e0b"/>
                    </svg>
                </div>
            </div>
        `,
        iconSize: [44, 44],
        iconAnchor: [22, 22]
    });

    return L.marker([lat, lng], { icon: icon, zIndexOffset: 2000 });
}

/**
 * Prepare simulation structures from a computed route
 */
function prepareDriveSimulation(route) {
    stopDriveSimulation();

    const built = buildRouteSegments(route);
    routeSegments = built.segments;
    totalRouteKm = built.totalDistKm;
    simProgressKm = 0;

    if (routeSegments.length > 0) {
        const start = routeSegments[0];
        if (vehicleMarker) {
            vehicleLayerGroup.removeLayer(vehicleMarker);
        }
        vehicleMarker = createVehicleMarker(start.fromNode.lat, start.fromNode.lng, start.heading);
        vehicleMarker.addTo(vehicleLayerGroup);
    }

    updateSimUI(false, false, 'Ready');
    updateVehiclePositionAtDistance(0);
}

/**
 * Toggle between play and pause
 */
window.toggleDriveSimulation = function() {
    if (!currentRouteData || routeSegments.length === 0) {
        showToast('Please compute a route first before simulating.');
        return;
    }

    if (!isSimulating) {
        startDriveSimulation();
    } else if (isPaused) {
        resumeDriveSimulation();
    } else {
        pauseDriveSimulation();
    }
};

/**
 * Start Drive Simulation
 */
function startDriveSimulation() {
    isSimulating = true;
    isPaused = false;
    lastAnimTimestamp = null;

    if (simProgressKm >= totalRouteKm) {
        simProgressKm = 0;
    }

    updateSimUI(true, false, 'Navigating');
    animFrameId = requestAnimationFrame(simStep);
    showToast(`🚗 Simulation started at ${simSpeed}x speed`);
}

/**
 * Pause Drive Simulation
 */
function pauseDriveSimulation() {
    isPaused = true;
    if (animFrameId) {
        cancelAnimationFrame(animFrameId);
        animFrameId = null;
    }
    updateSimUI(true, true, 'Paused');
}

/**
 * Resume Drive Simulation
 */
function resumeDriveSimulation() {
    isPaused = false;
    lastAnimTimestamp = null;
    updateSimUI(true, false, 'Navigating');
    animFrameId = requestAnimationFrame(simStep);
}

/**
 * Reset Drive Simulation to beginning
 */
window.resetDriveSimulation = function() {
    stopDriveSimulation();
    simProgressKm = 0;
    if (routeSegments.length > 0) {
        updateVehiclePositionAtDistance(0);
    }
    updateSimUI(false, false, 'Ready');
    showToast('Simulation reset to origin.');
};

/**
 * Stop Drive Simulation
 */
function stopDriveSimulation() {
    isSimulating = false;
    isPaused = false;
    if (animFrameId) {
        cancelAnimationFrame(animFrameId);
        animFrameId = null;
    }
    lastAnimTimestamp = null;
}

/**
 * Finish Drive Simulation when destination is reached
 */
function finishDriveSimulation() {
    stopDriveSimulation();
    updateSimUI(false, false, 'Arrived');
    showToast('🏁 You have reached your destination!');
}

/**
 * Set Simulation Speed Multiplier: 1x, 2x, 5x
 */
window.setSimSpeed = function(speed) {
    simSpeed = speed;
    document.querySelectorAll('.speed-btn').forEach(btn => btn.classList.remove('active'));
    const activeBtn = document.getElementById(`speed-${speed}x`);
    if (activeBtn) activeBtn.classList.add('active');
    showToast(`Simulation speed set to ${speed}x`);
};

/**
 * Handle clicking on the progress track to scrub position
 */
window.onProgressTrackClick = function(e) {
    if (!currentRouteData || totalRouteKm <= 0) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const clickX = e.clientX - rect.left;
    const fraction = Math.max(0, Math.min(1, clickX / rect.width));
    simProgressKm = fraction * totalRouteKm;
    updateVehiclePositionAtDistance(simProgressKm);
};

/**
 * Core Animation Frame Loop with requestAnimationFrame
 */
function simStep(timestamp) {
    if (!isSimulating || isPaused) return;

    if (!lastAnimTimestamp) {
        lastAnimTimestamp = timestamp;
    }

    const deltaSec = (timestamp - lastAnimTimestamp) / 1000;
    lastAnimTimestamp = timestamp;

    if (routeSegments.length === 0 || totalRouteKm <= 0) {
        stopDriveSimulation();
        return;
    }

    // Base rate: entire route traverses in ~24 seconds at 1x speed
    const kmPerSec = (totalRouteKm / 24) * simSpeed;
    simProgressKm += kmPerSec * deltaSec;

    if (simProgressKm >= totalRouteKm) {
        simProgressKm = totalRouteKm;
        updateVehiclePositionAtDistance(simProgressKm);
        finishDriveSimulation();
        return;
    }

    updateVehiclePositionAtDistance(simProgressKm);
    animFrameId = requestAnimationFrame(simStep);
}

/**
 * Interpolate coordinates, rotate vehicle, and update live telemetry
 */
function updateVehiclePositionAtDistance(dist) {
    if (routeSegments.length === 0) return;

    // Locate active segment based on cumulative distance
    let activeSeg = routeSegments[routeSegments.length - 1];
    let segIdx = routeSegments.length - 1;

    for (let i = 0; i < routeSegments.length; i++) {
        if (dist <= routeSegments[i].endCumDist) {
            activeSeg = routeSegments[i];
            segIdx = i;
            break;
        }
    }

    const segDist = dist - activeSeg.startCumDist;
    const t = Math.max(0, Math.min(1, segDist / (activeSeg.distKm || 0.001)));

    // Smooth linear coordinate interpolation
    const curLat = activeSeg.fromNode.lat + t * (activeSeg.toNode.lat - activeSeg.fromNode.lat);
    const curLng = activeSeg.fromNode.lng + t * (activeSeg.toNode.lng - activeSeg.fromNode.lng);

    // Update vehicle marker position and rotation
    if (vehicleMarker) {
        vehicleMarker.setLatLng([curLat, curLng]);
        const rotator = document.getElementById('sim-vehicle-rotator');
        if (rotator) {
            rotator.style.transform = `rotate(${activeSeg.heading.toFixed(1)}deg)`;
        }
    }

    // Auto-pan map if vehicle nears map boundary
    if (map && isSimulating && !map.getBounds().pad(-0.15).contains([curLat, curLng])) {
        map.panTo([curLat, curLng], { animate: true, duration: 0.35 });
    }

    // Update Progress Bar UI
    const percent = Math.min(100, (dist / (totalRouteKm || 1)) * 100);
    const bar = document.getElementById('sim-progress-bar');
    if (bar) bar.style.width = `${percent.toFixed(1)}%`;
    const pctText = document.getElementById('sim-progress-percent');
    if (pctText) pctText.textContent = `${Math.round(percent)}%`;
    const distText = document.getElementById('sim-progress-dist');
    if (distText) distText.textContent = `${dist.toFixed(2)} km / ${totalRouteKm.toFixed(2)} km`;

    // Update Live Telemetry HUD
    const telemRoad = document.getElementById('telemetry-road');
    if (telemRoad) telemRoad.textContent = activeSeg.roadName;
    const telemHeading = document.getElementById('telemetry-heading');
    if (telemHeading) telemHeading.textContent = `${Math.round(activeSeg.heading)}° ${getCompassDirection(activeSeg.heading)}`;
    const telemSpeed = document.getElementById('telemetry-speed');
    if (telemSpeed) {
        const effectiveSpeed = Math.round(activeSeg.speed / activeSeg.congestion);
        telemSpeed.textContent = `${effectiveSpeed} km/h`;
    }

    // Highlight current active step in Turn-by-Turn itinerary
    highlightItineraryStep(segIdx);
}

/**
 * Highlight the current driving step in the Turn-by-Turn itinerary
 */
function highlightItineraryStep(activeIdx) {
    const items = document.querySelectorAll('.itinerary-item');
    items.forEach((item, idx) => {
        if (idx === activeIdx) {
            item.style.borderColor = '#38bdf8';
            item.style.backgroundColor = 'rgba(56, 189, 248, 0.12)';
        } else {
            item.style.borderColor = 'rgba(255, 255, 255, 0.06)';
            item.style.backgroundColor = 'rgba(255, 255, 255, 0.025)';
        }
    });
}

/**
 * Update Simulation UI Controls (Button label, icon, badge)
 */
function updateSimUI(simulating, paused, statusLabel) {
    const playBtn = document.getElementById('btn-sim-play');
    const playLabel = document.getElementById('sim-play-label');
    const playIcon = document.getElementById('sim-play-icon');
    const badge = document.getElementById('sim-status-badge');

    if (badge) {
        badge.textContent = statusLabel;
        if (statusLabel === 'Navigating') {
            badge.style.color = '#38bdf8';
            badge.style.background = 'rgba(56, 189, 248, 0.15)';
        } else if (statusLabel === 'Paused') {
            badge.style.color = '#f59e0b';
            badge.style.background = 'rgba(245, 158, 11, 0.15)';
        } else if (statusLabel === 'Arrived') {
            badge.style.color = '#10b981';
            badge.style.background = 'rgba(16, 185, 129, 0.15)';
        } else {
            badge.style.color = '#94a3b8';
            badge.style.background = 'rgba(148, 163, 184, 0.15)';
        }
    }

    if (!playBtn || !playLabel || !playIcon) return;

    if (simulating && !paused) {
        playBtn.className = 'btn-sim-action btn-sim-pause';
        playLabel.textContent = 'Pause Drive';
        playIcon.innerHTML = '<rect x="6" y="4" width="4" height="16"></rect><rect x="14" y="4" width="4" height="16"></rect>';
    } else {
        playBtn.className = 'btn-sim-action btn-sim-play';
        playLabel.textContent = simulating && paused ? 'Resume Drive' : 'Simulate Drive';
        playIcon.innerHTML = '<polygon points="5 3 19 12 5 21 5 3"></polygon>';
    }
}

// ==========================================================================
// Live Recalculation Demonstration (Mid-journey Dynamic Rerouting)
// ==========================================================================

/**
 * Debounce wrapper for dynamic rerouting during slider adjustments
 */
function triggerDebouncedReroute(reason) {
    clearTimeout(rerouteDebounceTimer);
    rerouteDebounceTimer = setTimeout(() => {
        executeDynamicReroute(reason);
    }, 350);
}

/**
 * Execute dynamic route recalculation mid-drive:
 * Diverts the vehicle from its upcoming waypoint node to destination
 */
async function executeDynamicReroute(reasonText = 'Sudden Traffic Spike Ahead') {
    if (!isSimulating || isReroutingInProgress || !currentRouteData || routeSegments.length === 0) return;

    // Locate current segment and upcoming waypoint node
    let curSegIdx = 0;
    for (let i = 0; i < routeSegments.length; i++) {
        if (simProgressKm <= routeSegments[i].endCumDist) {
            curSegIdx = i;
            break;
        }
    }

    const nextNode = routeSegments[curSegIdx].toNode;
    const finalDstId = parseInt(document.getElementById('select-destination').value);

    // If next node is already destination, no reroute required
    if (nextNode.id === finalDstId) return;

    isReroutingInProgress = true;

    // Visual Flash & Alert Banner
    triggerRerouteFlash(reasonText);

    // Prepare updated traffic matrix
    const trafficUpdates = [];
    simulatedTraffic.forEach((congestion, key) => {
        const [u, v] = key.split('-').map(Number);
        trafficUpdates.push({ u, v, congestion });
    });

    try {
        const response = await fetch('/api/route', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                source: nextNode.id,
                destination: finalDstId,
                mode: routingMode,
                waypoints: [],
                alternatives: false,
                traffic_updates: trafficUpdates
            })
        });

        const data = await response.json();
        if (!response.ok || !data.success || !data.route) {
            throw new Error('Reroute computation failed');
        }

        const newSubRoute = data.route;

        // Splice traversed path with new diverted sub-route
        const traversedNodes = currentRouteData.route.nodes.slice(0, curSegIdx + 2);
        const traversedLegs = currentRouteData.route.legs.slice(0, curSegIdx + 1);

        const splicedNodes = traversedNodes.concat(newSubRoute.nodes.slice(1));
        const splicedLegs = traversedLegs.concat(newSubRoute.legs);

        const newTotalDist = splicedLegs.reduce((acc, l) => acc + l.distance_km, 0);
        const newTotalTime = splicedLegs.reduce((acc, l) => acc + l.time_minutes, 0);

        currentRouteData.route.nodes = splicedNodes;
        currentRouteData.route.legs = splicedLegs;
        currentRouteData.route.total_distance_km = newTotalDist;
        currentRouteData.route.estimated_time_minutes = newTotalTime;
        currentRouteData.route.node_count = splicedNodes.length;

        // Rebuild route segments from spliced route
        const built = buildRouteSegments(currentRouteData.route);
        routeSegments = built.segments;
        totalRouteKm = built.totalDistKm;

        // Redraw glowing blue polyline with new diverted path
        redrawPrimaryRoutePolyline(currentRouteData.route);

        // Update HUD and Itinerary
        updateHUDAndItinerary(currentRouteData.route);

        const diversionRoad = newSubRoute.legs[0]?.road_name || 'alternative corridor';
        showToast(`⚡ Live Reroute Applied: Diverted via ${diversionRoad}!`);
    } catch (err) {
        console.warn('Dynamic reroute error:', err);
    } finally {
        isReroutingInProgress = false;
    }
}

/**
 * Trigger visual flash on map and alert banner
 */
function triggerRerouteFlash(message) {
    const banner = document.getElementById('reroute-banner');
    const bannerText = document.getElementById('reroute-banner-text');
    const flash = document.getElementById('flash-overlay');

    if (bannerText) {
        bannerText.textContent = `⚡ ALERT: ${message}! Recalculating route...`;
    }

    if (banner) {
        banner.classList.add('show');
        setTimeout(() => { banner.classList.remove('show'); }, 3500);
    }

    if (flash) {
        flash.classList.add('active');
        setTimeout(() => { flash.classList.remove('active'); }, 700);
    }
}

/**
 * One-click demonstration for Sudden Bottleneck & Dynamic Rerouting
 * Simulates a gridlock on Tank Bund Road (East Shore) and diverts vehicle via PVNR Marg / Necklace Road (West Shore)
 */
window.triggerSuddenTrafficDemo = function() {
    if (!currentRouteData) {
        showToast('Please compute a route first.');
        return;
    }

    // If simulation not yet started, auto-start it
    if (!isSimulating) {
        startDriveSimulation();
    }

    // Simulate sudden gridlock on Tank Bund Road (20 <-> 21) or Ameerpet (9 <-> 10)
    selectCorridor(20, 21);
    const slider = document.getElementById('traffic-slider');
    slider.value = '2.9';

    const key = getEdgeKey(20, 21);
    simulatedTraffic.set(key, 2.9);
    updateTrafficBadge(2.9);
    renderRoadNetwork(mapData);

    // Trigger instant dynamic reroute!
    executeDynamicReroute('Sudden Gridlock on Tank Bund Road (2.9x)');
};

/**
 * Toast notification banner
 */
function showToast(message) {
    const toast = document.getElementById('app-toast');
    if (!toast) return;
    toast.textContent = message;
    toast.style.display = 'block';
    setTimeout(() => { toast.style.display = 'none'; }, 4000);
}

/**
 * Setup global event listeners
 */
function setupEventListeners() {
    const corridorSelect = document.getElementById('select-corridor');
    if (corridorSelect) {
        corridorSelect.addEventListener('change', updateSliderFromSelectedCorridor);
    }

    const slider = document.getElementById('traffic-slider');
    if (slider) {
        slider.addEventListener('input', onTrafficSliderInput);
    }
}
