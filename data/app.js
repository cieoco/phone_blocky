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
    // maxScale / minScale 是給「看全部」用的護欄：zoomToFit 會算出剛好塞滿的
    // 倍率，程式只有兩三個積木時那個倍率會大得離譜（積木佔滿整個螢幕），
    // 程式很長時又會縮到看不見字。沒有上下限的話這顆按鈕反而難用。
    // 註：pinch（雙指縮放）這版預設是 wheel||controls，兩者都開著所以已生效。
    zoom: {
      controls: true, wheel: true, pinch: true,
      startScale: isTouchOrSmall ? 1.3 : 1.0,
      maxScale: 2.0, minScale: 0.3
    },
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
  const openLocalBtn = document.getElementById('openLocalBtn');
  const localFileInput = document.getElementById('localFileInput');
  if (openLocalBtn && localFileInput) {
    openLocalBtn.onclick = () => {
      if (!confirmReplaceWorkspace()) return;
      localFileInput.value = '';          // 同一個檔案可以再選一次
      localFileInput.click();
    };
    localFileInput.onchange = () => openLocalFile(localFileInput.files[0]);
  }

  // 追蹤「畫面上有沒有還沒存的修改」：只看會改變程式的事件，載入／存檔後歸零
  workspace.addChangeListener((e) => {
    if (!e.isUiEvent && !suppressDirtyTracking) workspaceDirty = true;
  });

  const autorunToggle = document.getElementById('autorunToggle');
  if (autorunToggle) autorunToggle.onchange = () => setAutorun(autorunToggle.checked);

  initMobileShell();

  // 開頁時自動載入 ESP32 上的存檔(若有)
  setTimeout(autoLoadFromEsp32, 300);
});

// ============================================================================
// 手機版面（app shell）的互動：看全部／整理／抽屜／訊息列
//
// 整頁不捲動，所以任何「改變畫布高度」的動作都必須跟著呼叫 Blockly.svgResize()。
// Blockly 會在 inject 當下快取注入區的尺寸來換算指標座標，版面變了卻沒重算，
// 下一次點擊就會落在錯的地方（積木像是「跳走」）—— 這一頁最難查的老問題。
// ============================================================================
function initMobileShell() {
  const $ = (id) => document.getElementById(id);

  // ---- 看全部：直接命中「在手機上看不到整支程式」這件事 ----
  const fitBtn = $('fitBtn');
  if (fitBtn) fitBtn.onclick = () => fitProgramToScreen();

  // ---- 整理：把散落各處的積木排成一直欄，之後再按「看全部」就很整齊 ----
  const tidyBtn = $('tidyBtn');
  if (tidyBtn) {
    tidyBtn.onclick = () => {
      if (!workspace) return;
      workspace.cleanUp();
      fitProgramToScreen();
    };
  }

  // ---- 抽屜：存檔／開啟／autorun／Plotter ----
  const drawer = $('drawer');
  const scrim = $('drawerScrim');
  const setDrawer = (open) => {
    if (!drawer || !scrim) return;
    drawer.hidden = !open;
    scrim.hidden = !open;
  };
  const menuBtn = $('menuBtn');
  if (menuBtn) menuBtn.onclick = () => setDrawer(true);
  const closeBtn = $('drawerCloseBtn');
  if (closeBtn) closeBtn.onclick = () => setDrawer(false);
  if (scrim) scrim.onclick = () => setDrawer(false);
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') setDrawer(false);
  });
  // 存檔／開啟按完就把抽屜收起來，不然結果訊息被面板蓋住看不到
  ['saveBtn', 'openFileBtn', 'saveAsNewBtn', 'openLocalBtn'].forEach((id) => {
    const el = $(id);
    if (!el) return;
    el.addEventListener('click', () => setTimeout(() => setDrawer(false), 150));
  });

  // ---- 訊息列：收起時一行，點開成面板 ----
  const strip = $('statusStrip');
  const panel = $('sensorOutput');
  const latest = $('statusLatest');
  if (strip && panel) {
    strip.onclick = () => {
      const open = panel.hidden;
      panel.hidden = !open;
      strip.setAttribute('aria-expanded', String(open));
      if (open) panel.scrollTop = panel.scrollHeight;
      // 面板佔掉畫布的高度，一定要重算座標
      if (workspace) Blockly.svgResize(workspace);
    };
  }

  // 把最新一條訊息鏡像到收起來的那一行。用 MutationObserver 而不是去改
  // appendSensorOutput：那個函式有自己的去重與上限邏輯（而且 ai.html 之外
  // 還有別處在用它的輸出格式），從外面觀察比插手它的內部省事也安全。
  if (panel && latest && typeof MutationObserver !== 'undefined') {
    const mirror = () => {
      const ps = panel.querySelectorAll('p');
      if (ps.length) latest.textContent = ps[ps.length - 1].textContent;
      if (!panel.hidden) panel.scrollTop = panel.scrollHeight;
    };
    new MutationObserver(mirror).observe(panel, {
      childList: true, subtree: true, characterData: true
    });
  }
}

