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
  workspace.registerToolboxCategoryCallback('SUBROUTINE', subroutineFlyout);

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
        appendSensorOutput("❌ 韌體錯誤: " + (FIRMWARE_ERROR_TEXT[data.err] || data.err || '未知錯誤'));
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

// 積木巢狀上限（if / repeat / while 互相包的層數），需與韌體 PROG_MAX_BLOCK_DEPTH 一致
const MAX_BLOCK_DEPTH = 8;

// 「副程式」分類的積木清單：一個定義積木，加上目前每個副程式的呼叫積木。
// 只提供「沒有回傳值」的副程式（直譯器目前不支援回傳值）。
function subroutineFlyout(ws) {
  const el = (tag, attrs = {}) => {
    const e = Blockly.utils.xml.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    return e;
  };
  const def = el('block', { type: 'procedures_defnoreturn', gap: '24' });
  const nameField = el('field', { name: 'NAME' });
  nameField.textContent = Blockly.Msg['PROCEDURES_DEFNORETURN_PROCEDURE'] || '我的副程式';
  def.appendChild(nameField);
  const items = [def];
  const [noReturn] = Blockly.Procedures.allProcedures(ws);
  noReturn.sort((a, b) => a[0].localeCompare(b[0])).forEach(([name, args]) => {
    const call = el('block', { type: 'procedures_callnoreturn', gap: '16' });
    const mutation = el('mutation', { name });
    args.forEach(arg => mutation.appendChild(el('arg', { name: arg })));
    call.appendChild(mutation);
    items.push(call);
  });
  return items;
}

// 計算 PROG 指令陣列的最大巢狀層數（頂層指令本身為 0 層）。
// 呼叫副程式算一層，再加上該副程式本體的層數；副程式不可直接或間接呼叫自己。
function progNestingDepth(cmds, procs = {}, visiting = new Set(), memo = new Map()) {
  let max = 0;
  for (const c of cmds || []) {
    let d = 0;
    if (c.command === 'call') {
      d = 1 + procedureDepth(c.name, procs, visiting, memo);
    } else {
      for (const list of [c.then, c.else, c.do].filter(Array.isArray)) {
        d = Math.max(d, 1 + progNestingDepth(list, procs, visiting, memo));
      }
    }
    max = Math.max(max, d);
  }
  return max;
}

function procedureDepth(name, procs, visiting, memo) {
  if (memo.has(name)) return memo.get(name);
  if (!Object.prototype.hasOwnProperty.call(procs, name)) throw new Error(`找不到副程式「${name}」`);
  if (visiting.has(name)) throw new Error(`副程式「${name}」不能直接或間接呼叫自己`);
  visiting.add(name);
  const d = progNestingDepth(procs[name].body, procs, visiting, memo);
  visiting.delete(name);
  memo.set(name, d);
  return d;
}

// IR → PROG JSON，並在送出前檢查巢狀層數與副程式呼叫（超過時板子會拒絕整支程式）
function buildProgJson(irNodes) {
  const json = {
    mode: 'PROG',
    setup: irNodes.filter(n => n instanceof arduino_setupNode).flatMap(n => n.body.map(cmd => cmd.toJson())),
    loop:  irNodes.filter(n => n instanceof arduino_loopNode).flatMap(n => n.body.map(cmd => cmd.toJson()))
  };
  const procedures = irNodes.filter(n => n instanceof ProcedureDefNode).map(n => n.toJson());
  if (procedures.length) json.procedures = procedures;   // 沒有副程式時維持原本 JSON 形狀
  const procs = Object.fromEntries(procedures.map(p => [p.name, p]));
  const depth = Math.max(progNestingDepth(json.setup, procs), progNestingDepth(json.loop, procs),
                         ...procedures.map(p => procedureDepth(p.name, procs, new Set(), new Map())));
  if (depth > MAX_BLOCK_DEPTH) {
    throw new Error(`積木巢狀太深：目前 ${depth} 層，最多 ${MAX_BLOCK_DEPTH} 層（如果／重複／當…重複互相包的層數，呼叫副程式也算一層）`);
  }
  return json;
}

// 韌體錯誤代碼 → 使用者看得懂的說明
const FIRMWARE_ERROR_TEXT = {
  nesting_too_deep: `積木巢狀太深（最多 ${MAX_BLOCK_DEPTH} 層）`,
  json_parse_failed: '程式格式錯誤或巢狀太深，板子無法解析',
  json_too_large: '程式太大，請減少積木數量',
  recursive_procedure: '副程式不能直接或間接呼叫自己',
  unknown_procedure: '呼叫了不存在的副程式',
  duplicate_procedure: '有兩個同名的副程式',
  program_too_large: '程式太大，超過 ESP32 存檔或執行上限',
  bad_json: '送出的資料格式錯誤',
  missing_json: '存檔資料缺少程式內容',
  mode_must_be_prog: '存檔資料不是積木程式',
  nvs_write_failed: '寫入 ESP32 儲存區失敗（空間可能不足）',
  program_persistence_verification_failed: '寫入後讀回不一致，請再存一次',
  unsupported_or_invalid_program_command: '程式含有不支援的積木或參數'
};

