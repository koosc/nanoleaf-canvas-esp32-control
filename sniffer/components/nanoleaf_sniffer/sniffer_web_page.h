#pragma once

namespace esphome {
namespace nanoleaf_sniffer {

static const char SNIFFER_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no"/>
  <title>Nanoleaf Canvas Network Bus Sniffer</title>
  <style>
    :root {
      --bg: #0e1117;
      --card: #161b22;
      --card-border: #30363d;
      --primary: #58a6ff;
      --accent: #238636;
      --accent-hover: #2ea043;
      --danger: #da3633;
      --danger-hover: #f85149;
      --text: #f0f6fc;
      --text-muted: #8b949e;
      --yellow: #d29922;
      --purple: #bc8cff;
      --cyan: #39c5bb;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace;
      padding: 16px;
      font-size: 14px;
    }
    .header {
      display: flex;
      flex-wrap: wrap;
      justify-content: space-between;
      align-items: center;
      padding-bottom: 14px;
      border-bottom: 1px solid var(--card-border);
      margin-bottom: 16px;
      gap: 12px;
    }
    .title-group h1 {
      font-size: 20px;
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .tag {
      font-size: 11px;
      padding: 2px 8px;
      border-radius: 12px;
      background: #1f6feb;
      color: #fff;
      font-weight: 600;
      text-transform: uppercase;
    }
    .tag.passive { background: #238636; }
    .tag.emulator { background: #8957e5; }
    .stats-bar {
      display: flex;
      flex-wrap: wrap;
      gap: 16px;
      background: var(--card);
      border: 1px solid var(--card-border);
      border-radius: 8px;
      padding: 12px 16px;
      margin-bottom: 16px;
      font-size: 13px;
    }
    .stat-item {
      display: flex;
      flex-direction: column;
    }
    .stat-label {
      color: var(--text-muted);
      font-size: 11px;
      text-transform: uppercase;
      margin-bottom: 2px;
    }
    .stat-val {
      font-size: 16px;
      font-weight: 700;
      color: var(--text);
    }
    .toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
      margin-bottom: 16px;
    }
    button {
      background: var(--card);
      border: 1px solid var(--card-border);
      color: var(--text);
      padding: 7px 14px;
      border-radius: 6px;
      cursor: pointer;
      font-size: 13px;
      font-weight: 500;
      transition: all 0.15s ease;
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }
    button:hover { background: #21262d; border-color: #8b949e; }
    button.btn-primary { background: var(--accent); border-color: var(--accent); color: #fff; }
    button.btn-primary:hover { background: var(--accent-hover); }
    button.btn-danger { background: var(--danger); border-color: var(--danger); color: #fff; }
    button.btn-danger:hover { background: var(--danger-hover); }
    button.active { background: #388bfd33; border-color: var(--primary); color: var(--primary); }
    input[type="text"] {
      background: var(--card);
      border: 1px solid var(--card-border);
      color: var(--text);
      padding: 7px 12px;
      border-radius: 6px;
      font-size: 13px;
      min-width: 200px;
    }
    input[type="text"]:focus { outline: none; border-color: var(--primary); }
    .table-container {
      background: var(--card);
      border: 1px solid var(--card-border);
      border-radius: 8px;
      overflow-x: auto;
      max-height: calc(100vh - 240px);
      position: relative;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      text-align: left;
      font-size: 13px;
    }
    thead {
      position: sticky;
      top: 0;
      background: #1c2128;
      z-index: 10;
    }
    th {
      padding: 10px 12px;
      border-bottom: 1px solid var(--card-border);
      color: var(--text-muted);
      font-weight: 600;
      font-size: 12px;
      text-transform: uppercase;
      white-space: nowrap;
    }
    td {
      padding: 8px 12px;
      border-bottom: 1px solid #21262d;
      white-space: nowrap;
    }
    tr:hover { background: #1c2128; }
    .hex-code {
      font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
      color: var(--cyan);
      font-size: 12px;
      cursor: pointer;
      padding: 2px 4px;
      border-radius: 4px;
    }
    .hex-code:hover { background: #22272e; color: #fff; }
    .badge {
      display: inline-block;
      padding: 2px 7px;
      border-radius: 4px;
      font-size: 11px;
      font-weight: 600;
    }
    .badge-color { background: #79c0ff22; color: #79c0ff; border: 1px solid #388bfd44; }
    .badge-bri { background: #e3b34122; color: #e3b341; border: 1px solid #d2992244; }
    .badge-layout { background: #bc8cff22; color: #bc8cff; border: 1px solid #8957e544; }
    .badge-poll { background: #8b949e22; color: #8b949e; border: 1px solid #30363d; }
    .badge-node { background: #56d36422; color: #56d364; border: 1px solid #2ea04344; }
    .color-swatch {
      display: inline-block;
      width: 16px;
      height: 16px;
      border-radius: 3px;
      vertical-align: middle;
      margin-right: 6px;
      border: 1px solid #ffffff44;
    }
  </style>
</head>
<body>
  <div class="header">
    <div class="title-group">
      <h1>Nanoleaf Bus Sniffer <span id="mode-badge" class="tag passive">Passive Mode</span></h1>
    </div>
    <div style="color: var(--text-muted); font-size: 12px;">
      1 Mbaud 8N1 Half-Duplex &bull; <span id="conn-status">Connecting...</span>
    </div>
  </div>

  <div class="stats-bar">
    <div class="stat-item">
      <span class="stat-label">Buffer Packets</span>
      <span class="stat-val" id="stat-count">0</span>
    </div>
    <div class="stat-item">
      <span class="stat-label">Color Frames</span>
      <span class="stat-val" id="stat-color" style="color: var(--primary);">0</span>
    </div>
    <div class="stat-item">
      <span class="stat-label">Brightness Frames</span>
      <span class="stat-val" id="stat-bri" style="color: var(--yellow);">0</span>
    </div>
    <div class="stat-item">
      <span class="stat-label">Layout / Enum</span>
      <span class="stat-val" id="stat-layout" style="color: var(--purple);">0</span>
    </div>
    <div class="stat-item">
      <span class="stat-label">Polls Filtered</span>
      <span class="stat-val" id="stat-polls" style="color: var(--text-muted);">0</span>
    </div>
    <div class="stat-item">
      <span class="stat-label">Uptime</span>
      <span class="stat-val" id="stat-uptime">0s</span>
    </div>
  </div>

  <div class="toolbar">
    <button id="btn-pause" onclick="togglePause()">&#9208; Pause</button>
    <button id="btn-filter" class="active" onclick="toggleFilter()">&#9654; Filter C0 Polls: ON</button>
    <button id="btn-mode" onclick="toggleMode()">&#9881; Switch to Emulator Mode</button>
    <button id="btn-clear" class="btn-danger" onclick="clearBuffer()">&#128465; Clear</button>
    <button onclick="downloadJson()">&#128190; Export JSON</button>
    <div style="flex-grow:1;"></div>
    <input type="text" id="search-input" placeholder="Filter packets (e.g. FC, color, bri)..." oninput="applyFilter()"/>
  </div>

  <div class="table-container">
    <table id="packets-table">
      <thead>
        <tr>
          <th style="width: 50px;">#</th>
          <th style="width: 90px;">Time (s)</th>
          <th style="width: 80px;">&Delta; (ms)</th>
          <th style="width: 140px;">Type</th>
          <th>Decoded Summary</th>
          <th style="width: 260px;">Raw Hex</th>
          <th style="width: 50px;">Len</th>
        </tr>
      </thead>
      <tbody id="packets-body">
        <tr><td colspan="7" style="text-align:center; color: var(--text-muted); padding: 40px;">Waiting for bus packets... Connect stock controller or power cycle.</td></tr>
      </tbody>
    </table>
  </div>

  <script>
    let isPaused = false;
    let filterPolls = true;
    let emulatorMode = false;
    let lastSeq = 0;
    let allPackets = [];

    function togglePause() {
      isPaused = !isPaused;
      const btn = document.getElementById("btn-pause");
      btn.innerHTML = isPaused ? "&#9654; Resume" : "&#9208; Pause";
      btn.classList.toggle("active", isPaused);
    }

    async function toggleFilter() {
      filterPolls = !filterPolls;
      const res = await fetch(`/api/filter?polls=${filterPolls ? 1 : 0}`);
      const btn = document.getElementById("btn-filter");
      btn.innerText = `Filter C0 Polls: ${filterPolls ? 'ON' : 'OFF'}`;
      btn.classList.toggle("active", filterPolls);
    }

    async function toggleMode() {
      const target = emulatorMode ? "passive" : "emulator";
      if (!confirm(`Switch sniffer mode to ${target.toUpperCase()}?\n\n- PASSIVE: 100% listen-only, safe for inline/tapped monitoring.\n- EMULATOR: Responds to 0x80/0xC0 so stock controller runs standalone.`)) return;
      const res = await fetch(`/api/mode?mode=${target}`);
      const data = await res.json();
      emulatorMode = data.emulator_mode;
      updateModeUI();
    }

    function updateModeUI() {
      const badge = document.getElementById("mode-badge");
      const btn = document.getElementById("btn-mode");
      if (emulatorMode) {
        badge.className = "tag emulator";
        badge.innerText = "Panel Emulator Mode";
        btn.innerText = "Switch to Passive Sniffer";
      } else {
        badge.className = "tag passive";
        badge.innerText = "Passive Sniffer";
        btn.innerText = "Switch to Emulator Mode";
      }
    }

    async function clearBuffer() {
      await fetch('/api/clear');
      allPackets = [];
      lastSeq = 0;
      document.getElementById("packets-body").innerHTML = '<tr><td colspan="7" style="text-align:center; color: var(--text-muted); padding: 30px;">Buffer cleared. Listening for next packet...</td></tr>';
      document.getElementById("stat-count").innerText = "0";
    }

    function downloadJson() {
      const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(allPackets, null, 2));
      const a = document.createElement('a');
      a.setAttribute("href", dataStr);
      a.setAttribute("download", `nanoleaf_sniff_${Date.now()}.json`);
      document.body.appendChild(a);
      a.click();
      a.remove();
    }

    function renderPacketRow(p) {
      let badgeClass = "badge-poll";
      let summary = p.desc || p.type;
      let swatch = "";

      if (p.r >= 0 && p.g >= 0 && p.b >= 0) {
        badgeClass = "badge-color";
        swatch = `<span class="color-swatch" style="background: rgb(${p.r},${p.g},${p.b});"></span>`;
        summary = `${swatch}<strong>R:${p.r} G:${p.g} B:${p.b}</strong> &bull; <span style="color:#fff;background:#333;padding:1px 4px;border-radius:3px;">W:${p.w}</span> &bull; T:${p.trans}`;
      } else if (p.bri >= 0) {
        badgeClass = "badge-bri";
        summary = `<strong>Brightness: ${p.bri}/255 (${Math.round(p.bri*100/255)}%)</strong>`;
      } else if (p.type.includes("Layout") || p.type.includes("Root")) {
        badgeClass = "badge-layout";
      } else if (p.type.includes("Node")) {
        badgeClass = "badge-node";
      }

      return `<tr data-text="${(p.type + ' ' + p.desc + ' ' + p.hex).toLowerCase()}">
        <td style="color: var(--text-muted);">${p.id}</td>
        <td>${p.time_s.toFixed(3)}</td>
        <td style="color: ${p.delta_ms > 100 ? 'var(--yellow)' : 'var(--text-muted)'};">+${p.delta_ms.toFixed(1)}</td>
        <td><span class="badge ${badgeClass}">${p.type}</span></td>
        <td>${summary}</td>
        <td><span class="hex-code" title="Click to copy" onclick="navigator.clipboard.writeText('${p.hex}')">${p.hex}</span></td>
        <td style="color: var(--text-muted);">${p.len}</td>
      </tr>`;
    }

    function applyFilter() {
      const q = document.getElementById("search-input").value.trim().toLowerCase();
      const rows = document.querySelectorAll("#packets-body tr");
      rows.forEach(r => {
        const text = r.getAttribute("data-text");
        if (!text) return;
        r.style.display = (!q || text.includes(q)) ? "" : "none";
      });
    }

    async function pollPackets() {
      if (isPaused) {
        setTimeout(pollPackets, 500);
        return;
      }
      try {
        const res = await fetch(`/api/packets?since=${lastSeq}&limit=100`);
        const data = await res.json();
        document.getElementById("conn-status").innerText = "Connected";
        document.getElementById("conn-status").style.color = "var(--accent)";

        if (data.stats) {
          document.getElementById("stat-count").innerText = data.buffer_count || 0;
          document.getElementById("stat-color").innerText = data.stats.color_frames || 0;
          document.getElementById("stat-bri").innerText = data.stats.brightness_frames || 0;
          document.getElementById("stat-layout").innerText = data.stats.layout_frames || 0;
          document.getElementById("stat-polls").innerText = data.stats.c0_polls || 0;
          document.getElementById("stat-uptime").innerText = (data.stats.uptime_s || 0) + "s";
          if (data.stats.mode !== undefined) {
            emulatorMode = (data.stats.mode === "emulator");
            updateModeUI();
          }
        }

        if (data.packets && data.packets.length > 0) {
          const tbody = document.getElementById("packets-body");
          if (allPackets.length === 0) {
            tbody.innerHTML = "";
          }
          let html = "";
          data.packets.forEach(p => {
            allPackets.push(p);
            html += renderPacketRow(p);
            if (p.id > lastSeq) lastSeq = p.id;
          });
          tbody.insertAdjacentHTML('beforeend', html);
          applyFilter();
          const container = document.querySelector(".table-container");
          container.scrollTop = container.scrollHeight;
        }
      } catch (err) {
        document.getElementById("conn-status").innerText = "Offline / Reconnecting";
        document.getElementById("conn-status").style.color = "var(--danger)";
      }
      setTimeout(pollPackets, 400);
    }

    window.addEventListener("DOMContentLoaded", () => {
      pollPackets();
    });
  </script>
</body>
</html>
)rawliteral";

}  // namespace nanoleaf_sniffer
}  // namespace esphome
