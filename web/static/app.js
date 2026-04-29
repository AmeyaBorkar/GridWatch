// Dispatch Simulator web UI — vanilla JS, polls /state at 10 Hz.
'use strict';

const canvas = document.getElementById('map');
const ctx = canvas.getContext('2d');
const statusEl = document.getElementById('status');
const logEl = document.getElementById('log');
const pauseBtn = document.getElementById('pause');
const spawnBtn = document.getElementById('spawn');
const rateEl = document.getElementById('rate');
const rateVal = document.getElementById('rate-val');
const qEl = document.getElementById('q');
const fuzzyEl = document.getElementById('fuzzy');
const matchesEl = document.getElementById('matches');

let lastState = null;
let mapBounds = null;  // { minX, minY, maxX, maxY }
let hoverPos = null;   // { x, y } in canvas-css pixels, or null
const tipEl = document.getElementById('tip');
const UNIT_NAMES = ['Ambulance', 'Fire truck', 'Police cruiser'];
const INC_NAMES = ['medical', 'fire', 'crime'];
const STATION_NAMES = ['Hospital', 'Fire station', 'Police precinct'];
const STATE_NAMES = ['idle', 'enroute', 'on-scene', 'returning'];
const SEV_NAMES = ['', 'low', 'medium', 'high'];

// ---------- canvas sizing ----------
function resizeCanvas() {
  const r = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(100, Math.floor(r.width * dpr));
  canvas.height = Math.max(100, Math.floor(r.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  if (lastState) draw(lastState);
}
window.addEventListener('resize', resizeCanvas);

// ---------- drawing ----------
function computeBounds(state) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  const pts = [...state.nodes, ...state.stations];
  for (const p of pts) {
    if (p.x < minX) minX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.x > maxX) maxX = p.x;
    if (p.y > maxY) maxY = p.y;
  }
  if (!isFinite(minX)) { minX = 0; minY = 0; maxX = 100; maxY = 100; }
  const pad = 40;
  return { minX: minX - pad, minY: minY - pad, maxX: maxX + pad, maxY: maxY + pad };
}

function project(b, x, y) {
  const r = canvas.getBoundingClientRect();
  const sx = r.width / (b.maxX - b.minX);
  const sy = r.height / (b.maxY - b.minY);
  const s = Math.min(sx, sy);
  const offX = (r.width - (b.maxX - b.minX) * s) / 2;
  const offY = (r.height - (b.maxY - b.minY) * s) / 2;
  return [offX + (x - b.minX) * s, offY + (y - b.minY) * s];
}

const UNIT_COLORS = ['#5cff8c', '#ff8a3c', '#5c8fff']; // AMB, FIRE, POLICE
const UNIT_LETTERS = ['A', 'F', 'P'];
const INC_COLORS = ['#5cff8c', '#ff8a3c', '#5c8fff'];

