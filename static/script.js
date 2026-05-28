      // ── Config ────────────────────────────────────────────────────────────
        //const SERVER_IP   = "10.22.148.129";
        const SERVER_IP   = "192.168.100.65";
        //const SERVER_IP   = "172.22.10.53";
        const SERVER_PORT = "8000";
        const API_BASE    = `http://${SERVER_IP}:${SERVER_PORT}`;
        const WS_URL      = `ws://${SERVER_IP}:${SERVER_PORT}/ws`;
        const NUM_OUTLETS = 3;
        const MAX_POINTS  = 30;
 
        // ── State ─────────────────────────────────────────────────────────────
        let outletStates  = [false, false, false];  // default disabled
        let currentLimits = [7.5,  7.5,  7.5 ];
        
        let pendingLimits = [7.5,  7.5,  7.5 ];
        let limitPending  = [false, false, false];
        let hourlyData    = [[], [], []];   // {x: hour 0-23, y: kWh} one point per hour
        let lastHourSaved = [-1, -1, -1];  // last hour we recorded for each outlet
        let charts_daily  = [];

        let limitSavedAt = [0, 0, 0];
 
        // ── Logging ───────────────────────────────────────────────────────────
        function log(msg, type = '') {
            const el = document.getElementById('log');
            const line = document.createElement('div');
            line.className = `log-line ${type}`;
            line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
            el.prepend(line);
            if (el.children.length > 40) el.lastChild.remove();
        }
 
        // ── Populate current-limit dropdowns ──────────────────────────────────
        function buildSelects() {
            for (let i = 0; i < NUM_OUTLETS; i++) {
                const sel = document.getElementById(`limit-${i}`);
                for (let v = 0.5; v <= 15; v += 0.5) {
                    const opt = document.createElement('option');
                    opt.value = v.toFixed(1);
                    opt.textContent = `${v.toFixed(1)} A`;
                    if (v === 7.5) opt.selected = true;
                    sel.appendChild(opt);
                }
            }
        }
 
        // ── Build Chart.js charts ─────────────────────────────────────────────
        
        function buildCharts() {
    const colors = [
        { border: 'rgb(0,0,0)',     bg: 'rgba(0,0,0,0.1)'      },
        { border: 'rgb(0,100,200)', bg: 'rgba(0,100,200,0.1)'  },
        { border: 'rgb(200,0,0)',   bg: 'rgba(200,0,0,0.1)'    }
    ];

    for (let i = 0; i < NUM_OUTLETS; i++) {

        // ── Daily chart (hourly points, x = 0..24) ──────────────────────────
        const ctxD = document.getElementById(`chart-daily-${i}`).getContext('2d');
        charts_daily[i] = new Chart(ctxD, {
            type: 'line',
            data: {
                datasets: [{
                    label: `Outlet ${i+1} Daily (kWh)`,
                    data: [],
                    borderColor: colors[i].border,
                    backgroundColor: colors[i].bg,
                    fill: true,
                    tension: 0.3,
                    pointRadius: 4,
                }]
            },
            options: {
                animation: false,
                parsing: false,
                scales: {
                    x: {
                        type: 'linear',
                        min: 0,
                        max: 24,
                        ticks: {
                            stepSize: 1,
                            callback: v => `${String(v).padStart(2,'0')}:00`
                        },
                        title: { display: true, text: 'Hour of Day' }
                    },
                    y: {
                        beginAtZero: true,
                        title: { display: true, text: 'Energy (kWh)' },
                        ticks: { callback: v => v.toFixed(4) }
                    }
                },
                plugins: {
                    tooltip: {
                        callbacks: {
                            title: items => `${String(items[0].parsed.x).padStart(2,'0')}:00`,
                            label: ctx  => `${ctx.parsed.y.toFixed(6)} kWh`
                        }
                    }
                }
            }
        });
    }
}

        // ── Toggle outlet on/off ──────────────────────────────────────────────
        function toggleOutlet(index) {
            outletStates[index] = !outletStates[index];
            const btn = document.getElementById(`btn-${index}`);
            if (outletStates[index]) {
                btn.innerText = 'Outlet Enabled';
                btn.style.backgroundColor = '';
                btn.style.color = '';
            } else {
                btn.innerText = 'Outlet Disabled';
                btn.style.backgroundColor = '#f44336';
                btn.style.color = '#fff';
            }
            sendSettings();
            log(`Outlet ${index + 1} turned ${outletStates[index] ? 'ON' : 'OFF'}`,
                outletStates[index] ? 'ok' : 'err');
        }

        // render outlet button to match a given true/false state
        function renderOutletButton(index, enabled) {
            const btn = document.getElementById(`btn-${index}`);
            if (enabled) {
                btn.innerText = 'Outlet Enabled';
                btn.style.backgroundColor = '';
                btn.style.color = '';
            } else {
                btn.innerText = 'Outlet Disabled';
                btn.style.backgroundColor = '#f44336';
                btn.style.color = '#fff';
            }
        }

        // update present limit label
        function renderPresentLimit(index, value) {
            document.getElementById(`present-limit-${index}`).textContent =
                parseFloat(value).toFixed(1) + ' A';
        }

        //  dropdown changed — mark as pending, do NOT call sendSettings
        function onLimitChange(index, value) {
            pendingLimits[index] = parseFloat(value);
            limitPending[index]  = (pendingLimits[index] !== currentLimits[index]);

            const sel     = document.getElementById(`limit-${index}`);
            const saveBtn = document.getElementById(`save-btn-${index}`);

            if (limitPending[index]) {
                sel.classList.add('pending');
                saveBtn.disabled = false;
                log(`Outlet ${index + 1}: ${value} A selected — press Save to apply`, '');
            } else {
                sel.classList.remove('pending');
                saveBtn.disabled = true;
            }
        }

        // Save button pressed — commit pending limit and send
        async function saveLimit(index) {
            currentLimits[index] = pendingLimits[index];
            
            limitPending[index]  = false;

            renderPresentLimit(index, currentLimits[index]);
            document.getElementById(`limit-${index}`).classList.remove('pending');
            document.getElementById(`save-btn-${index}`).disabled = true;

            await sendSettings();
            log(`Outlet ${index + 1} current limit saved → ${currentLimits[index].toFixed(1)} A`, 'ok');
        }
 
        // ── POST settings to server ───────────────────────────────────────────
        async function sendSettings() {
            try {
                const res = await fetch(`${API_BASE}/user_outlet_input`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        outlet_on_off:        outletStates,
                        outlet_current_limit: currentLimits
                    })
                });
                if (res.ok) {
                    log('Settings posted to server', 'ok');
                } else {
                    log(`Settings POST failed: ${res.status}`, 'err');
                }
            } catch (e) {
                log(`Settings POST error: ${e.message}`, 'err');
            }
        }



        //  apply outlet on/off and limit state from server into UI
        function applyServerState(data) {
            if (Array.isArray(data.outlet_on_off)) {
                for (let i = 0; i < NUM_OUTLETS; i++) {
                    const s = !!data.outlet_on_off[i];
                    if (outletStates[i] !== s) {
                        outletStates[i] = s;
                        renderOutletButton(i, s);
                    }
                }
            }
            if (Array.isArray(data.outlet_current_limit)) {
                for (let i = 0; i < NUM_OUTLETS; i++) {
                    const lim = parseFloat(data.outlet_current_limit[i]);
                    if (!isNaN(lim)){
                        currentLimits[i] = lim;
                        renderPresentLimit(i, lim);
                        if (!limitPending[i]) {
                            document.getElementById(`limit-${i}`).value = lim.toFixed(1);
                            pendingLimits[i] = lim;
                        }
                    }
                }
            }
        }

 // ── Apply energy data to UI and chart ────────────────────────────────

 function applyEnergyData(data) {
    const now  = new Date();
    const hour = now.getHours();  // 0-23
    document.getElementById('last-update').textContent = now.toLocaleTimeString();

    applyServerState(data);

    let totalPower = 0;

    for (let i = 0; i < NUM_OUTLETS; i++) {
        const interval = data.interval_eng_Wh?.[i] ?? 0;  // kWh per 5-min
        const daily    = data.daily_eng_consum?.[i] ?? 0;  // cumulative kWh today

        document.getElementById(`rt-${i}`).textContent    = interval.toFixed(6);
        document.getElementById(`daily-${i}`).textContent = daily.toFixed(6);
        totalPower += interval;

        // ── Midnight reset: clear chart if server daily value dropped to ~0 ──
    if (daily < 0.000001 && lastHourSaved[i] !== -1) {
        hourlyData[i] = [];
        lastHourSaved[i] = -1;
        charts_daily[i].data.datasets[0].data = [];
        charts_daily[i].update('none');
    }

    // ── Midnight reset: clear chart if browser clock just hit hour 0 ─────
    if (hour === 0 && lastHourSaved[i] !== -1 && lastHourSaved[i] !== 0) {
        hourlyData[i] = [];
        lastHourSaved[i] = -1;
        charts_daily[i].data.datasets[0].data = [];
        charts_daily[i].update('none');
    }
    
        // ── Daily chart: record one point per hour ───────────────────────────
       if (hour !== lastHourSaved[i]) {
    // Find the cumulative total at the END of the previous hour
    const prevPoint = hourlyData[i].find(p => p.x === lastHourSaved[i]);
    const prevTotal = prevPoint ? prevPoint._cumulative : 0;

    // This hour's consumption = current cumulative minus previous hour's cumulative
    const hourlyConsumption = daily - prevTotal;

    // Store the point, carrying _cumulative so next hour can subtract from it
    hourlyData[i] = hourlyData[i].filter(p => p.x !== hour);
    hourlyData[i].push({ x: hour, y: hourlyConsumption, _cumulative: daily });
    hourlyData[i].sort((a, b) => a.x - b.x);

    lastHourSaved[i] = hour;
    charts_daily[i].data.datasets[0].data = hourlyData[i];
    charts_daily[i].update('none');
}
    }

    document.getElementById('total-power').textContent =
        `${totalPower.toFixed(6)} kWh`;
}


        // ── WebSocket (primary) ───────────────────────────────────────────────
        function connectWS() {
            const ws   = new WebSocket(WS_URL);
            const pill = document.getElementById('ws-pill');
 
            ws.onopen = () => {
                pill.textContent = '● LIVE';
                pill.classList.add('connected');
                log('WebSocket connected', 'ok');
            };
 
            ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    applyEnergyData(data);
                } catch (e) {
                    log(`WS parse error: ${e.message}`, 'err');
                }
            };
 
            ws.onclose = () => {
                pill.textContent = '● DISCONNECTED';
                pill.classList.remove('connected');
                log('WebSocket closed — reconnecting in 3 s…', 'err');
                setTimeout(connectWS, 3000);
            };
 
            ws.onerror = () => log('WebSocket error', 'err');
        }
 
        // ── REST polling fallback (every 2 s) ─────────────────────────────────
        async function pollEnergy() {
            try {
                const res = await fetch(`${API_BASE}/energy_data`);
                if (res.ok) applyEnergyData(await res.json());
            } catch (_) {}
        }
 
        // ── Init ──────────────────────────────────────────────────────────────
        buildSelects();
        buildCharts();
        // render initial disabled state and present limits
        for (let i = 0; i < NUM_OUTLETS; i++) {
            renderOutletButton(i, false);
            renderPresentLimit(i, currentLimits[i]);
        }
        sendSettings();
        log('Page loaded — posting default settings…');
        connectWS();
        setInterval(pollEnergy, 2000);