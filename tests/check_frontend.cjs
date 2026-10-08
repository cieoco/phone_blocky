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
// Old 「馬達」/「舵機」 blocks were removed (2026-10-09); they are now unknown blocks.
assert.throws(() => value(`getTranslator('馬達')({type:'馬達'})`), /不支援的積木/);
assert.throws(() => value(`getTranslator('舵機')({type:'舵機'})`), /不支援的積木/);
assert.deepEqual(value('new DelayNode("1000").toJson()'), {cmd:'delay',ms:1000});
assert.throws(() => value('new DelayNode("oops").toJson()'));
// Loops: repeat / while translate to firmware shapes; nesting is capped at MAX_BLOCK_DEPTH (8).
vm.runInContext(`
    var fakeBlock = (type, fields, inputs) => ({type, getFieldValue: n => fields[n],
        getInputTargetBlock: n => inputs[n] || null, getNextBlock: () => null});
    var delay10 = () => fakeBlock('arduino_delay', {}, {});
`, context);
assert.deepEqual(value('new RepeatNode(new MathNumberNode(3), [new DelayNode("100")]).toJson()'),
    {command:'repeat', times:{command:'math_number', number:3}, do:[{cmd:'delay', ms:100}]});
assert.deepEqual(value('new WhileNode(new LogicBooleanNode("TRUE"), [], "UNTIL").toJson()'),
    {command:'while', mode:'UNTIL', condition:{command:'logic_boolean', value:true}, do:[]});
assert.deepEqual(value(`translateRepeat(fakeBlock('controls_repeat_ext', {}, {
        TIMES: fakeBlock('math_number', {NUM: 5}, {}),
        DO: fakeBlock('motor_stop', {MOTOR: '1'}, {})})).toJson()`),
    {command:'repeat', times:{command:'math_number', number:5}, do:[{cmd:'stop', motor:1}]});
assert.deepEqual(value(`translateWhile(fakeBlock('controls_whileUntil', {MODE: 'WHILE'}, {
        BOOL: fakeBlock('logic_boolean', {BOOL: 'TRUE'}, {})})).toJson()`),
    {command:'while', mode:'WHILE', condition:{command:'logic_boolean', value:true}, do:[]});
assert.throws(() => value(`translateRepeat(fakeBlock('controls_repeat_ext', {}, {}))`), /次數/);
assert.throws(() => value(`translateWhile(fakeBlock('controls_whileUntil', {MODE: 'WHILE'}, {}))`), /條件/);
vm.runInContext(`
    var nestRepeat = n => n === 0 ? new DelayNode("10")
        : new RepeatNode(new MathNumberNode(2), [nestRepeat(n - 1)]);
    var setupWith = node => Object.assign(Object.create(arduino_setupNode.prototype), {body: [node]});
`, context);
assert.equal(value('progNestingDepth([nestRepeat(3).toJson()])'), 3);
assert.equal(value('progNestingDepth([new IfNode(null, [], []).toJson()])'), 1);
assert.equal(value('buildProgJson([setupWith(nestRepeat(8))]).setup.length'), 1);
assert.throws(() => value('buildProgJson([setupWith(nestRepeat(9))])'), /最多 8 層/);
vm.runInContext(`
    const deg90 = {type:'math_number',getFieldValue:n=>({NUM:90})[n]};
    const motor = {type:'motor_position',getFieldValue:n=>({MOTOR:'3'})[n],
        getInputTargetBlock:n=>({DEG:deg90})[n]||null,getNextBlock:()=>null};
    const setup = {type:'arduino_setup',getParent:()=>null,getInputTargetBlock:()=>motor,getNextBlock:()=>null};
    workspace = {getTopBlocks:()=>[setup]};
    Blockly = {Xml:{workspaceToDom:()=>null,domToText:()=>'<xml/>'}};
`, context);
assert.deepEqual(value('buildProgPayload().json'), {mode:'PROG',setup:[{cmd:'move_to',motor:3,deg:9000}],loop:[]});
// Value slots: constants keep the old JSON (range-checked); expressions go to the firmware as objects.
vm.runInContext('var vx = new VariableGetNode("x");', context);
const vxJson = value('vx.toJson()');
assert.deepEqual(value('new MotorPwmNode("1", "BACKWARD", vx).toJson()'),
    {cmd:'pwm', motor:1, duty:{command:'math_arithmetic', operator:'MULTIPLY', left:vxJson, right:-1}});
