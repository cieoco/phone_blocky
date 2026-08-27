/********************************************
 * app.js - v1.3.39 (Message Log Limit)
 ********************************************/

var workspace;
var ws;
let lastAppendTime = 0;
let wsReconnectTimer = null;
let wsReconnectEnabled = true;
const WS_RECONNECT_DELAY_MS = 2000;

function appendSensorOutput(newMessage) {
  const sensorOutputDiv = document.getElementById('sensorOutput');
  if (!sensorOutputDiv) return;

  if (!document.getElementById('clearBtn')) {
    const clearBtn = document.createElement('button');
    clearBtn.id = 'clearBtn';
    clearBtn.textContent = '🗑️ 清除訊息';
    clearBtn.className = 'btn';
    clearBtn.style.fontSize = '10px';
    clearBtn.style.padding = '5px 10px';
    clearBtn.style.marginBottom = '10px';
    clearBtn.onclick = () => {
      const ps = sensorOutputDiv.querySelectorAll('p');
      ps.forEach(p => p.remove());
    };
    sensorOutputDiv.insertBefore(clearBtn, sensorOutputDiv.firstChild.nextSibling);
  }

  const now = Date.now();
  // v1.3.3: 效能優化 - 如果是感測器數值且更新太快，則更新最後一行而不是新增
  const isSensorData = newMessage.includes('=');
  const lastP = sensorOutputDiv.lastElementChild;

  if (isSensorData && lastP && lastP.tagName === 'P' && lastP.textContent.includes('=') && (now - lastAppendTime < 100)) {
    lastP.textContent = newMessage;
    return;
  }

  lastAppendTime = now;
  const p = document.createElement('p');
  p.textContent = newMessage;
  p.style.fontSize = '12px';
  p.style.margin = '2px 0';
  p.style.borderBottom = '1px solid #f0f0f0';
  sensorOutputDiv.appendChild(p);

  if (sensorOutputDiv.querySelectorAll('p').length > 10) {
    const ps = sensorOutputDiv.querySelectorAll('p');
    if (ps.length > 0) ps[0].remove();
  }
}

window.addEventListener('load', function () {
  // 觸控或小螢幕時放大積木，手指比較好點選與拖曳
  var isTouchOrSmall = ('ontouchstart' in window) ||
    window.matchMedia('(max-width: 768px)').matches;

  workspace = Blockly.inject('blocklyDiv', {
    toolbox: document.getElementById('toolbox'),
    renderer: 'geras',
    zoom: { controls: true, wheel: true, startScale: isTouchOrSmall ? 1.3 : 1.0 },
    move: { scrollbars: true, drag: true, wheel: true }
  });

  // Blockly 會在 inject 當下快取注入區的尺寸/位置來換算滑鼠座標；
  // 若之後版面位移（載入遮罩消失、視窗縮放/瀏覽器縮放）卻沒重新整理，
  // 第一次點擊就會用到過期座標 → 積木/畫面「跳到別的地方」。
  // 在版面穩定後與每次 resize 時重新計算尺寸即可修正。
  Blockly.svgResize(workspace);
  window.addEventListener('resize', function () {
    if (workspace) Blockly.svgResize(workspace);
  });

  initWebSocket();

  var setupBlock = workspace.newBlock('arduino_setup');
  var loopBlock = workspace.newBlock('arduino_loop');
  setupBlock.initSvg(); loopBlock.initSvg();
  setupBlock.render(); loopBlock.render();

  // 將 setup 與 loop 積木連接在一起
  setupBlock.nextConnection.connect(loopBlock.previousConnection);

  // 稍微往右下移動，避免太靠左上角
  setupBlock.moveBy(20, 20);

  document.getElementById('resetBtn').onclick = resetCode;
  document.getElementById('runBtn').onclick = runBlocklyCode;
  document.getElementById('saveBtn').onclick = saveFile;
  document.getElementById('openFileBtn').onclick = openFile;
  const saveAsBtn = document.getElementById('saveAsNewBtn');
  if (saveAsBtn) saveAsBtn.onclick = downloadFile;

  const autorunToggle = document.getElementById('autorunToggle');
  if (autorunToggle) autorunToggle.onchange = () => setAutorun(autorunToggle.checked);

  // 開頁時自動載入 ESP32 上的存檔(若有)
  setTimeout(autoLoadFromEsp32, 300);
});

