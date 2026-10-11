// Offline only: no browser, fetch, WebSocket, or board access.
//
// Validates the built-in examples in data/examples/ against the REAL block definitions
// (data/appBlock.js) and the REAL translators (appIr.js / appJson.js / app.js):
//   1. every block type exists and has a translator
//   2. every field / value-input / statement-input name is one the block actually has
//   3. every dropdown field value is one of the block's legal options
//   4. every value input is filled (the translators throw on empty slots at runtime)
//   5. the whole example really turns into a valid PROG JSON (setup + loop, depth <= 8)
// Hand-written XML is the only practical way to author examples, and a broken example is
// worse than no example, so this has to be checkable without a board.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const EX_DIR = 'data/examples';

// ---------------------------------------------------------------------------
// 1. What fields and inputs does each block really have?
//    Run every block's init() against a recorder instead of a browser Blockly.
// ---------------------------------------------------------------------------
function collectBlockSpecs() {
    const Blockly = {
        Blocks: {},
        Msg: {},
        ALIGN_LEFT: 0, ALIGN_CENTRE: 1, ALIGN_RIGHT: 2,
        FieldTextInput: class { constructor(v) { this.value = v; } },
        FieldNumber: class { constructor(v) { this.value = v; } },
        FieldCheckbox: class { constructor(v) { this.value = v; } },
        FieldVariable: class { constructor(v) { this.value = v; } },
        FieldLabel: class { constructor(v) { this.value = v; } },
        // Only dropdowns constrain the legal values, so only they report them.
        FieldDropdown: class { constructor(opts) { this.options = opts.map(o => o[1]); } }
    };
    vm.runInNewContext(read('data/appBlock.js'), {Blockly, console}, {filename: 'appBlock.js'});

    const specs = {};
    for (const [type, def] of Object.entries(Blockly.Blocks)) {
        const spec = {fields: {}, valueInputs: new Set(), statementInputs: new Set()};
        const input = () => {
            const base = {
                appendField(field, name) {
                    if (name) spec.fields[name] = field && field.options ? field.options : null;
                    return proxy;
                }
            };
            // Anything else chained on an input (setCheck / setAlign / …) returns the input itself,
            // so the chain in appBlock.js keeps working however long it is.
            const proxy = new Proxy(base, {get: (t, k) => (k in t ? t[k] : () => proxy)});
            return proxy;
        };
        const base = {
            appendDummyInput: () => input(),
            appendValueInput: n => { spec.valueInputs.add(n); return input(); },
            appendStatementInput: n => { spec.statementInputs.add(n); return input(); }
        };
        // Everything else a block's init() may call (setColour, setTooltip, mutators, …) is a no-op.
        const block = new Proxy(base, {get: (t, k) => (k in t ? t[k] : () => block)});
        def.init.call(block);
        specs[type] = spec;
    }
    return specs;
}

// Blocks that come from Blockly core, not appBlock.js. Examples may use them, but their
// field/input names can't be recorded here, so they're listed explicitly.
const CORE_BLOCKS = {
    procedures_defnoreturn: {fields: {NAME: null}, valueInputs: new Set(), statementInputs: new Set(['STACK'])},
    procedures_callnoreturn: {fields: {NAME: null}, valueInputs: new Set(), statementInputs: new Set()},
    variables_get: {fields: {VAR: null}, valueInputs: new Set(), statementInputs: new Set()},
    variables_set: {fields: {VAR: null}, valueInputs: new Set(['VALUE']), statementInputs: new Set()},
    math_change: {fields: {VAR: null}, valueInputs: new Set(['DELTA']), statementInputs: new Set()}
};