// 沒接在 setup／loop 裡的積木不會執行，明確提醒（以前會默默忽略）
function warnStrayBlocks(irNodes) {
  if (irNodes.strayBlocks > 0) {
    appendSensorOutput(`⚠️ 有 ${irNodes.strayBlocks} 個積木沒有接在 setup／loop 裡，不會執行`);
  }
}

// 舊存檔相容：馬達／舵機／延遲的數值原本是積木上的格子（field），
// 現在改成可接積木的插槽（value）。載入前把舊格子換成「插槽 + 數字積木」，舊程式不用重做。
const LEGACY_NUMBER_FIELDS = {
  motor_pwm: ['PWM'], motor_speed: ['RPM'], motor_position: ['DEG'],
  motor_move_by: ['DEG'], servo_set: ['DEG'], arduino_delay: ['DELAY_TIME']
};

function upgradeLegacyXml(dom) {
  for (const blockEl of dom.getElementsByTagName('block')) {
    const names = LEGACY_NUMBER_FIELDS[blockEl.getAttribute('type')];
    if (!names) continue;
    for (const child of Array.from(blockEl.children)) {
      if (child.tagName.toLowerCase() !== 'field' || !names.includes(child.getAttribute('name'))) continue;
      const doc = blockEl.ownerDocument;
      const ns = blockEl.namespaceURI;
      const value = doc.createElementNS(ns, 'value');
      value.setAttribute('name', child.getAttribute('name'));
      const shadow = doc.createElementNS(ns, 'shadow');
      shadow.setAttribute('type', 'math_number');
      const num = doc.createElementNS(ns, 'field');
      num.setAttribute('name', 'NUM');
      num.textContent = String(Number(child.textContent) || 0);
      shadow.appendChild(num);
      value.appendChild(shadow);
      blockEl.replaceChild(value, child);
    }
  }
  return dom;
}

function loadXmlIntoWorkspace(xmlText) {
  workspace.clear();
  Blockly.Xml.domToWorkspace(upgradeLegacyXml(Blockly.utils.xml.textToDom(xmlText)), workspace);
}

function runBlocklyCode() {
  try {
    const irNodes = parseWorkspaceToIR(workspace);
    const payload = buildProgJson(irNodes);
    warnStrayBlocks(irNodes);

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

// 存檔上限（JSON + XML 合計），需與韌體 ProgramPayload.h 的 PROGRAM_STORE_MAX_BYTES 一致
const MAX_SAVE_BYTES = 32768;

// 計算指令數（含 if／迴圈內部與副程式本體），只用來顯示
function countProgCommands(cmds) {
  let n = 0;
  for (const c of cmds || []) {
    n += 1;
    for (const list of [c.then, c.else, c.do]) if (Array.isArray(list)) n += countProgCommands(list);
  }
  return n;
}

function buildProgPayload() {
  // noId：不存積木 ID（載入時 Blockly 會重新產生），XML 約小 20%；變數 ID 仍保留
  const xml = Blockly.Xml.workspaceToDom(workspace, true);
  const xmlText = Blockly.Xml.domToText(xml);
  const irNodes = parseWorkspaceToIR(workspace);
  const json = buildProgJson(irNodes);
  warnStrayBlocks(irNodes);
  const size = xmlText.length + JSON.stringify(json).length;
  if (size > MAX_SAVE_BYTES) {
    throw new Error(`程式太大，無法存檔（目前約 ${Math.ceil(size / 1024)} KB，上限 ${MAX_SAVE_BYTES / 1024} KB）。` +
                    `可以把重複的動作改成副程式或迴圈來縮小`);
  }
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
      if (!verify.ok || !verify.has_program || verify.xml !== payload.xml) {
        throw new Error('ESP32 未能讀回剛儲存的程式');
      }
      const j = payload.json;
      const total = countProgCommands(j.setup) + countProgCommands(j.loop) +
                    (j.procedures || []).reduce((s, p) => s + countProgCommands(p.body), 0);
      const kb = ((payload.xml.length + JSON.stringify(j).length) / 1024).toFixed(1);
      appendSensorOutput(`💾 已存入 ESP32，讀回驗證成功（${total} 個指令，${kb} KB／上限 ${MAX_SAVE_BYTES / 1024} KB）`);
    } else {
      appendSensorOutput(`❌ 存檔失敗: ${FIRMWARE_ERROR_TEXT[data.error] || data.error || '未知錯誤'}`);
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
      loadXmlIntoWorkspace(data.xml);
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
      loadXmlIntoWorkspace(data.xml);
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
