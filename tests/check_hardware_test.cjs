// Offline only: no browser, fetch, WebSocket, or board access.
//
// Drives data/hardware_test.html's real script through a fake DOM and a fake clock,
// then checks the 啟動 → 執行 → 確認 chain end to end:
//   start a run → the first test actually sends commands → its sequence finishes and
//   raises the confirmation box → confirming advances to the next test → finishing the
//   last one produces the report. Also covers abort and reset.
// Every one of these was broken at some point by a missing element or a dead helper
// (updateProgress() looked up progress-m1 … which no longer exists and threw, so both
// 開始 buttons did nothing at all), and none of it is visible without a board unless a
// test like this drives it.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'data/hardware_test.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

const TEST_IDS = ['m1', 'm2', 'm3', 'm4', 'servo', 'button', 'ultrasonic', 'digital', 'analog',
                  'output', 'encmanual', 'i2c'];

// ---------------------------------------------------------------------------
// Fake DOM: elements are created on demand and remembered, so any id the page asks
// for behaves like a real (empty) element.
// ---------------------------------------------------------------------------
function makeElement(id) {
    const classes = new Set();
    const el = {
        id,
        style: {},
        dataset: {},
        textContent: '',
        innerHTML: '',
        checked: false,
        hidden: false,
        scrollTop: 0,
        scrollHeight: 0,
        children: [],
        classList: {
            add: (...c) => c.forEach(x => classes.add(x)),
            remove: (...c) => c.forEach(x => classes.delete(x)),
            contains: c => classes.has(c),
            toggle: (c, force) => {
                const on = force === undefined ? !classes.has(c) : !!force;
                if (on) classes.add(c); else classes.delete(c);
                return on;
            }
        },
        _classes: classes,
        scrollIntoView() {},
        querySelector(sel) {
            if (!el._q) el._q = {};
            if (!el._q[sel]) el._q[sel] = makeElement(`${id}${sel}`);
            return el._q[sel];
        },
        querySelectorAll: () => []
    };
    return el;
}

function makeDom() {
    const byId = new Map();
    const get = id => {
        if (!byId.has(id)) byId.set(id, makeElement(id));
        return byId.get(id);
    };
    const sections = TEST_IDS.map(id => get(`${id}-test-section`));
    const headings = sections.map(sec => {
        const h3 = makeElement(`${sec.id}-h3`);
        h3.parentElement = sec;
        return h3;
    });
    const document = {
        getElementById: get,
        querySelector: sel => get(`sel:${sel}`),
        querySelectorAll: sel => {
            if (sel === '.test-section > h3') return headings;
            if (sel === '.test-section' || sel === '.test-item') return sections;
            return [];
        },
        createElement: tag => makeElement(`<${tag}>`),
        body: makeElement('body')
    };
    return {document, get, sections, headings};
}

// ---------------------------------------------------------------------------
// Fake clock: the test sequences are setTimeout chains several seconds long.
// ---------------------------------------------------------------------------
function makeClock() {
    let now = 0, nextId = 1;
    const timers = new Map();
    const api = {
        setTimeout: (fn, ms) => { timers.set(nextId, {fn, at: now + (ms || 0), every: 0}); return nextId++; },
        setInterval: (fn, ms) => { timers.set(nextId, {fn, at: now + (ms || 0), every: ms || 1}); return nextId++; },
        clearTimeout: id => timers.delete(id),
        clearInterval: id => timers.delete(id),
        pending: () => timers.size,
        advance(ms) {
            const end = now + ms;
            for (let guard = 0; guard < 20000; guard++) {
                let due = null, dueId = null;
                for (const [id, t] of timers) if (t.at <= end && (!due || t.at < due.at)) { due = t; dueId = id; }
                if (!due) break;
                now = due.at;
                if (due.every) due.at = now + due.every; else timers.delete(dueId);
                due.fn();
            }
            now = end;
        }
    };
    return api;
}