// ---------------------------------------------------------------------------
// 2. Minimal XML parser — node has none built in, and the repo has no dependencies.
//    Only the subset we author by hand: tags, quoted attributes, text, comments.
// ---------------------------------------------------------------------------
function parseXml(text, file) {
    const TAG = /<!--[\s\S]*?-->|<\?[\s\S]*?\?>|<(\/?)([A-Za-z_][\w.:-]*)((?:\s+[\w:.-]+\s*=\s*"[^"]*")*)\s*(\/?)>/g;
    const unescape = s => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>')
        .replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&amp;/g, '&');
    const rootNode = {tag: '#root', attrs: {}, children: [], text: ''};
    const stack = [rootNode];
    let last = 0;
    for (let m; (m = TAG.exec(text));) {
        const [whole, closing, tag, attrText, selfClosing] = m;
        const chunk = text.slice(last, m.index);
        if (chunk.trim()) stack[stack.length - 1].text += unescape(chunk);
        last = m.index + whole.length;
        if (whole.startsWith('<!--') || whole.startsWith('<?')) continue;
        if (closing) {
            const open = stack.pop();
            assert.ok(open && open.tag === tag, `${file}: </${tag}> 對不上 <${open && open.tag}>`);
            continue;
        }
        const attrs = {};
        for (const a of attrText.matchAll(/([\w:.-]+)\s*=\s*"([^"]*)"/g)) attrs[a[1]] = unescape(a[2]);
        const node = {tag, attrs, children: [], text: ''};
        stack[stack.length - 1].children.push(node);
        if (!selfClosing) stack.push(node);
    }
    assert.equal(stack.length, 1, `${file}: 有標籤沒有關閉`);
    assert.equal(rootNode.children.length, 1, `${file}: 最外層必須只有一個 <xml>`);
    assert.equal(rootNode.children[0].tag, 'xml', `${file}: 最外層必須是 <xml>`);
    return rootNode.children[0];
}

// ---------------------------------------------------------------------------
// 3. Check every name against the block specs, then hand the tree to the real translators.
// ---------------------------------------------------------------------------
const ALLOWED_CHILDREN = new Set(['field', 'value', 'statement', 'next', 'comment', 'mutation']);

function checkBlockNames(node, specs, file) {
    const type = node.attrs.type;
    assert.ok(type, `${file}: <${node.tag}> 少了 type`);
    const spec = specs[type] || CORE_BLOCKS[type];
    assert.ok(spec, `${file}: 不認得的積木「${type}」（已刪除的舊積木，或打錯字）`);
    for (const child of node.children) {
        assert.ok(ALLOWED_CHILDREN.has(child.tag), `${file}: ${type} 裡有非法的 <${child.tag}>`);
        const name = child.attrs.name;
        if (child.tag === 'field') {
            assert.ok(name in spec.fields, `${file}: ${type} 沒有欄位「${name}」`);
            const legal = spec.fields[name];
            if (legal) {
                assert.ok(legal.includes(child.text.trim()),
                    `${file}: ${type}.${name} = "${child.text.trim()}" 不在下拉選單 [${legal}] 裡`);
            }
        } else if (child.tag === 'value') {
            assert.ok(spec.valueInputs.has(name), `${file}: ${type} 沒有數值插槽「${name}」`);
        } else if (child.tag === 'statement') {
            assert.ok(spec.statementInputs.has(name), `${file}: ${type} 沒有積木插槽「${name}」`);
        }
    }
    // Empty value inputs are a runtime error in the translators, so they're a bug in an example.
    // A present-but-empty <value> counts as empty — that is exactly what a deleted block leaves behind.
    for (const input of spec.valueInputs) {
        const slot = node.children.find(c => c.tag === 'value' && c.attrs.name === input);
        const filled = slot && slot.children.some(c => c.tag === 'block' || c.tag === 'shadow');
        assert.ok(filled, `${file}: ${type} 的數值插槽「${input}」是空的`);
    }
    for (const child of node.children) {
        for (const grand of child.children) {
            if (grand.tag === 'block' || grand.tag === 'shadow') checkBlockNames(grand, specs, file);
        }
    }
}

