/********************************************
 * app.js - v1.3.39 (Message Log Limit)
 ********************************************/

var workspace;
var ws;
let lastAppendTime = 0;

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

  // 開頁時自動載入 ESP32 上的存檔(若有)
  setTimeout(autoLoadFromEsp32, 300);
});

function initWebSocket() {
  var wsUrl = ((window.location.protocol === 'https:') ? 'wss://' : 'ws://') + window.location.hostname + '/ws';
  ws = new WebSocket(wsUrl);

  ws.onmessage = (e) => {
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
      if (data.message && data.message !== "指令已接收") {
        ws.send(JSON.stringify({ "mode": "PROG", "message_success": true }));
      }
    } catch (err) {
      appendSensorOutput("收到: " + e.data);
    }
  };

  ws.onclose = () => appendSensorOutput("連線關閉");
  ws.onerror = () => appendSensorOutput("連線錯誤");
}

function resetCode() {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ mode: 'PROG', setup: [], loop: [] }));
    appendSensorOutput("已停止程式");
  }
}

function resetAndGoHome() { resetCode(); setTimeout(() => location.href = 'index.html', 200); }

function runBlocklyCode() {
  try {
    const irNodes = parseWorkspaceToIR(workspace);

    const payload = {
      mode: 'PROG',
      setup: irNodes.filter(n => n instanceof arduino_setupNode).flatMap(n => n.body.map(cmd => cmd.toJson())),
      loop: irNodes.filter(n => n instanceof arduino_loopNode).flatMap(n => n.body.map(cmd => cmd.toJson()))
    };

    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(payload));
      appendSensorOutput("🚀 程式已傳送 (v1.3.1)");
    } else {
      appendSensorOutput("❌ 錯誤: WebSocket 未連線");
    }
  } catch (e) {
    appendSensorOutput(`❌ 無法執行: ${e.message}`);
  }
}

// ============================================================================
// 程式存檔 — 統一存到 ESP32 NVS,與 ai.html 共用同一份；不受 uploadfs 影響
// 端點: GET/POST/DELETE /api/program  (見 src/ProgramStore.h)
// ============================================================================

function buildProgPayload() {
  const xml = Blockly.Xml.workspaceToDom(workspace);
  const xmlText = Blockly.Xml.domToText(xml);
  const irNodes = parseWorkspaceToIR(workspace);
  const json = {
    mode: 'PROG',
    setup: irNodes.filter(n => n instanceof arduino_setupNode).flatMap(n => n.body.map(cmd => cmd.toJson())),
    loop:  irNodes.filter(n => n instanceof arduino_loopNode).flatMap(n => n.body.map(cmd => cmd.toJson()))
  };
  return { json, xml: xmlText, source: 'blockly' };
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