assert.deepEqual(value('new MotorPwmNode("2", "FORWARD", vx).toJson()'), {cmd:'pwm', motor:2, duty:vxJson});
assert.deepEqual(value('new MotorPwmNode("2", "STOP", vx).toJson()'), {cmd:'stop', motor:2});
assert.deepEqual(value('new MotorPositionNode("3", vx).toJson()'),
    {cmd:'move_to', motor:3, deg:{command:'math_arithmetic', operator:'MULTIPLY', left:vxJson, right:100}});
assert.deepEqual(value('new ServoSetNode("1", vx).toJson()'), {cmd:'servo', ch:1, deg:vxJson});
assert.deepEqual(value('new ServoSetNode("1", 45).toJson()'), {cmd:'servo', ch:1, deg:45});
assert.deepEqual(value('new MotorSpeedNode("4", vx).toJson()'), {cmd:'speed', motor:4, rpm:vxJson});
assert.deepEqual(value('new DelayNode(vx).toJson()'), {cmd:'delay', ms:vxJson});
assert.throws(() => value('new MotorPwmNode("1", "FORWARD", 150).toJson()'), /馬達動力/);
assert.throws(() => value('new ServoSetNode("1", 200).toJson()'), /舵機角度/);
assert.throws(() => value('new MotorSpeedNode("3", 30).toJson()'), /轉速/);
assert.throws(() => value(`translateMotorPwm(fakeBlock('motor_pwm', {MOTOR:'1', DIRECTION:'FORWARD'}, {}))`), /沒有放數值/);
assert.equal(value(`translateMotorPwm(fakeBlock('motor_pwm', {MOTOR:'1', DIRECTION:'FORWARD'},
    {PWM: fakeBlock('math_number', {NUM: 70}, {})})).toJson().duty`), 70);
// AND / OR
assert.deepEqual(value('new LogicOperationNode("OR", new LogicBooleanNode("TRUE"), vx).toJson()'),
    {command:'logic_operation', operator:'OR', left:{command:'logic_boolean', value:true}, right:vxJson});
assert.throws(() => value(`translateLogicOperation(fakeBlock('logic_operation', {OP:'AND'}, {}))`), /且／或/);
// Unknown blocks fail loudly; blocks outside setup/loop are counted, not translated
assert.throws(() => value(`getTranslator('mystery_block')({type:'mystery_block'})`), /不支援的積木/);
vm.runInContext(`
    var strayRepeat = fakeBlock('controls_repeat_ext', {}, {});   // incomplete, would throw if translated
    strayRepeat.getParent = () => null;
    var setupOnly = {type:'arduino_setup',getParent:()=>null,getInputTargetBlock:()=>null,getNextBlock:()=>null};
    workspace = {getTopBlocks:()=>[setupOnly, strayRepeat]};
`, context);
assert.equal(value('parseWorkspaceToIR(workspace).strayBlocks'), 1);
// Math blocks: abs / random / constrain / map translate to named-argument JSON; empty slots fail loudly
const n = v => ({command:'math_number', number:v});
vm.runInContext(`var num = v => fakeBlock('math_number', {NUM: v}, {});`, context);
assert.deepEqual(value(`translateMathAbs(fakeBlock('math_abs', {}, {VALUE: num(-5)})).toJson()`),
    {command:'math_abs', value:n(-5)});
assert.deepEqual(value(`translateMathRandomInt(fakeBlock('math_random_int', {}, {FROM: num(1), TO: num(6)})).toJson()`),
    {command:'math_random', from:n(1), to:n(6)});
assert.deepEqual(value(`translateMathConstrain(fakeBlock('math_constrain', {}, {VALUE: num(150), LOW: num(0), HIGH: num(100)})).toJson()`),
    {command:'math_constrain', value:n(150), low:n(0), high:n(100)});
assert.deepEqual(value(`translateMathMap(fakeBlock('math_map', {}, {VALUE: num(50), FROM_LOW: num(0), FROM_HIGH: num(100),
        TO_LOW: num(0), TO_HIGH: num(180)})).toJson()`),
    {command:'math_map', value:n(50), fromLow:n(0), fromHigh:n(100), toLow:n(0), toHigh:n(180)});