// XML node -> the small slice of the Blockly block API the translators actually use.
function fakeBlock(node, parent) {
    const firstBlock = (tag, name) => {
        const slot = node.children.find(c => c.tag === tag && (!name || c.attrs.name === name));
        const child = slot && slot.children.find(c => c.tag === 'block' || c.tag === 'shadow');
        return child || null;
    };
    const self = {
        type: node.attrs.type,
        getFieldValue(name) {
            const f = node.children.find(c => c.tag === 'field' && c.attrs.name === name);
            return f ? f.text.trim() : undefined;
        },
        getInputTargetBlock(name) {
            const child = firstBlock('value', name) || firstBlock('statement', name);
            return child ? fakeBlock(child, self) : null;
        },
        getNextBlock() {
            const child = firstBlock('next', null);
            return child ? fakeBlock(child, self) : null;
        },
        isEnabled: () => node.attrs.disabled !== 'true',
        getParent: () => parent || null
    };
    return self;
}

// ---------------------------------------------------------------------------
// Run the real front-end code, then validate every example end to end.
// ---------------------------------------------------------------------------
const context = vm.createContext({console, window: {addEventListener() {}}});
vm.runInContext(read('data/appIr.js') + '\n' + read('data/appJson.js') + '\n' + read('data/app.js'), context);
const specs = collectBlockSpecs();

// The manifest and the files must agree, or the drawer lists examples that 404.
const manifest = JSON.parse(read(`${EX_DIR}/index.json`));
assert.ok(Array.isArray(manifest.examples) && manifest.examples.length, 'index.json 沒有任何範例');
const xmlFiles = fs.readdirSync(path.join(root, EX_DIR)).filter(f => f.endsWith('.xml')).sort();
const ids = manifest.examples.map(e => e.id);
assert.deepEqual(ids.slice().sort().map(id => `${id}.xml`), xmlFiles, 'index.json 的 id 與 .xml 檔案對不起來');
assert.equal(new Set(ids).size, ids.length, 'index.json 有重複的 id');
for (const ex of manifest.examples) {
    // The id goes into a URL path in app.js, and the name/desc are the only guidance a
    // student gets before loading an example, so neither may be missing.
    assert.match(ex.id, /^[A-Za-z0-9_-]+$/, `範例 id「${ex.id}」只能用英數與 - _`);
    assert.ok(ex.name && ex.desc, `範例 ${ex.id} 少了 name 或 desc`);
    assert.ok(ex.desc.length <= 120, `範例 ${ex.id} 的 desc 太長（抽屜裡顯示不完）`);
}

