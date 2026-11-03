/* Shared status/command gate for the existing student pages. No replacement UI. */
(function () {
  'use strict';
  const NativeSocket = window.WebSocket;
  const sockets = new Set();
  let state = null, seenAt = 0, sequence = 0, lastKey = '', stale = false;
  const disabledByUs = new Set();
  const names = { TEACHING: '一般教學', IDLE: '等待裁判', PREPARING: '準備本場', READY: '準備完成',
    STARTING: '即將開始', RUN_MANUAL: '手動階段', RUN_AUTO: '自動階段', GAP: '換段暫停',
    PAUSED: '裁判暫停', FINISHED: '比賽結束', ABORTED: '比賽中止', FAULT: '故障停車' };
  const isRead = d => (!d.mode && !d.command && ['ping', 'capabilities', 'read', 'read_config'].includes(d.cmd)) || d.message_success === true;
  const isMotion = d => !d.command && (d.mode === 'joy' || (!d.mode && ['pwm', 'speed', 'move_to', 'move_by', 'servo', 'stop'].includes(d.cmd)));
  function current() { return !!state && performance.now() - seenAt < 350 && !stale; }
  function changed() {
    window.dispatchEvent(new CustomEvent('competitionchange', {detail: {...state, stale: !current()}}));
  }
  function observe(next) {
    if (next.type !== 'competition_state') return;
    const key = [next.boot_id, next.match_id, next.generation, next.epoch, next.managed, next.allowed].join('/');
    const transition = key !== lastKey || stale;
    state = next; seenAt = performance.now(); stale = false; lastKey = key;
    if (transition) changed();
    render();
  }
  function stop() {
    for (const ws of sockets) if (ws.readyState === NativeSocket.OPEN) {
      NativeSocket.prototype.send.call(ws, JSON.stringify({cmd: 'competition_stop'}));
      return;
    }
    fetch('/api/competition/stop', {method: 'POST'}).catch(() => {});
  }
  window.CompetitionStudent = { get state() { return state; }, stop };
  window.WebSocket = class extends NativeSocket {
    constructor(...args) {
      super(...args);
      // Only the local firmware socket belongs to this protocol.
      this.competitionSocket = new URL(args[0], location.href).host === location.host && new URL(args[0], location.href).pathname === '/ws';
      if (!this.competitionSocket) return;
      sockets.add(this);
      this.addEventListener('message', event => {
        let d; try { d = JSON.parse(event.data); } catch (_) { return; }
        if (d.type === 'competition_state') { event.stopImmediatePropagation(); observe(d); }
      });
      this.addEventListener('close', () => {
        sockets.delete(this);
        if (state && state.managed) { stale = true; changed(); render(); }
      });
    }
    send(raw) {
      if (!this.competitionSocket || !state || !state.managed) return super.send(raw);
      let d; try { d = JSON.parse(raw); } catch (_) { return; }
      if (isRead(d) || d.cmd === 'competition_stop') return super.send(raw);
      // Original Blockly stop sends an empty program. It remains a real stop in AUTO.
      if (d.mode === 'PROG' && Array.isArray(d.setup) && !d.setup.length && Array.isArray(d.loop) && !d.loop.length) {
        return super.send(JSON.stringify({cmd: 'competition_stop'}));
      }
      if (!current() || !state.state_fresh || !state.allowed || document.hidden || !isMotion(d)) return;
      d._competition = {token: state.token, generation: state.generation, sequence: ++sequence};
      return super.send(JSON.stringify(d));
    }
  };
  function setDisabled(el, value) {
    if (value && !el.disabled) { el.disabled = true; disabledByUs.add(el); }
    if (!value && disabledByUs.has(el)) { el.disabled = false; disabledByUs.delete(el); }
  }
  function render() {
    if (!document.body || !state) return;
    let banner = document.getElementById('competitionStatus');
    if (!banner) {
      banner = document.createElement('div'); banner.id = 'competitionStatus';
      banner.setAttribute('role', 'status');
      banner.style.cssText = 'position:sticky;top:0;z-index:9999;flex:none;padding:8px 12px;background:#142d43;color:white;font:15px sans-serif;display:flex;gap:12px;align-items:center;justify-content:space-between';
      const label = document.createElement('span'); label.id = 'competitionStatusText'; banner.appendChild(label);
      const button = document.createElement('button'); button.id = 'competitionStop'; button.textContent = '停止本車';
      button.onclick = stop; banner.appendChild(button); document.body.prepend(banner);
    }
    banner.hidden = !state.managed && !state.paired;
    banner.style.display = banner.hidden ? 'none' : 'flex';
    const staleState = !current() || !state.state_fresh;
    let label = names[state.state] || '等待裁判';
    if (state.managed && staleState && !state.fault) label = '狀態連線中斷・操作鎖定';
    if (state.managed && !staleState) label += ' · ' + (state.remaining_ms / 1000).toFixed(1) + ' 秒';
    if (state.managed) label += state.allowed && !staleState ? ' · 可手動操作' : ' · 手動操作鎖定';
    document.getElementById('competitionStatusText').textContent = label;
    document.getElementById('competitionStop').hidden = !state.managed;
    const locked = state.managed;
    for (const id of ['runBtn', 'saveBtn', 'autorunToggle']) {
      const el = document.getElementById(id); if (el) setDisabled(el, locked);
    }
    // Preserve the existing page layout and controls; only their availability changes.
    const path = location.pathname;
    if (/\/(set(?:\.html)?|hardware_test\.html)$/.test(path)) {
      document.querySelectorAll('input,select,button').forEach(el => {
        if (el.id !== 'competitionStop') setDisabled(el, locked);
      });
    }
    if (/\/joy(?:\.html)?$/.test(path)) {
      const unavailable = locked && (staleState || !state.allowed);
      document.querySelectorAll('input[type="range"],select').forEach(el => setDisabled(el, unavailable));
      document.querySelectorAll('.joystick-section').forEach(el => { el.style.opacity = unavailable ? '.5' : ''; });
    }
  }
  document.addEventListener('visibilitychange', () => {
    if (document.hidden && state && state.managed) {
      if (state.allowed) stop(); // AUTO runs independently of student browser visibility.
      stale = true; changed(); render();
    }
  });
  document.addEventListener('DOMContentLoaded', render);
  // Pages without their own WebSocket still receive the same state.
  let polling = false, supported = true;
  setInterval(async () => {
    if (state && state.managed && !current() && !stale) { stale = true; changed(); render(); }
    if (!supported || polling || [...sockets].some(ws => ws.readyState === NativeSocket.OPEN)) return;
    polling = true;
    try {
      const r = await fetch('/api/competition/state', {cache: 'no-store'});
      if (r.status === 404) supported = false;
      if (r.ok) observe(await r.json());
    } catch (_) { /* A previous managed state remains locked. */ }
    finally { polling = false; }
  }, 100);
})();