assert.deepEqual(value(`translateMathArithmetic(fakeBlock('math_arithmetic', {OP:'MODULO'}, {A: num(7), B: num(3)})).toJson()`),
    {command:'math_arithmetic', operator:'MODULO', left:n(7), right:n(3)});
assert.throws(() => value(`translateMathMap(fakeBlock('math_map', {}, {VALUE: num(1)}))`), /對應換算/);
assert.throws(() => value(`translateMathArithmetic(fakeBlock('math_arithmetic', {OP:'ADD'}, {A: num(1)}))`), /運算/);
assert.throws(() => value(`translateLogicCompare(fakeBlock('logic_compare', {OP:'LT'}, {B: num(1)}))`), /比較/);
// Subroutines: definitions go to top-level "procedures", calls are {command:"call"}; depth counts calls; recursion refused
vm.runInContext(`
    var procDef = (name, params, body) => Object.assign(fakeBlock('procedures_defnoreturn', {NAME: name}, {STACK: body || null}),
        {getVarModels: () => params.map(id => ({getId: () => id})), getParent: () => null});
    var procCall = (name, argBlocks) => Object.assign(fakeBlock('procedures_callnoreturn', {},
        Object.fromEntries((argBlocks || []).map((b, i) => ['ARG' + i, b]))),
        {getProcedureCall: () => name, getVars: () => (argBlocks || []).map((_, i) => 'p' + i)});
    var loopWith = node => Object.assign(Object.create(arduino_loopNode.prototype), {body: [node]});
`, context);
assert.deepEqual(value(`translateProcedureDef(procDef('夾爪', ['vid1'], procCall('其他', []))).toJson()`),
    {name:'夾爪', params:['vid1'], body:[{command:'call', name:'其他', args:[]}]});
assert.deepEqual(value(`translateProcedureCall(procCall('夾爪', [num(60)])).toJson()`),
    {command:'call', name:'夾爪', args:[n(60)]});
assert.throws(() => value(`translateProcedureCall(Object.assign(procCall('夾爪', [num(1)]), {getInputTargetBlock: () => null}))`), /參數/);
assert.throws(() => value(`getTranslator('procedures_defreturn')({})`), /回傳值/);
// payload: procedures present only when defined; old shape unchanged otherwise
assert.deepEqual(value(`buildProgJson([loopWith(new ProcedureCallNode('A', [])), new ProcedureDefNode('A', [], [new DelayNode(10)])])`),
    {mode:'PROG', setup:[], loop:[{command:'call', name:'A', args:[]}], procedures:[{name:'A', params:[], body:[{cmd:'delay', ms:10}]}]});
assert.equal(value(`'procedures' in buildProgJson([loopWith(new DelayNode(10))])`), false);
// call counts as a level: loop → call A (1) → A has 7 nested repeats → 8 total OK; 8 nested → 9 → refused
assert.equal(value(`buildProgJson([loopWith(new ProcedureCallNode('A', [])), new ProcedureDefNode('A', [], [nestRepeat(7)])]).loop.length`), 1);
assert.throws(() => value(`buildProgJson([loopWith(new ProcedureCallNode('A', [])), new ProcedureDefNode('A', [], [nestRepeat(8)])])`), /最多 8 層/);
// recursion (direct and indirect) and unknown names refused
assert.throws(() => value(`buildProgJson([new ProcedureDefNode('A', [], [new ProcedureCallNode('A', [])])])`), /不能直接或間接呼叫自己/);
assert.throws(() => value(`buildProgJson([new ProcedureDefNode('A', [], [new ProcedureCallNode('B', [])]),
    new ProcedureDefNode('B', [], [new RepeatNode(new MathNumberNode(2), [new ProcedureCallNode('A', [])])])])`), /不能直接或間接呼叫自己/);
assert.throws(() => value(`buildProgJson([loopWith(new ProcedureCallNode('不存在', []))])`), /找不到副程式/);
// top-level procedure definitions are translated, not counted as stray
vm.runInContext(`workspace = {getTopBlocks: () => [procDef('B', [], null)]};`, context);
assert.equal(value('parseWorkspaceToIR(workspace).strayBlocks'), 0);
assert.equal(value('parseWorkspaceToIR(workspace)[0] instanceof ProcedureDefNode'), true);
console.log('Frontend syntax, IR units, flat payload, loops, nesting limit, value slots, AND/OR, math functions, subroutines, unknown/stray blocks: PASS');
