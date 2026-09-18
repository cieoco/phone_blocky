// Offline only: no browser, fetch, WebSocket, or board access.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const html = read('data/ai.html');
const context = vm.createContext({console, window: {addEventListener() {}}});
const constants = html.slice(html.indexOf('    const ALLOWED_CMDS'), html.indexOf('    const MAX_COMMANDS'));
const validator = html.slice(html.indexOf('    function validateProg('), html.indexOf('    //', html.indexOf('    function countList(')));
vm.runInContext(constants + '\nconst MAX_COMMANDS = 64;\n' + validator, context);
if (process.argv.includes('--validate')) {
    const cases = JSON.parse(fs.readFileSync(0, 'utf8'));
    context.cases = cases;
    process.stdout.write(JSON.stringify(vm.runInContext('cases.map(x => validateProg(x).length === 0)', context)));
    process.exit(0);
}
// Syntax-check every application JS and every inline script, without executing page startup.
for (const file of fs.readdirSync(path.join(root, 'data'))) {
    if (file.endsWith('.js') && !file.includes('compressed')) new vm.Script(read('data/' + file), {filename: file});
    if (file.endsWith('.html')) {
        for (const match of read('data/' + file).matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)) {
            new vm.Script(match[1], {filename: file});
        }
    }
}
vm.runInContext(read('data/appIr.js') + '\n' + read('data/appJson.js') + '\n' + read('data/app.js'), context);
const value = s => JSON.parse(JSON.stringify(vm.runInContext(s, context)));
assert.deepEqual(value('new MotorPositionNode("3", 90).toJson()'), {cmd:'move_to',motor:3,deg:9000});
assert.deepEqual(value('new MotorMoveByNode("4", -45.25).toJson()'), {cmd:'move_by',motor:4,deg:-4525});
assert.deepEqual(value('new MotorPwmNode("1", "BACKWARD", 60).toJson()'), {cmd:'pwm',motor:1,duty:-60});
assert.deepEqual(value('new MotorNode("1", "BACKWARD", 60).toJson()'), {cmd:'pwm',motor:1,duty:-60});
assert.deepEqual(value('new ServoNode("2", "90").toJson()'), {command:'servo_control',servo:'2',angle:'90'});
assert.deepEqual(value('new DelayNode("1000").toJson()'), {cmd:'delay',ms:1000});
assert.throws(() => value('new DelayNode("oops").toJson()'));
assert.throws(() => value('translateRepeat({})'));
assert.throws(() => value('translateWhile({})'));
vm.runInContext(`
    const motor = {type:'motor_position',getFieldValue:n=>({MOTOR:'3',DEG:90})[n],getNextBlock:()=>null};
    const setup = {type:'arduino_setup',getParent:()=>null,getInputTargetBlock:()=>motor,getNextBlock:()=>null};
    workspace = {getTopBlocks:()=>[setup]};
    Blockly = {Xml:{workspaceToDom:()=>null,domToText:()=>'<xml/>'}};
`, context);
assert.deepEqual(value('buildProgPayload().json'), {mode:'PROG',setup:[{cmd:'move_to',motor:3,deg:9000}],loop:[]});
console.log('Frontend syntax, IR units, legacy shapes, flat payload, and unsupported loops: PASS');
