#pragma once

const char CANVAS_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no"/>
  <title>Nanoleaf Canvas Controller</title>
  <style>
    :root {
      --bg: #111215;
      --card: #1b1d22;
      --card-border: #2c2f38;
      --primary: #00ff88;
      --text: #ffffff;
      --text-muted: #8b92a5;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      padding: 16px;
      display: flex;
      flex-direction: column;
      align-items: center;
      min-height: 100vh;
    }
    .container {
      width: 100%;
      max-width: 640px;
      display: flex;
      flex-direction: column;
      gap: 16px;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 8px 4px;
    }
    h1 { font-size: 1.25rem; font-weight: 700; letter-spacing: -0.5px; }
    .badge {
      background: rgba(0, 255, 136, 0.15);
      color: var(--primary);
      padding: 4px 10px;
      border-radius: 12px;
      font-size: 0.75rem;
      font-weight: 600;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--card-border);
      border-radius: 16px;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 14px;
    }
    .canvas-wrap {
      display: flex;
      justify-content: center;
      align-items: center;
      overflow: auto;
      background: #0d0e11;
      border-radius: 12px;
      padding: 16px;
      min-height: 280px;
    }
    svg {
      max-width: 100%;
      height: auto;
      filter: drop-shadow(0 4px 12px rgba(0,0,0,0.5));
    }
    .tile-group {
      cursor: pointer;
      transition: transform 0.15s ease, filter 0.15s ease;
    }
    .tile-group:hover {
      filter: brightness(1.2);
    }
    .tile-selected rect {
      stroke: #ffffff !important;
      stroke-width: 3px !important;
      filter: drop-shadow(0 0 10px rgba(255,255,255,0.8));
    }
    .controls-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }
    label { font-size: 0.85rem; color: var(--text-muted); font-weight: 500; }
    .slider-wrap {
      display: flex;
      align-items: center;
      gap: 12px;
      flex: 1;
    }
    input[type="range"] {
      flex: 1;
      accent-color: var(--primary);
      height: 6px;
      border-radius: 3px;
      background: #2c2f38;
      outline: none;
    }
    .palette {
      display: grid;
      grid-template-columns: repeat(6, 1fr);
      gap: 8px;
    }
    .swatch {
      width: 100%;
      height: 38px;
      border-radius: 8px;
      border: 2px solid transparent;
      cursor: pointer;
      transition: transform 0.1s ease;
    }
    .swatch:hover { transform: scale(1.08); }
    .swatch:active { transform: scale(0.95); }
    .btn-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    button {
      background: #252830;
      color: #fff;
      border: 1px solid var(--card-border);
      border-radius: 10px;
      padding: 10px 14px;
      font-size: 0.85rem;
      font-weight: 600;
      cursor: pointer;
      transition: background 0.15s ease;
    }
    button:hover { background: #303440; }
    button.active {
      background: var(--primary);
      color: #000;
      border-color: var(--primary);
    }
    .color-picker-row {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    input[type="color"] {
      -webkit-appearance: none;
      border: none;
      width: 44px;
      height: 44px;
      border-radius: 10px;
      cursor: pointer;
      background: none;
    }
    input[type="color"]::-webkit-color-swatch-wrapper { padding: 0; }
    input[type="color"]::-webkit-color-swatch {
      border: 2px solid var(--card-border);
      border-radius: 10px;
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>Nanoleaf Canvas</h1>
      <span class="badge" id="panel-count">12 Panels</span>
    </header>

    <!-- Visual Canvas Placement Card -->
    <div class="card">
      <div style="display:flex; justify-content:space-between; align-items:center;">
        <span style="font-weight:600; font-size:0.95rem;">Wall Placement</span>
        <button onclick="selectAll()" id="btn-select-all" style="padding:6px 12px; font-size:0.75rem;">Select All</button>
      </div>
      <div class="canvas-wrap">
        <svg id="canvas-svg" xmlns="http://www.w3.org/2000/svg"></svg>
      </div>
      <div id="selection-label" style="font-size:0.8rem; color:var(--text-muted); text-align:center;">
        Click any tile above to set its color
      </div>
    </div>

    <!-- Color & Brightness Controls -->
    <div class="card">
      <div class="controls-row">
        <label>Selected Target:</label>
        <span id="target-name" style="font-weight:700; color:var(--primary);">Tile #0</span>
      </div>

      <div class="color-picker-row">
        <input type="color" id="native-picker" value="#ffd2a0" onchange="onColorPicked(this.value)"/>
        <div style="flex:1;">
          <div style="font-size:0.85rem; font-weight:600;" id="hex-label">#FFD2A0</div>
          <div style="font-size:0.75rem; color:var(--text-muted);">Tap palette or pick custom</div>
        </div>
      </div>

      <div class="palette">
        <div class="swatch" style="background:#ff2d55;" onclick="onColorPicked('#ff2d55')"></div>
        <div class="swatch" style="background:#ff9500;" onclick="onColorPicked('#ff9500')"></div>
        <div class="swatch" style="background:#ffcc00;" onclick="onColorPicked('#ffcc00')"></div>
        <div class="swatch" style="background:#34c759;" onclick="onColorPicked('#34c759')"></div>
        <div class="swatch" style="background:#00c7be;" onclick="onColorPicked('#00c7be')"></div>
        <div class="swatch" style="background:#007aff;" onclick="onColorPicked('#007aff')"></div>
        <div class="swatch" style="background:#5856d6;" onclick="onColorPicked('#5856d6')"></div>
        <div class="swatch" style="background:#af52de;" onclick="onColorPicked('#af52de')"></div>
        <div class="swatch" style="background:#ff2d95;" onclick="onColorPicked('#ff2d95')"></div>
        <div class="swatch" style="background:#ffe0b2;" onclick="onColorPicked('#ffe0b2')"></div>
        <div class="swatch" style="background:#ffffff;" onclick="onColorPicked('#ffffff')"></div>
        <div class="swatch" style="background:#1a1a1a; border-color:#444;" onclick="onColorPicked('#000000')"></div>
      </div>

      <div class="controls-row" style="margin-top:6px;">
        <label>Brightness</label>
        <div class="slider-wrap">
          <input type="range" id="bri-slider" min="0" max="255" value="255" oninput="onBrightnessInput(this.value)"/>
          <span id="bri-val" style="width:36px; text-align:right; font-size:0.8rem; font-weight:600;">100%</span>
        </div>
      </div>
    </div>

    <!-- Effects Card -->
    <div class="card">
      <div style="font-weight:600; font-size:0.95rem;">Dynamic Effects</div>
      <div class="btn-row">
        <button id="btn-wave" onclick="toggleEffect('wave')">🌊 Rainbow Wave</button>
        <button onclick="setEffect('static_rainbow')">🌈 Static Rainbow</button>
      </div>
    </div>
  </div>

  <script>
    let tiles = [];
    let selectedId = 0; // null = all, number = specific tile
    let isWaveActive = false;

    async function loadTiles() {
      try {
        const res = await fetch('/api/tiles');
        if (!res.ok) return;
        tiles = await res.json();
        document.getElementById('panel-count').innerText = `${tiles.length} Panels`;
        renderSvg();
      } catch (e) {
        console.error('Failed to load tiles:', e);
      }
    }

    function renderSvg() {
      if (!tiles.length) return;
      const svg = document.getElementById('canvas-svg');
      
      let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
      tiles.forEach(t => {
        if (t.x < minX) minX = t.x;
        if (t.x > maxX) maxX = t.x;
        if (t.y < minY) minY = t.y;
        if (t.y > maxY) maxY = t.y;
      });

      const TILE_SIZE = 72;
      const SPACING = 82;
      const PADDING = 16;

      const width = (maxX - minX + 1) * SPACING - (SPACING - TILE_SIZE) + PADDING * 2;
      const height = (maxY - minY + 1) * SPACING - (SPACING - TILE_SIZE) + PADDING * 2;

      svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
      svg.setAttribute('width', width);
      svg.setAttribute('height', height);

      let html = '';
      tiles.forEach(t => {
        const px = (t.x - minX) * SPACING + PADDING;
        const py = (maxY - t.y) * SPACING + PADDING; // Invert Y for SVG coordinates
        const isSel = (selectedId === t.id);
        const fill = `rgb(${t.r}, ${t.g}, ${t.b})`;

        html += `
          <g class="tile-group ${isSel ? 'tile-selected' : ''}" onclick="selectTile(${t.id})">
            <rect x="${px}" y="${py}" width="${TILE_SIZE}" height="${TILE_SIZE}" rx="8"
                  fill="${fill}" stroke="#252830" stroke-width="2"/>
            <!-- 4 quadrant lines of Canvas -->
            <line x1="${px + TILE_SIZE/2}" y1="${py}" x2="${px + TILE_SIZE/2}" y2="${py + TILE_SIZE}" stroke="rgba(0,0,0,0.25)" stroke-width="1.5"/>
            <line x1="${px}" y1="${py + TILE_SIZE/2}" x2="${px + TILE_SIZE}" y2="${py + TILE_SIZE/2}" stroke="rgba(0,0,0,0.25)" stroke-width="1.5"/>
            <text x="${px + TILE_SIZE/2}" y="${py + TILE_SIZE/2 + 5}" text-anchor="middle"
                  fill="#ffffff" font-size="13" font-weight="700" style="text-shadow: 0 1px 3px rgba(0,0,0,0.8); pointer-events:none;">
              #${t.id}
            </text>
          </g>
        `;
      });
      svg.innerHTML = html;
    }

    function selectTile(id) {
      selectedId = id;
      document.getElementById('target-name').innerText = `Tile #${id}`;
      document.getElementById('btn-select-all').classList.remove('active');
      const tile = tiles.find(t => t.id === id);
      if (tile) {
        const hex = rgbToHex(tile.r, tile.g, tile.b);
        document.getElementById('native-picker').value = hex;
        document.getElementById('hex-label').innerText = hex.toUpperCase();
      }
      renderSvg();
    }

    function selectAll() {
      selectedId = null;
      document.getElementById('target-name').innerText = 'All Tiles';
      document.getElementById('btn-select-all').classList.add('active');
      renderSvg();
    }

    function hexToRgb(hex) {
      let c = hex.replace('#', '');
      if (c.length === 3) c = c.split('').map(x => x + x).join('');
      const num = parseInt(c, 16);
      return { r: (num >> 16) & 255, g: (num >> 8) & 255, b: num & 255 };
    }

    function rgbToHex(r, g, b) {
      return '#' + [r, g, b].map(x => x.toString(16).padStart(2, '0')).join('');
    }

    async function onColorPicked(hex) {
      document.getElementById('native-picker').value = hex;
      document.getElementById('hex-label').innerText = hex.toUpperCase();
      const rgb = hexToRgb(hex);

      if (selectedId === null) {
        // Set all tiles
        tiles.forEach(t => { t.r = rgb.r; t.g = rgb.g; t.b = rgb.b; });
        renderSvg();
        await fetch(`/api/all?r=${rgb.r}&g=${rgb.g}&b=${rgb.b}`);
      } else {
        // Set single tile
        const t = tiles.find(x => x.id === selectedId);
        if (t) { t.r = rgb.r; t.g = rgb.g; t.b = rgb.b; }
        renderSvg();
        await fetch(`/api/set?id=${selectedId}&r=${rgb.r}&g=${rgb.g}&b=${rgb.b}`);
      }
    }

    let briDebounce = null;
    function onBrightnessInput(val) {
      const pct = Math.round((val / 255) * 100);
      document.getElementById('bri-val').innerText = `${pct}%`;
      clearTimeout(briDebounce);
      briDebounce = setTimeout(() => {
        fetch(`/api/all?bri=${val}`);
      }, 100);
    }

    async function toggleEffect(effect) {
      isWaveActive = !isWaveActive;
      const btn = document.getElementById('btn-wave');
      if (isWaveActive) {
        btn.classList.add('active');
        await fetch('/api/effect?name=wave');
      } else {
        btn.classList.remove('active');
        await fetch('/api/effect?name=off');
      }
    }

    async function setEffect(name) {
      isWaveActive = false;
      document.getElementById('btn-wave').classList.remove('active');
      await fetch(`/api/effect?name=${name}`);
      setTimeout(loadTiles, 300);
    }

    // Initial load
    loadTiles();
    // Background polling every 2.5s to keep sync with wall
    setInterval(loadTiles, 2500);
  </script>
</body>
</html>
)rawliteral";