// 開機自動執行開關。與 ai.html 共用 /api/program/autorun 端點，兩頁狀態一致。
//
// 這個開關對 I2C 從機模式特別要緊：主端遙控介面按鈕能不能在斷電重開後繼續
// 驅動模組，就取決於它 —— 關著的話開機不會執行程式，主端設定的功能值沒有人
// 消費，按鈕看起來全部失效。原本它只在 ai.html，學生在這頁存完檔很容易漏掉。
async function setAutorun(on) {
  const toggle = document.getElementById('autorunToggle');
  try {
    const resp = await fetch('/api/program/autorun', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ on })
    });
    const data = await resp.json();
    if (data.ok) {
      appendSensorOutput(`⚡ 開機自動執行：${on ? '已開啟' : '已關閉'}`);
    } else {
      throw new Error(data.error || '未知錯誤');
    }
  } catch (e) {
    // 失敗就把勾選狀態改回去，畫面不能顯示一個沒有生效的設定。
    if (toggle) toggle.checked = !on;
    appendSensorOutput(`❌ 設定開機自動執行失敗: ${e.message}`);
  }
}

function initWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;

  if (wsReconnectTimer) {
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = null;
  }

  var wsUrl = ((window.location.protocol === 'https:') ? 'wss://' : 'ws://') + window.location.hostname + '/ws';
  const socket = new WebSocket(wsUrl);
  ws = socket;

  socket.onopen = () => appendSensorOutput("WebSocket 已連線");

  socket.onmessage = (e) => {
    try {
      const data = JSON.parse(e.data);

      // 韌體回報錯誤（如 PROG 解析失敗 / 程式過大）→ 明確顯示，不再靜默
      if (data.ok === false || data.err) {
        appendSensorOutput("❌ 韌體錯誤: " + (data.err || '未知錯誤'));
        return;
      }

      if (data.type !== 'plot') appendSensorOutput("收到: " + (data.message || data.status || e.data));

      // 重要：發送 JSON 格式的確認 (v1.3.1)
      // 配合修正後的韌體，這不會再中斷 Loop 執行
      if (data.message && data.message !== "指令已接收" && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ "mode": "PROG", "message_success": true }));
      }
    } catch (err) {
      appendSensorOutput("收到: " + e.data);
    }
  };

  socket.onclose = () => {
    if (ws === socket) ws = null;
    appendSensorOutput("連線關閉，2 秒後重連");
    if (wsReconnectEnabled && !wsReconnectTimer) {
      wsReconnectTimer = setTimeout(initWebSocket, WS_RECONNECT_DELAY_MS);
    }
  };
  socket.onerror = () => appendSensorOutput("連線錯誤");
}

window.addEventListener('beforeunload', () => {
  wsReconnectEnabled = false;
  if (wsReconnectTimer) clearTimeout(wsReconnectTimer);
});

function resetCode() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ mode: 'PROG', setup: [], loop: [] }));
    appendSensorOutput("已停止程式");
  }
}

function resetAndGoHome() { resetCode(); setTimeout(() => location.href = 'index.html', 200); }

// PROG payload 的單一來源。執行（runBlocklyCode）與存檔（buildProgPayload）
// 共用同一份，否則「跑起來是對的、存下去卻不一樣」這種 bug 遲早會發生。
function buildProgJson() {
  const irNodes = parseWorkspaceToIR(workspace);
  const { declarations, warnings } = collectFunctionDeclarations(workspace);
  warnings.forEach(w => appendSensorOutput(`⚠️ 對外功能：${w}`));
  return {
    mode: 'PROG',
    // 對外功能表拉到頂層：主機一套用就要讀得到，不能等 setup 跑完（SDD §6.1）
    functions: declarations,
    setup: irNodes.filter(n => n instanceof arduino_setupNode).map(n => n.toJson()),
    loop: irNodes.filter(n => n instanceof arduino_loopNode).map(n => n.toJson())
  };
}

