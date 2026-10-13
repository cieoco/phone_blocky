#!/usr/bin/env node
'use strict';
// Generates data/examples/*.xml and data/examples/index.json from tools/examples_src/<id>.cjs.
//   node tools/gen_examples.cjs            write files
//   node tools/gen_examples.cjs --check    exit 1 if files on disk differ from the sources
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const SRC = path.join(__dirname, 'examples_src');
const OUT = path.join(root, 'data', 'examples');
const {SHADOW_INPUTS} = require('./examples_src/_dsl.cjs');

const SERIES = {A: '動起來', B: '角度', C: '感測與腳位', D: '邏輯資料', E: '整合', F: '對外 I2C', G: '小車副程式'};

const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

// A bare number becomes math_number; shadow vs real block follows the toolbox convention per input.
function valueOf(parentType, name, v) {
  if (typeof v === 'number') {
    return {type: 'math_number', fields: {NUM: v}, values: {}, statements: {},
      shadow: (SHADOW_INPUTS[parentType] || []).includes(name)};
  }
  return v;
}

// Render the first block of `chain` plus the rest as its <next> chain, at indent level `lv`.
function renderChain(chain, lv, used) {
  const [b, ...rest] = chain;
  const p = ' '.repeat(lv);
  const tag = b.shadow ? 'shadow' : 'block';
  used.add(b.type);
  let out = `${p}<${tag} type="${b.type}"${b.pos || ''}>\n`;
  if (b.comment) {
    out += `${p}  <comment pinned="false" h="${b.comment.h}" w="${b.comment.w}">${esc(b.comment.text)}</comment>\n`;
  }
  if (b.mutation) {
    const {args = [], ...rest} = b.mutation;
    const attrs = Object.entries(rest).map(([k, v]) => ` ${k}="${esc(v)}"`).join('');
    out += `${p}  <mutation${attrs}>${args.map(a => `<arg name="${esc(a)}"></arg>`).join('')}</mutation>
`;
  }
  for (const [k, v] of Object.entries(b.fields)) out += `${p}  <field name="${k}">${esc(v)}</field>\n`;
  for (const [k, raw] of Object.entries(b.values)) {
    const v = valueOf(b.type, k, raw);
    if (!v) throw new Error(`${b.type}.${k} is empty`);
    out += `${p}  <value name="${k}">\n${renderChain([v], lv + 2, used)}${p}  </value>\n`;
  }
  for (const [k, list] of Object.entries(b.statements)) {
    if (list && list.length) out += `${p}  <statement name="${k}">\n${renderChain(list, lv + 2, used)}${p}  </statement>\n`;
  }
  if (rest.length) out += `${p}  <next>\n${renderChain(rest, lv + 2, used)}${p}  </next>\n`;
  return out + `${p}</${tag}>\n`;
}

function renderExample(ex) {
  const used = new Set();
  const top = {type: 'arduino_setup', fields: {}, values: {}, statements: {SETUP_BODY: ex.setup || []}, pos: ' x="20" y="20"'};
  const loop = {type: 'arduino_loop', fields: {}, values: {}, statements: {LOOP_BODY: ex.loop || []}};
  let xml = renderChain([top, loop], 1, used);
  let y = 300;
  for (const extra of ex.extra || []) { // optional extra top-level blocks (e.g. procedure definitions)
    extra.pos = ` x="20" y="${y}"`;
    xml += renderChain([extra], 1, used);
    y += 200;
  }
  return {xml: `<xml xmlns="https://developers.google.com/blockly/xml">\n${xml}</xml>\n`, blocks: [...used].sort()};
}

function build() {
  const files = {};
  const entries = [];
  const srcs = fs.readdirSync(SRC).filter(f => f.endsWith('.cjs') && !f.startsWith('_')).sort();
  for (const f of srcs) {
    const ex = require(path.join(SRC, f));
    const m = ex.meta;
    if (f !== `${m.id}.cjs`) throw new Error(`${f}: meta.id "${m.id}" must match the file name`);
    if (!SERIES[m.series]) throw new Error(`${f}: unknown series "${m.series}"`);
    const r = renderExample(ex);
    files[`${m.id}.xml`] = r.xml;
    entries.push({id: m.id, name: m.name, desc: m.desc, hw: m.hw, level: m.level, series: m.series, blocks: r.blocks});
  }
  const NL = String.fromCharCode(10);
  files['index.json'] = ['{"version": 1,', `"series": ${JSON.stringify(SERIES)},`, '"examples": [',
    entries.map(e => JSON.stringify(e)).join(',' + NL), ']}'].join(NL) + NL;
  return files;
}

const norm = s => s.replace(/\r\n/g, '\n');
const files = build();
if (process.argv.includes('--check')) {
  const bad = [];
  for (const [name, text] of Object.entries(files)) {
    const p = path.join(OUT, name);
    if (!fs.existsSync(p) || norm(fs.readFileSync(p, 'utf8')) !== text) bad.push(name);
  }
  for (const f of fs.readdirSync(OUT)) {
    if (/\.(xml|json)$/.test(f) && !(f in files)) bad.push(`${f} (no source)`);
  }
  if (bad.length) {
    console.error(`範例檔與 tools/examples_src 不一致（請改來源後執行 node tools/gen_examples.cjs）：\n  ${bad.join('\n  ')}`);
    process.exit(1);
  }
  console.log(`gen_examples --check ok: ${Object.keys(files).length - 1} 支`);
} else {
  for (const f of fs.readdirSync(OUT)) {
    if (/\.xml$/.test(f) && !(f in files)) fs.unlinkSync(path.join(OUT, f)); // stale (renamed/removed) example
  }
  for (const [name, text] of Object.entries(files)) fs.writeFileSync(path.join(OUT, name), text);
  console.log(`generated ${Object.keys(files).length - 1} examples + index.json`);
}
