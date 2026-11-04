/* Shared status/command gate for the existing student pages. No replacement UI. */
(function () {
  'use strict';
  const NativeSocket = window.WebSocket;
  const sockets = new Set();
  let state = null, seenAt = 0, sequence = 0, lastKey = '', stale = false;
  let handoverBusy = false, handoverNotice = '';
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
  async function handover() {
    if (!current() || state.committed || handoverBusy) return;
    const value = window.prompt('請輸入裁判頁顯示的頻道（1–11）。更換頻道後，手機可能需要重新連上這台車。', String(state.channel || 1));
    if (value === null) return;
    const channel = Number(value);
    if (!Number.isInteger(channel) || channel < 1 || channel > 11) { window.alert('頻道須為 1–11 的整數。'); return; }
    if (!window.confirm('停止本車、移除原裁判配對，並開放新裁判接管 60 秒？學生程式與設定會保留。逾時仍保持停止，可再次開放。')) return;
    handoverBusy = true; handoverNotice = '正在送出更換裁判請求…'; render();
    try {
      const response = await fetch('/api/competition/handover', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({channel})});
      if (!response.ok) throw new Error('請求未接受，請重新確認車輛狀態。');
      handoverNotice = '請等待「開放接管」狀態；若手機斷線，請重新連上這台車。';
    } catch (e) { handoverNotice = '尚未確認結果，請重新連線查看學生頁。'; }
    finally { handoverBusy = false; render(); }
  }
  window.CompetitionStudent = { get state() { return state; }, stop, handover };
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
      banner.style.cssText = 'position:sticky;top:0;z-index:9999;flex:none;width:100%;max-width:100%;min-width:0;box-sizing:border-box;padding:8px 12px;background:#142d43;color:white;font:15px sans-serif;display:flex;flex-wrap:wrap;gap:8px 12px;align-items:center;justify-content:space-between';
      const label = document.createElement('span'); label.id = 'competitionStatusText';
      label.style.cssText = 'flex:1 1 160px;min-width:0;overflow-wrap:anywhere'; banner.appendChild(label);
      const button = document.createElement('button'); button.id = 'competitionStop'; button.textContent = '停止本車';
      button.onclick = stop; banner.appendChild(button);
      // Fixed-height editors reserve space inside their shell; normal pages use body flow.
      (document.getElementById('competitionStatusHost') || document.body).prepend(banner);
      const change = document.createElement('button'); change.id = 'competitionHandover';
      change.onclick = handover; banner.appendChild(change);
    }
    banner.hidden = false;
    banner.style.display = banner.hidden ? 'none' : 'flex';
    const staleState = !current() || !state.state_fresh;
    let label = names[state.state] || '等待裁判';
    if (state.managed && staleState && !state.fault) label = '狀態連線中斷・操作鎖定';
    if (state.managed && !staleState) label += ' · ' + (state.remaining_ms / 1000).toFixed(1) + ' 秒';
    if (state.managed) label += state.allowed && !staleState ? ' · 可手動操作' : ' · 手動操作鎖定';
    label += ` · 頻道 ${state.channel || '?'}`;
    if (state.controller_mac) label += ` · 裁判 ${state.controller_mac}`;
    if (state.pairing_open) { handoverNotice = ''; label += ` · 開放接管 ${Math.ceil(state.pairing_remaining_ms / 1000)} 秒`; }
    const pairingErrors = {authorization_active:'授權仍在進行，請先停止本車後再更換裁判',ownership_write_failed:'管制狀態寫入失敗，維持鎖定',pairing_write_failed:'配對寫入失敗，維持鎖定',channel_write_failed:'頻道儲存失敗',channel_change_failed:'頻道切換失敗',peer_add_failed:'新裁判通訊建立失敗'};
    if (state.pairing_error) { handoverNotice = ''; label += ' · ' + (pairingErrors[state.pairing_error] || '配對未完成'); }
    if (handoverNotice) label += ' · ' + handoverNotice;
    document.getElementById('competitionStatusText').textContent = label;
    document.getElementById('competitionStop').hidden = !state.managed;
    const change = document.getElementById('competitionHandover');
    change.textContent = state.paired || state.managed ? '允許更換裁判' : '開放配對／設定頻道';
    change.disabled = !current() || !!state.committed || handoverBusy;
    const locked = state.managed;
    for (const id of ['runBtn', 'saveBtn', 'autorunToggle']) {
      const el = document.getElementById(id); if (el) setDisabled(el, locked);
    }
    // Preserve the existing page layout and controls; only their availability changes.
    const path = location.pathname;
    if (/\/(set(?:\.html)?|hardware_test\.html)$/.test(path)) {
      document.querySelectorAll('input,select,button').forEach(el => {
        if (!['competitionStop', 'competitionHandover'].includes(el.id)) setDisabled(el, locked);
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