function runBlocklyCode() {
  const payload = buildProgJson();

  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
    appendSensorOutput("🚀 程式已傳送 (v1.3.1)");
  } else {
    appendSensorOutput("❌ 錯誤: WebSocket 未連線");
  }
}

// ============================================================================
// 程式存檔 — 統一存到 ESP32 NVS,與 ai.html 共用同一份；不受 uploadfs 影響
// 端點: GET/POST/DELETE /api/program  (見 src/ProgramStore.h)
// ============================================================================

function buildProgPayload() {
  const xml = Blockly.Xml.workspaceToDom(workspace);
  const xmlText = Blockly.Xml.domToText(xml);
  return { json: buildProgJson(), xml: xmlText, source: 'blockly' };
}

async function saveFile() {
  try {
    const payload = buildProgPayload();
    const resp = await fetch('/api/program', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const data = await resp.json();
    if (data.ok) {
      // 讀回驗證，讓畫面上的成功訊息代表資料確實可再被開啟。
      const verifyResp = await fetch('/api/program', { cache: 'no-store' });
      const verify = await verifyResp.json();
      if (!verify.ok || !verify.has_program || !verify.xml) {
        throw new Error('ESP32 未能讀回剛儲存的程式');
      }
      appendSensorOutput(`💾 已存入 ESP32，讀回驗證成功 (${payload.json.setup.length + payload.json.loop.length} 個指令)`);
    } else {
      appendSensorOutput(`❌ 存檔失敗: ${data.error || '未知錯誤'}`);
    }
  } catch (e) {
    appendSensorOutput(`❌ 存檔失敗: ${e.message}`);
  }
}

async function openFile() {
  try {
    const resp = await fetch('/api/program');
    const data = await resp.json();
    if (!data.ok || !data.has_program) {
      appendSensorOutput("ESP32 上沒有存檔");
      return;
    }
    if (data.xml) {
      workspace.clear();
      Blockly.Xml.domToWorkspace(Blockly.utils.xml.textToDom(data.xml), workspace);
      const src = data.meta?.source || 'unknown';
      appendSensorOutput(`📂 已從 ESP32 載入 (來源: ${src})`);
    } else {
      // 只有 JSON,沒 XML — 可能是 ai.html 存的,Blockly 無法視覺還原
      appendSensorOutput("⚠️ ESP32 上的存檔由 AI 產生,Blockly 無法還原視覺積木");
    }
  } catch (e) {
    appendSensorOutput(`❌ 載入失敗: ${e.message}`);
  }
}

async function autoLoadFromEsp32() {
  try {
    const resp = await fetch('/api/program');
    if (!resp.ok) return;
    const data = await resp.json();
    // 開關要反映 ESP32 上的實際設定，不能讓畫面顯示一個沒有根據的預設值。
    const toggle = document.getElementById('autorunToggle');
    if (toggle) toggle.checked = !!data.autorun;
    if (data.ok && data.has_program && data.xml) {
      workspace.clear();
      Blockly.Xml.domToWorkspace(Blockly.utils.xml.textToDom(data.xml), workspace);
      const src = data.meta?.source || 'unknown';
      appendSensorOutput(`已自動載入 ESP32 上的存檔 (來源: ${src})`);
    }
  } catch (e) {
    // 開頁時若 ESP32 沒接通,就靜默略過
  }
}

// 「另存」改成下載到本機,作為跨裝置備份手段
function downloadFile() {
  const xml = Blockly.Xml.workspaceToDom(workspace);
  const xmlText = Blockly.Xml.domToText(xml);
  const blob = new Blob([xmlText], { type: 'application/xml' });
  const a = document.createElement('a');
  const ts = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
  a.href = URL.createObjectURL(blob);
  a.download = `phone_blocky_${ts}.xml`;
  a.click();
  URL.revokeObjectURL(a.href);
  appendSensorOutput("📁 已下載 .xml 備份到本機");
}

function optimizeTouchExperience() { }
function hideLoadingProgress() { }
function showLoadingProgress() { }
function updateLoadingProgress() { }