let totalBytes = 0;
for (const ex of manifest.examples) {
    const file = `${EX_DIR}/${ex.id}.xml`;
    const text = read(file);
    totalBytes += Buffer.byteLength(text);
    assert.ok(Buffer.byteLength(text) <= 8192, `${file}: 單支範例超過 8 KB，拆小一點`);

    const xml = parseXml(text, file);
    const tops = xml.children.filter(c => c.tag === 'block');
    assert.ok(tops.length >= 1, `${file}: 沒有任何積木`);
    tops.forEach(t => checkBlockNames(t, specs, file));

    // Every type in the file must also have a translator, or running it throws at runtime.
    const types = new Set(text.match(/type="[^"]+"/g).map(s => s.slice(6, -1)));
    for (const type of types) {
        assert.doesNotThrow(() => context.getTranslator(type), `${file}: 「${type}」沒有翻譯器`);
    }

    // Finally: does it really become a valid PROG program?
    context.exampleTops = tops.map(n => fakeBlock(n, null));
    const prog = JSON.parse(JSON.stringify(vm.runInContext(`
        (function () {
            var chain = [];
            for (var b = exampleTops[0]; b; b = b.getNextBlock()) chain.push(b);
            var ws = {getTopBlocks: () => chain, getAllBlocks: () => chain};
            return buildProgJson(parseWorkspaceToIR(ws), ws);
        })()
    `, context)));
    assert.equal(prog.mode, 'PROG', `${file}: 不是 PROG 程式`);
    assert.ok(Array.isArray(prog.setup) && Array.isArray(prog.loop), `${file}: 缺少 setup 或 loop`);
    assert.ok(prog.loop.length || prog.setup.length, `${file}: setup 與 loop 都是空的`);
    // Examples must show both halves of the shape students will copy.
    assert.ok(text.includes('arduino_setup') && text.includes('arduino_loop'),
        `${file}: 範例要同時含 setup 與 loop 積木`);
    const commands = [...prog.setup, ...prog.loop];
    for (const c of commands) {
        assert.ok(c && (c.cmd || c.command), `${file}: 產出了沒有 cmd/command 的指令 ${JSON.stringify(c)}`);
    }
}
assert.ok(totalBytes <= 65536, '範例總量超過 64 KB，LittleFS 空間要重新評估');

// Known-good output for the examples shipped today; catches silent protocol drift
// (deg is fixed-point ×100, duty is signed ±100).
const EXPECTED = {
    '01-motor-pwm': prog => {
        assert.deepEqual(prog.setup, [{cmd: 'stop'}]);   // MOTOR=0 (全部) 不帶 motor 欄位
        assert.deepEqual(prog.loop, [
            {cmd: 'pwm', motor: 1, duty: 50}, {cmd: 'delay', ms: 1000},
            {cmd: 'stop', motor: 1}, {cmd: 'delay', ms: 1000}
        ]);
    },
    '02-servo-angle': prog => {
        assert.deepEqual(prog.setup, [{cmd: 'servo', ch: 1, deg: 0}]);
        assert.deepEqual(prog.loop[0], {cmd: 'servo', ch: 1, deg: 90});
        assert.deepEqual(prog.loop[2], {cmd: 'servo', ch: 1, deg: 0});
    },
    '03-ultrasonic-stop': prog => {
        assert.equal(prog.loop[1].command, 'if');
        assert.deepEqual(prog.loop[1].then, [{cmd: 'stop', motor: 1}]);
        assert.deepEqual(prog.loop[1].else, [{cmd: 'pwm', motor: 1, duty: 40}]);
    }
};
for (const ex of manifest.examples) {
    const check = EXPECTED[ex.id];
    if (!check) continue;
    const xml = parseXml(read(`${EX_DIR}/${ex.id}.xml`), ex.id);
    context.exampleTops = xml.children.filter(c => c.tag === 'block').map(n => fakeBlock(n, null));
    check(JSON.parse(JSON.stringify(vm.runInContext(`
        (function () {
            var chain = [];
            for (var b = exampleTops[0]; b; b = b.getNextBlock()) chain.push(b);
            var ws = {getTopBlocks: () => chain, getAllBlocks: () => chain};
            return buildProgJson(parseWorkspaceToIR(ws), ws);
        })()
    `, context))));
}

// A typo'd element id would make the examples panel silently show nothing, so check the wiring too.
const page = read('data/blockly.html');
const app = read('data/app.js');
assert.match(page, /id="exampleList"/, 'blockly.html 少了 #exampleList');
assert.match(app, /getElementById\('exampleList'\)/, 'app.js 沒有接上 #exampleList');
assert.match(page, /id="examplesBtn"/, 'blockly.html 的頂列少了 📚 範例按鈕');
assert.match(page, /id="examplesSheet"/, 'blockly.html 少了範例面板');
assert.match(app, /\$\('examplesBtn'\)/, 'app.js 沒有接上頂列的範例按鈕');
assert.match(page, /appBlock\.js\?v=([\w-]+)"/, 'blockly.html 的 script 少了 ?v= 版本字串');

console.log(`examples ok: ${manifest.examples.length} 支，共 ${(totalBytes / 1024).toFixed(1)} KB`);