function load({connected = true} = {}) {
    const dom = makeDom();
    const clock = makeClock();
    const sent = [];
    const alerts = [];
    class FakeSocket {
        constructor() { this.readyState = connected ? 1 : 3; FakeSocket.last = this; }
        send(text) { sent.push(JSON.parse(text)); }
        close() { this.readyState = 3; }
    }
    FakeSocket.OPEN = 1;
    const win = {location: {hostname: '192.168.8.136'}, addEventListener() {}};
    // WiFi 實況用 fetch 讀 /api/wifi；測試不連網，直接餵一個固定回應
    const fetchStub = async () => ({json: async () => ({sta_connected: true, ssid: 'lab', rssi: -55, ip: '192.168.8.136'})});
    const ctx = vm.createContext({
        document: dom.document, window: win, WebSocket: FakeSocket, fetch: fetchStub,
        // 頁面自己的 console.log 會把測試輸出洗掉，只保留 error
        console: {log() {}, warn() {}, error: console.error},
        alert: msg => alerts.push(msg),
        setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout,
        setInterval: clock.setInterval, clearInterval: clock.clearInterval,
        Date, Math, JSON, Object, Array, Number, String, Boolean, parseInt, parseFloat, isNaN, URL, Blob
    });
    ctx.window = win;
    vm.runInContext(script, ctx, {filename: 'hardware_test.html'});
    win.onload();                                   // the page wires itself up here
    if (connected) FakeSocket.last.onopen({});      // pretend the board answered
    // value(): JSON round-trip, because objects from the VM realm have different
    // prototypes and deepStrictEqual would fail on identity alone.
    return {
        ctx, dom, clock, sent, alerts,
        run: expr => vm.runInContext(expr, ctx),
        // 讓測試可以模擬板子回傳的 WebSocket 訊息
        recv: obj => FakeSocket.last.onmessage({data: JSON.stringify(obj)}),
        value: expr => JSON.parse(JSON.stringify(vm.runInContext(expr, ctx)))
    };
}

// ---------------------------------------------------------------------------
// 1. After load every section must still be clickable.
//    (The old page set pointer-events:none on all nine, which also killed the
//    per-section start buttons and the ✅/❌ confirmation buttons.)
// ---------------------------------------------------------------------------
{
    const {dom} = load();
    for (const section of dom.sections) {
        assert.notEqual(section.style.pointerEvents, 'none', `${section.id} 載入後就被鎖住，按鈕會點不動`);
        assert.ok(section.classList.contains('collapsed'), `${section.id} 載入時應該是收合的`);
    }
}

// ---------------------------------------------------------------------------
// 2. 啟動：pressing 開始勾選檢測 must actually start the first test.
// ---------------------------------------------------------------------------
{
    const {dom, run, value, sent, clock} = load();
    TEST_IDS.forEach(id => { dom.get(`check-${id}`).checked = id === 'm1' || id === 'm2'; });
    run('startSelectedTests()');
    assert.deepEqual(value('selectedTests'), ['m1', 'm2'], '只勾兩項時 selectedTests 應該只有兩項');
    assert.equal(run('autoTestMode'), true);
    assert.ok(sent.length > 0, '按下開始之後完全沒有送出任何指令');
    assert.deepEqual(sent[0], {type: 'motorControl', motor: 'M1', speed: 80, mode: 'hardwareTest'});
    assert.ok(!dom.get('runBar').hidden, '執行中底部執行列應該出現');
    assert.ok(dom.get('progress-text').textContent.includes('1/2'), '進度要顯示第幾項／共幾項');

    // 執行：the sequence runs to the end and raises the confirmation box.
    clock.advance(10000);
    assert.equal(dom.get('m1-confirmation').style.display, 'block', 'M1 序列跑完後沒有跳出確認框');
    const stops = sent.filter(c => c.motor === 'M1' && c.speed === 0);
    assert.ok(stops.length >= 2, 'M1 序列應該含停止步驟');

    // 確認：confirming advances to the next selected test.
    sent.length = 0;
    run('confirmM1(true)');
    clock.advance(1500);
    assert.equal(run('testResults.m1'), true);
    assert.equal(dom.get('m1-confirmation').style.display, 'none');
    assert.ok(sent.some(c => c.motor === 'M2'), '確認 M1 之後沒有接著跑 M2');
    assert.ok(dom.get('progress-text').textContent.includes('2/2'));

    // 最後一項確認完 → 出報告
    clock.advance(10000);
    assert.equal(dom.get('m2-confirmation').style.display, 'block');
    run('confirmM2(true)');
    clock.advance(1500);
    assert.equal(run('autoTestMode'), false);
    assert.equal(dom.get('results-section').style.display, 'block', '全部確認完應該顯示結果');
    assert.ok(dom.get('runBar').hidden, '跑完要收掉底部執行列');
    assert.ok(dom.get('results-container').innerHTML.includes('M1'), '報告內容應該列出項目');
}

// ---------------------------------------------------------------------------
// 3. 中止：stops the motors, clears every timer, and restores the start buttons.
// ---------------------------------------------------------------------------
{
    const {dom, run, value, sent, clock} = load();
    run('startAllTests()');
    assert.equal(value('selectedTests').length, TEST_IDS.length,
        `開始全部檢測應該涵蓋 ${TEST_IDS.length} 項（TEST_ITEMS 與卡片數量要一致）`);
    clock.advance(2000);
    sent.length = 0;
    run('abortRun("測試")');
    for (const motor of ['M1', 'M2', 'M3', 'M4']) {
        assert.ok(sent.some(c => c.motor === motor && c.speed === 0), `中止時沒有停止 ${motor}`);
    }
    assert.equal(run('autoTestMode'), false);
    assert.ok(dom.get('runBar').hidden);
    assert.equal(dom.get('m1-controls').style.display, '', '中止後開始按鈕要回來');
    sent.length = 0;
    clock.advance(30000);
    assert.equal(sent.length, 0, '中止後仍有計時器在送指令');
}