// 把整支程式縮到剛好塞滿畫面並置中。
// zoomToFit 在工作區完全沒有積木時算不出範圍，所以先擋掉那種情況。
function fitProgramToScreen() {
  if (!workspace) return;
  if (workspace.getTopBlocks(false).length === 0) {
    workspace.setScale(1.0);
    workspace.scrollCenter();
    return;
  }
  workspace.zoomToFit();   // 內部已經會 scrollCenter()，不必再呼叫一次
}

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
        appendSensorOutput("❌ 韌體錯誤: " + (FIRMWARE_ERROR_TEXT[data.err] || data.err || '未知錯誤'));
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
// ws 顯式傳入（而非沿用全域 workspace）：對外功能表是「掃整個工作區」得到的，
// 跟 irNodes 不同源；寫成參數才看得出這個依賴，也讓 tests/check_frontend.cjs
// 能單獨測巢狀深度而不必準備完整的 workspace 替身。
function buildProgJson(irNodes, ws = null) {
  const json = {
    mode: 'PROG',
    setup: irNodes.filter(n => n instanceof arduino_setupNode).flatMap(n => n.body.map(cmd => cmd.toJson())),
    loop:  irNodes.filter(n => n instanceof arduino_loopNode).flatMap(n => n.body.map(cmd => cmd.toJson()))
  };
  // 對外功能表拉到頂層：主機一套用就要讀得到，不能等 setup 跑完（SDD §6.1）。
  // 即使沒有宣告積木也要送空陣列 —— 韌體是用「functions 鍵存在不存在」決定要不要
  // 換表的（CommandProcessor.h 的 haveDecls）。省略的話，學生刪掉所有宣告積木後
  // 主機那邊會一直留著舊元件，怎麼重跑都清不掉。
  if (ws) {
    const { declarations, warnings } = collectFunctionDeclarations(ws);
    json.functions = declarations;
    warnings.forEach(w => appendSensorOutput(`⚠️ 對外功能：${w}`));
  }
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

// 畫面上是否有尚未存檔的修改；以及畫面上的程式是否就是 ESP32 上那一支
let workspaceDirty = false;
let workspaceMatchesEsp32 = false;
let suppressDirtyTracking = false;

// 載入 XML；失敗時還原成載入前的積木（不會因為壞檔案把畫面清空）
function loadXmlIntoWorkspace(xmlText) {
  const dom = upgradeLegacyXml(Blockly.utils.xml.textToDom(xmlText));   // 不是 XML 會在這裡丟錯
  const backup = Blockly.Xml.workspaceToDom(workspace);
  suppressDirtyTracking = true;
  try {
    workspace.clear();
    Blockly.Xml.domToWorkspace(dom, workspace);
  } catch (e) {
    workspace.clear();
    Blockly.Xml.domToWorkspace(backup, workspace);
    throw e;
  } finally {
    // Blockly 的變更事件是非同步送出的，等它們送完再恢復追蹤
    setTimeout(() => { suppressDirtyTracking = false; workspaceDirty = false; }, 0);
  }
}

// 畫面上有積木、而且有還沒存的修改時，先確認再取代
function confirmReplaceWorkspace() {
  const hasContent = workspace.getAllBlocks(false)
    .some(b => b.type !== 'arduino_setup' && b.type !== 'arduino_loop');
  if (!hasContent || !workspaceDirty) return true;
  return window.confirm('目前畫面上的積木還沒存檔，開啟後會被取代。確定要開啟嗎？');
}

// 把 Blockly 載入錯誤轉成看得懂的說明
function describeLoadError(e) {
  const msg = String(e && e.message || e);
  if (/textToDom|parse|XML/i.test(msg)) return '這不是積木程式檔（.xml 格式不正確）';
  if (/block|type/i.test(msg)) {
    return '檔案裡有目前不支援的積木（可能是舊版「馬達」「舵機」積木），無法開啟';
  }
  return msg;
}

function runBlocklyCode() {
  try {
    const irNodes = parseWorkspaceToIR(workspace);
    const payload = buildProgJson(irNodes, workspace);
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
  const json = buildProgJson(irNodes, workspace);
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
    // 板子上只存一支：畫面上的程式不是從 ESP32 開的、而板子上已經有程式時，先確認會覆蓋
    if (!workspaceMatchesEsp32) {
      const existing = await (await fetch('/api/program', { cache: 'no-store' })).json();
      if (existing.ok && existing.has_program &&
          !window.confirm('ESP32 上已經有一支程式，存檔會把它蓋掉。確定要存嗎？\n（想保留的話，可以先從 ESP32 開啟後「下載到本機」）')) {
        appendSensorOutput('已取消存到 ESP32');
        return;
      }
    }
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
      workspaceDirty = false;
      workspaceMatchesEsp32 = true;
    } else {
      appendSensorOutput(`❌ 存檔失敗: ${FIRMWARE_ERROR_TEXT[data.error] || data.error || '未知錯誤'}`);
    }
  } catch (e) {
    appendSensorOutput(`❌ 存檔失敗: ${e.message}`);
  }
}