function draw(state) {
  const r = canvas.getBoundingClientRect();
  ctx.clearRect(0, 0, r.width, r.height);
  const b = mapBounds = computeBounds(state);

  const nodeById = new Map(state.nodes.map(n => [n.id, n]));

  // roads
  ctx.lineWidth = 1.4;
  for (const rd of state.roads) {
    const a = nodeById.get(rd.from);
    const c = nodeById.get(rd.to);
    if (!a || !c) continue;
    const [ax, ay] = project(b, a.x, a.y);
    const [cx, cy] = project(b, c.x, c.y);
    ctx.strokeStyle = rd.blocked ? '#ff5c5c' : '#2b3140';
    ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(cx, cy); ctx.stroke();
  }

  // nodes (intersections)
  ctx.fillStyle = '#3a4050';
  for (const n of state.nodes) {
    const [x, y] = project(b, n.x, n.y);
    ctx.beginPath(); ctx.arc(x, y, 2.2, 0, Math.PI * 2); ctx.fill();
  }

  // stations (squares with letter)
  ctx.font = '700 11px ui-monospace, monospace';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  for (const s of state.stations) {
    const [x, y] = project(b, s.x, s.y);
    ctx.fillStyle = UNIT_COLORS[s.type] || '#ccc';
    ctx.fillRect(x - 7, y - 7, 14, 14);
    ctx.fillStyle = '#0a0c10';
    ctx.fillText(['H', 'F', 'P'][s.type] || '?', x, y + 0.5);
  }

  // incidents (pulsing !)
  const t = performance.now() / 300;
  for (const i of state.incidents) {
    if (i.resolved) continue;
    const [x, y] = project(b, i.x, i.y);
    const base = 6 + i.severity * 2;
    const pulse = base + Math.sin(t + i.id) * 2;
    ctx.fillStyle = INC_COLORS[i.type] || '#fc5';
    ctx.globalAlpha = 0.25;
    ctx.beginPath(); ctx.arc(x, y, pulse + 4, 0, Math.PI * 2); ctx.fill();
    ctx.globalAlpha = 1.0;
    ctx.beginPath(); ctx.arc(x, y, pulse, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = '#0a0c10';
    ctx.font = '700 11px ui-monospace, monospace';
    ctx.fillText('!', x, y + 0.5);
  }

  // units (circle with letter)
  for (const u of state.units) {
    const [x, y] = project(b, u.x, u.y);
    const col = UNIT_COLORS[u.type] || '#fff';
    ctx.fillStyle = col;
    ctx.beginPath(); ctx.arc(x, y, 6, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = u.state === 0 ? '#000' : '#fff';
    ctx.lineWidth = 1.2;
    ctx.stroke();
    ctx.fillStyle = '#0a0c10';
    ctx.font = '700 10px ui-monospace, monospace';
    ctx.fillText(UNIT_LETTERS[u.type] || '?', x, y + 0.5);
  }
}

function findHover(state) {
  if (!hoverPos || !mapBounds) return null;
  const radius2 = 12 * 12;
  let best = null;
  let bestD = radius2;
  const consider = (x, y, kind, obj) => {
    const dx = x - hoverPos.x, dy = y - hoverPos.y;
    const d = dx * dx + dy * dy;
    if (d <= bestD) { best = { kind, obj }; bestD = d; }
  };
  for (const u of state.units) {
    const [x, y] = project(mapBounds, u.x, u.y);
    consider(x, y, 'unit', u);
  }
  for (const s of state.stations) {
    const [x, y] = project(mapBounds, s.x, s.y);
    consider(x, y, 'station', s);
  }
  for (const i of state.incidents) {
    if (i.resolved) continue;
    const [x, y] = project(mapBounds, i.x, i.y);
    consider(x, y, 'incident', i);
  }
  return best;
}

function updateTip(state) {
  const h = findHover(state);
  if (!h) { tipEl.hidden = true; return; }
  let title = '', body = '', tag = '';
  if (h.kind === 'unit') {
    const u = h.obj;
    title = `${UNIT_NAMES[u.type] || 'Unit'} #${u.id}`;
    body  = `state: ${STATE_NAMES[u.state] || '?'}`;
    if (u.eta > 0) body += `<br>ETA: ${u.eta.toFixed(1)}s`;
    if (u.incident >= 0) body += `<br>responding: incident #${u.incident}`;
    tag = u.state === 0 ? 'AVL roster · idle pool in quadtree' : 'on a Dijkstra path · skip-list ETA';
  } else if (h.kind === 'station') {
    const s = h.obj;
    title = `${STATION_NAMES[s.type] || 'Station'} ${s.name || ''}`;
    body  = `${s.n_units} units stationed`;
    tag = 'R-tree coverage · BSP partition';
  } else if (h.kind === 'incident') {
    const i = h.obj;
    title = `Incident #${i.id} — ${INC_NAMES[i.type] || '?'}`;
    body  = `severity: ${SEV_NAMES[i.severity] || i.severity}`;
    body += `<br>spawned t=${i.spawn.toFixed(1)}s`;
    body += i.assigned >= 0
      ? `<br>assigned unit #${i.assigned}`
      : '<br>pending — DEPQ awaiting unit';
    tag = i.assigned >= 0 ? 'Fibonacci-heap Dijkstra found this unit' : 'sitting in severity DEPQ';
  }
  tipEl.innerHTML = `<div class="tip-title">${title}</div>${body}<div class="tip-tag">${tag}</div>`;
  tipEl.hidden = false;
  const r = canvas.getBoundingClientRect();
  const wrap = canvas.parentElement.getBoundingClientRect();
  let tx = hoverPos.x + 14;
  let ty = hoverPos.y + 14;
  const tw = tipEl.offsetWidth, th = tipEl.offsetHeight;
  if (tx + tw > r.width - 8) tx = hoverPos.x - tw - 14;
  if (ty + th > r.height - 8) ty = hoverPos.y - th - 14;
  tipEl.style.left = (tx) + 'px';
  tipEl.style.top  = (ty) + 'px';
}

canvas.addEventListener('mousemove', (e) => {
  const r = canvas.getBoundingClientRect();
  hoverPos = { x: e.clientX - r.left, y: e.clientY - r.top };
  if (lastState) updateTip(lastState);
});
canvas.addEventListener('mouseleave', () => { hoverPos = null; tipEl.hidden = true; });

// ---------- HUD ----------
function updateHud(state) {
  const m = state.metrics;
  document.getElementById('m-time').textContent = state.time.toFixed(1);
  document.getElementById('m-total').textContent = m.total;
  document.getElementById('m-resolved').textContent = m.resolved;
  document.getElementById('m-pending').textContent = m.pending;
  document.getElementById('m-idle').textContent = m.idle_units;
  document.getElementById('m-active').textContent = m.active_units;
  document.getElementById('m-avg').textContent = m.avg_resp.toFixed(2) + 's';
  document.getElementById('m-roadc').textContent = m.road_components + ' comp';
  document.getElementById('m-dispatch').textContent = m.dispatch_calls + ' calls';
  document.getElementById('m-pq').textContent = m.pq_ops;
  document.getElementById('m-huff').textContent = m.huffman_ratio.toFixed(2);
  document.getElementById('m-sa').textContent = m.sa_suffixes;
  document.getElementById('m-incidents-tree').textContent = m.total + ' / ' + (m.idle_units + m.active_units);
  logEl.textContent = state.log_tail || '';
  logEl.scrollTop = logEl.scrollHeight;
  pauseBtn.textContent = state.paused ? 'Resume' : 'Pause';
  pauseBtn.classList.toggle('active', state.paused);
}

// ---------- polling ----------
async function poll() {
  try {
    const res = await fetch('/state');
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const state = await res.json();
    if (state.error) { statusEl.textContent = state.error; statusEl.className = 'status err'; return; }
    lastState = state;
    statusEl.textContent = 'ok';
    statusEl.className = 'status ok';
    draw(state); updateHud(state); updateTip(state);
  } catch (e) {
    statusEl.textContent = 'offline: ' + e.message;
    statusEl.className = 'status err';
  }
}

// ---------- controls ----------
async function sendControl(body) {
  const res = await fetch('/control', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (res.ok) { const s = await res.json(); lastState = s; draw(s); updateHud(s); }
}

pauseBtn.addEventListener('click', () => sendControl({ paused: !(lastState && lastState.paused) }));
spawnBtn.addEventListener('click', () => sendControl({ force_spawn: true }));
rateEl.addEventListener('input', () => {
  rateVal.textContent = parseFloat(rateEl.value).toFixed(2);
  sendControl({ spawn_rate: parseFloat(rateEl.value) });
});

// ---------- search / autocomplete ----------
let searchTimer = 0;
let selectedIdx = -1;

function renderMatches(items) {
  matchesEl.innerHTML = '';
  if (!items.length) { matchesEl.classList.remove('open'); return; }
  items.forEach((m, idx) => {
    const li = document.createElement('li');
    li.textContent = m;
    if (idx === selectedIdx) li.classList.add('sel');
    li.addEventListener('mousedown', (e) => { e.preventDefault(); qEl.value = m; matchesEl.classList.remove('open'); });
    matchesEl.appendChild(li);
  });
  matchesEl.classList.add('open');
}

async function runSearch() {
  const q = qEl.value.trim();
  if (!q) { matchesEl.classList.remove('open'); return; }
  const url = `/search?q=${encodeURIComponent(q)}&fuzzy=${fuzzyEl.checked ? 1 : 0}&max=10`;
  try {
    const r = await fetch(url);
    const j = await r.json();
    selectedIdx = -1;
    renderMatches(j.matches || []);
  } catch {}
}

qEl.addEventListener('input', () => { clearTimeout(searchTimer); searchTimer = setTimeout(runSearch, 120); });
qEl.addEventListener('blur', () => setTimeout(() => matchesEl.classList.remove('open'), 150));
qEl.addEventListener('focus', runSearch);
fuzzyEl.addEventListener('change', runSearch);
qEl.addEventListener('keydown', (e) => {
  const items = matchesEl.querySelectorAll('li');
  if (e.key === 'ArrowDown') { selectedIdx = Math.min(items.length - 1, selectedIdx + 1); e.preventDefault(); }
  else if (e.key === 'ArrowUp') { selectedIdx = Math.max(-1, selectedIdx - 1); e.preventDefault(); }
  else if (e.key === 'Enter' && selectedIdx >= 0) { qEl.value = items[selectedIdx].textContent; matchesEl.classList.remove('open'); return; }
  else if (e.key === 'Escape') { matchesEl.classList.remove('open'); return; }
  else return;
  items.forEach((li, i) => li.classList.toggle('sel', i === selectedIdx));
});

// ---------- about modal ----------
const aboutEl = document.getElementById('about');
const aboutGo = document.getElementById('about-go');
const aboutClose = document.getElementById('about-close');
const ABOUT_KEY = 'gridwatch.about.seen.v1';
function showAbout() { aboutEl.hidden = false; }
function hideAbout() { aboutEl.hidden = true; try { localStorage.setItem(ABOUT_KEY, '1'); } catch {} }
aboutGo.addEventListener('click', hideAbout);
aboutClose.addEventListener('click', hideAbout);
aboutEl.addEventListener('click', (e) => { if (e.target === aboutEl) hideAbout(); });
window.addEventListener('keydown', (e) => {
  if (e.key === '?' && !aboutEl.hidden) hideAbout();
  else if (e.key === '?' && document.activeElement !== qEl) showAbout();
  else if (e.key === 'Escape' && !aboutEl.hidden) hideAbout();
});
try { if (!localStorage.getItem(ABOUT_KEY)) showAbout(); } catch { showAbout(); }

// ---------- boot ----------
resizeCanvas();
setInterval(poll, 100);
poll();