// ---------------------------------------------------------------------------
// 4. 感測器測試：提早按停止要立刻跳確認框，而且不會被稍後的自動停止打斷。
// ---------------------------------------------------------------------------
{
    const {dom, run, value, sent, clock} = load();
    run('startUltrasonicTest()');
    clock.advance(2000);
    assert.ok(sent.some(c => c.test === 'ultrasonicReading'), '超音波測試應該週期性讀取');
    run('stopUltrasonicTest()');
    clock.advance(10);
    assert.equal(dom.get('ultrasonic-confirmation').style.display, 'block', '按下停止後沒有馬上跳確認框');
    sent.length = 0;
    clock.advance(30000);
    assert.equal(sent.length, 0, '停止之後仍有殘留的讀取迴圈');
}

// ---------------------------------------------------------------------------
// 5. 未連線時不得啟動（舊版會在 200ms 的讀取迴圈裡連環 alert）。
// ---------------------------------------------------------------------------
{
    const {run, sent, alerts, clock} = load({connected: false});
    run('startAllTests()');
    assert.equal(run('autoTestMode'), false, '沒連線還是進了執行模式');
    assert.equal(sent.length, 0);
    clock.advance(30000);
    assert.equal(alerts.length, 0, '未連線時不應該跳 alert（迴圈裡會洗版）');
}

// ---------------------------------------------------------------------------
// 6. 重置：清掉狀態、收合區塊、還原按鈕、停掉所有計時器。
// ---------------------------------------------------------------------------
{
    const {dom, run, value, sent, clock} = load();
    run('startAllTests()');
    clock.advance(3000);
    run('resetAllTests()');
    assert.deepEqual(value('testResults'), {});
    assert.equal(run('autoTestMode'), false);
    for (const id of TEST_IDS) {
        assert.equal(dom.get(`${id}-confirmation`).style.display, 'none', `${id} 的確認框沒有收起來`);
        assert.ok(dom.get(`${id}-test-section`).classList.contains('collapsed'));
        assert.equal(dom.get(`status-${id}`).textContent, '等待中');
    }
    sent.length = 0;
    clock.advance(30000);
    assert.equal(sent.length, 0, '重置之後仍有計時器在送指令');
}

// ---------------------------------------------------------------------------
// 7. 數位／PWM 輸出：送出 outputTest，而且收尾會把腳位關掉。
// ---------------------------------------------------------------------------
{
    const {dom, run, sent, clock} = load();
    dom.get('output-digital-pin').value = '2';
    dom.get('output-pwm-pin').value = '26';
    run('startOutputTest()');
    clock.advance(1000);
    const digital = sent.filter(c => c.test === 'outputTest' && c.pinMode === 'digital');
    assert.ok(digital.length >= 2, '數位輸出沒有送出 outputTest');
    assert.equal(digital[0].gpio, 2);
    assert.equal(digital[0].value, 1);
    clock.advance(10000);
    const pwm = sent.filter(c => c.test === 'outputTest' && c.pinMode === 'pwm');
    assert.ok(pwm.some(c => c.gpio === 26 && c.value === 255), 'PWM 應該掃到最大值 255');
    assert.equal(dom.get('output-confirmation').style.display, 'block', '輸出序列跑完要跳確認框');
    sent.length = 0;
    run('confirmOutput(true)');
    assert.ok(sent.some(c => c.pinMode === 'digital' && c.value === 0), '確認後要把數位腳位關掉');
    assert.ok(sent.some(c => c.pinMode === 'pwm' && c.value === 0), '確認後要把 PWM 腳位關掉');
}

// ---------------------------------------------------------------------------
// 8. 編碼器手轉：只讀編碼器、不驅動馬達，並由回傳值算出變化量。
// ---------------------------------------------------------------------------
{
    const {dom, run, sent, clock, recv} = load();
    run('startEncoderManualTest()');
    clock.advance(1000);
    assert.ok(sent.every(c => c.test === 'encoderReading'), '手轉測試不可以送馬達指令');
    assert.ok(sent.some(c => c.motor === 'M3') && sent.some(c => c.motor === 'M4'));
    recv({type: 'encoderValue', motor: 'M3', ticks: 100, deg: 50});
    recv({type: 'encoderValue', motor: 'M4', ticks: 0, deg: 0});
    recv({type: 'encoderValue', motor: 'M3', ticks: 460, deg: 230});
    recv({type: 'encoderValue', motor: 'M4', ticks: 3, deg: 1.5});
    run('stopEncoderManualTest()');
    clock.advance(10);
    assert.equal(dom.get('encmanual-m3').textContent, 360, 'M3 變化量算錯');
    assert.equal(dom.get('encmanual-m4').textContent, 3);
    assert.equal(dom.get('encmanual-confirmation').style.display, 'block');
}