async function openFile() {
  try {
    if (!confirmReplaceWorkspace()) return;
    const resp = await fetch('/api/program');
    const data = await resp.json();
    if (!data.ok || !data.has_program) {
      appendSensorOutput("ESP32 上沒有存檔");
      return;
    }
    if (data.xml) {
      loadXmlIntoWorkspace(data.xml);
      workspaceMatchesEsp32 = true;
      const src = data.meta?.source || 'unknown';
      appendSensorOutput(`📂 已從 ESP32 開啟 (來源: ${src})`);
    } else {
      // 只有 JSON,沒 XML — 可能是 ai.html 存的,Blockly 無法視覺還原
      appendSensorOutput("⚠️ ESP32 上的存檔由 AI 產生,Blockly 無法還原視覺積木");
    }
  } catch (e) {
    appendSensorOutput(`❌ 從 ESP32 開啟失敗: ${describeLoadError(e)}`);
  }
}

// 從電腦／手機選一個 .xml 檔載入
async function openLocalFile(file) {
  if (!file) return;
  try {
    if (file.size > 1024 * 1024) throw new Error('檔案太大，不像是積木程式檔');
    loadXmlIntoWorkspace(await file.text());
    workspaceMatchesEsp32 = false;   // 這支還沒存到板子上
    appendSensorOutput(`⬆️ 已從本機開啟「${file.name}」（還沒存到 ESP32，要執行或開機自動跑請按「存到 ESP32」）`);
  } catch (e) {
    appendSensorOutput(`❌ 無法開啟「${file.name}」：${describeLoadError(e)}`);
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
      loadXmlIntoWorkspace(data.xml);
      workspaceMatchesEsp32 = true;
      const src = data.meta?.source || 'unknown';
      appendSensorOutput(`已自動載入 ESP32 上的存檔 (來源: ${src})`);
    }
  } catch (e) {
    // 開頁時若 ESP32 沒接通,就靜默略過
  }
}

// 「另存」改成下載到本機,作為跨裝置備份手段
// 把檔名裡不能用的字元換掉，並確保副檔名是 .xml
function sanitizeFileName(name) {
  const base = String(name).trim().replace(/\.xml$/i, '').replace(/[\\/:*?"<>|\x00-\x1f]/g, '_').slice(0, 60);
  return (base || 'phone_blocky') + '.xml';
}

function downloadFile() {
  // 與存到 ESP32 相同：不含積木 ID 的 XML（變數 ID 保留），可用「從本機開啟」載回
  const xmlText = Blockly.Xml.domToText(Blockly.Xml.workspaceToDom(workspace, true));
  const now = new Date();
  const pad = n => String(n).padStart(2, '0');
  const suggested = `phone_blocky_${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}`;
  const name = window.prompt('檔名（存到電腦或手機的下載資料夾）', suggested);
  if (name === null) return;                       // 取消
  const fileName = sanitizeFileName(name);
  const blob = new Blob([xmlText], { type: 'application/xml' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = fileName;
  document.body.appendChild(a);                    // Firefox 需要在頁面上才會觸發
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);   // 太早釋放會讓 iPhone Safari 下載失敗
  workspaceDirty = false;
  appendSensorOutput(`⬇️ 已下載「${fileName}」到本機（${(xmlText.length / 1024).toFixed(1)} KB）`);
}

function optimizeTouchExperience() { }
function hideLoadingProgress() { }
function showLoadingProgress() { }
function updateLoadingProgress() { }