// ---------------------------------------------------------------------------
// 9. I2C 狀態：桌上模式韌體要說「這支韌體沒有這個功能」，不是假裝沒連上。
// ---------------------------------------------------------------------------
{
    const {dom, run, sent, clock, recv} = load();
    run('startI2cTest()');
    clock.advance(100);
    assert.ok(sent.some(c => c.test === 'i2cStatus'), '沒有送出 i2cStatus 查詢');
    recv({type: 'i2cStatus', supported: false});
    assert.match(dom.get('i2c-init').textContent, /不適用/, '桌上模式韌體要明講不適用');
    recv({type: 'i2cStatus', supported: true, stats: {address: 0x36, sda: 21, scl: 22, initialized: true,
        rx: 128, bad_cmd: 0, not_ready: 1, dropped: 0, last_command: 0x64, last_comm_ms: 1000,
        since_last_ms: 250, link_recoveries: 0}});
    assert.match(dom.get('i2c-init').textContent, /0X36|0x36/i);
    assert.equal(dom.get('i2c-rx').textContent, 128);
    clock.advance(5000);
    assert.equal(dom.get('i2c-confirmation').style.display, 'block');
}

// ---------------------------------------------------------------------------
// 10. 馬達 + 編碼器：轉向判定（正轉計數要增加、反轉要減少）
// ---------------------------------------------------------------------------
{
    const {dom, run, clock, recv} = load();
    run('startM3Test()');
    clock.advance(100);
    recv({type: 'encoderValue', motor: 'M3', ticks: 0, deg: 0});       // 正轉段開始
    recv({type: 'encoderValue', motor: 'M3', ticks: 500, deg: 250});
    clock.advance(5000);                                               // 進入反轉段
    recv({type: 'encoderValue', motor: 'M3', ticks: 480, deg: 240});
    recv({type: 'encoderValue', motor: 'M3', ticks: -100, deg: -50});
    clock.advance(6000);
    assert.match(dom.get('m3-enc-dir').textContent, /方向一致/, '正增反減應判定為正常');
}

{
    // 兩段同方向 = 只讀到一相，或方向訊號沒作用
    const {dom, run, clock, recv} = load();
    run('startM4Test()');
    clock.advance(100);
    recv({type: 'encoderValue', motor: 'M4', ticks: 0, deg: 0});
    recv({type: 'encoderValue', motor: 'M4', ticks: 500, deg: 250});
    clock.advance(5000);
    recv({type: 'encoderValue', motor: 'M4', ticks: 520, deg: 260});
    recv({type: 'encoderValue', motor: 'M4', ticks: 900, deg: 450});
    clock.advance(6000);
    assert.match(dom.get('m4-enc-dir').textContent, /兩段同方向/, '同方向應該被抓出來');
}

// ---------------------------------------------------------------------------
// 11. 版面與接線的靜態檢查
// ---------------------------------------------------------------------------
assert.match(html, /<label class="test-item-card"/, '檢測項目卡要是 <label>，整張卡才能點選');
// 只擋「程式碼裡真的去拿這些 id」，說明用的註解可以提到它們
assert.doesNotMatch(html, /'progress-(m1|m2|button|complete)'/, '不要再參照已不存在的舊進度元素');
assert.doesNotMatch(html, /�/, 'HTML 裡有壞掉的字元（編碼破損）');
assert.match(html, /id="runBar"/, '少了底部執行列');
assert.match(html, /id="runLog"/, '少了執行紀錄區');
assert.doesNotMatch(html, /appendLog\('connectionStatus'/, '執行訊息不可以塞進連線狀態藥丸');
// 每個檢測項目都要有卡片、狀態列與詳細區，少一個就會在執行到它時整條鏈停住
for (const id of TEST_IDS) {
    assert.ok(html.includes(`id="check-${id}"`), `少了 ${id} 的勾選框`);
    assert.ok(html.includes(`id="card-${id}"`), `少了 ${id} 的項目卡`);
    assert.ok(html.includes(`id="${id}-test-section"`), `少了 ${id} 的詳細測試區`);
    assert.ok(html.includes(`id="${id}-confirmation"`), `少了 ${id} 的確認框`);
}
assert.match(html, /id="wifiInfo"/, '少了 WiFi 實況顯示');

console.log('hardware_test: start/run/confirm chain, abort, early stop, offline guard, reset, layout wiring: PASS');
