const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const ctx = vm.createContext({console, window:{addEventListener(){}}});
for (const f of ['appIr.js','appJson.js','app.js']) vm.runInContext(fs.readFileSync(path.join(root,'data',f),'utf8'),ctx);
const run = s => vm.runInContext(s,ctx);
const value = s => JSON.parse(JSON.stringify(run(s)));
run(`var block = (type,fields,inputs={}) => ({type,getFieldValue:k=>fields[k],getInputTargetBlock:k=>inputs[k]||null});
var oled = getTranslator('oled_text')(block('oled_text',{ADDRESS:'60',ROW:'0',TEXT:'Hello'}));`);
assert.deepEqual(value('oled.toJson()'),{command:'oled_text',address:60,row:0,text:'Hello'});
assert.deepEqual(value("getTranslator('oled_clear')(block('oled_clear',{ADDRESS:'61'})).toJson()"),{command:'oled_clear',address:61});
assert.deepEqual(value("getTranslator('oled_value')(block('oled_value',{ADDRESS:'60',ROW:'3'},{VALUE:block('arduino_ultrasonic',{TRIG:'2',ECHO:'33'})})).toJson()"),
  {command:'oled_value',address:60,row:3,value:{command:'arduinoUltrasonic',trigPin:'2',echoPin:'33'}});
for (const fields of ["{ADDRESS:'59',ROW:'0',TEXT:'ok'}","{ADDRESS:'60',ROW:'4',TEXT:'ok'}","{ADDRESS:'60',ROW:'0',TEXT:'中文'}","{ADDRESS:'60',ROW:'0',TEXT:'1234567890123456789012'}"]) {
  assert.throws(()=>run(`getTranslator('oled_text')(block('oled_text',${fields})).toJson()`));
}
assert.throws(()=>run("getTranslator('oled_value')(block('oled_value',{ADDRESS:'60',ROW:'0'}))"));
// Real payload builder must reject conflicts inside procedures, loops and value expressions.
run('var raw = json => ({toJson:()=>json});');
for (const conflict of [
  '{command:"blockly_func_set",idx:0,value:1}',
  '{command:"if",condition:{command:"blockly_func_get",idx:0},then:[]}',
  '{command:"repeat",times:2,do:[{command:"digitalWrite",pin:"21",value:1}]}',
  '{command:"oled_value",value:{command:"arduinoUltrasonic",trigPin:"22",echoPin:"33"}}'
]) {
  assert.throws(()=>run(`buildProgJson([new arduino_setupNode([oled,raw(${conflict})])])`), /只能擇一/);
}
assert.throws(()=>run(`buildProgJson([new arduino_setupNode([oled]),new ProcedureDefNode('p',[],[raw({command:'blockly_func_set',idx:0,value:1})])])`),/只能擇一/);
assert.equal(value('inspectI2cProgram(buildProgJson([new arduino_setupNode([oled])])).oled'),true);
// Warnings appear and clear, disabled blocks do not claim the bus.
run(`var warningBlocks = ['oled_text','blockly_func_get'].map(type=>({type,enabled:true,
isEnabled(){return this.enabled},getFieldValue(){return null},setWarningText(t){this.warning=t}}));
workspace={getAllBlocks:()=>warningBlocks}; boardOledSupport=true; boardI2cRole='idle'; updateI2cWarnings();`);
assert.match(run('warningBlocks[0].warning'),/只能擇一/);
run('warningBlocks[1].enabled=false; updateI2cWarnings();');
assert.equal(run('warningBlocks[0].warning'),null);
run("boardI2cRole='upstream'; updateI2cWarnings();");
assert.match(run('warningBlocks[0].warning'),/停用對上/);
run('boardOledSupport=false; updateI2cWarnings();');
assert.match(run('warningBlocks[0].warning'),/更新韌體/);
// TCS34725 is a downstream peer, not a conflict with OLED.
for (const channel of ['r','g','b','c']) {
  assert.deepEqual(value(`getTranslator('tcs34725_read')(block('tcs34725_read',{CHANNEL:'${channel}'})).toJson()`),
    {command:'tcs34725_read',channel});
}
assert.throws(()=>run("new Tcs34725ReadNode('lux').toJson()"),/通道/);
run(`var tcsDisplay = new OledNode(block('oled_value',{ADDRESS:'60',ROW:'0'},
  {VALUE:block('tcs34725_read',{CHANNEL:'r'})}));`);
assert.equal(value('inspectI2cProgram(buildProgJson([new arduino_setupNode([tcsDisplay])])).tcs'),true);
assert.equal(value('inspectI2cProgram(buildProgJson([new arduino_setupNode([tcsDisplay])])).oled'),true);
for (const conflict of [
  '{command:"blockly_func_set",idx:0,value:1}',
  '{command:"if",condition:{command:"blockly_func_get",idx:0},then:[]}',
  '{command:"digitalWrite",pin:"21",value:1}'
]) {
  assert.throws(()=>run(`buildProgJson([new arduino_setupNode([
    raw({command:'message_print',content:{command:'tcs34725_read',channel:'r'}}),raw(${conflict})])])`), /只能擇一/);
}
run(`warningBlocks[0].type='tcs34725_read'; boardTcsSupport=false; updateI2cWarnings();`);
assert.match(run('warningBlocks[0].warning'),/更新韌體/);
run(`boardTcsSupport=true; boardOledSupport=false; boardI2cRole='idle'; updateI2cWarnings();`);
assert.equal(run('warningBlocks[0].warning'),null);
run('warningBlocks[1].enabled=true; updateI2cWarnings();');
assert.match(run('warningBlocks[0].warning'),/只能擇一/);
console.log('OLED/TCS translation, validation, nested role conflicts and live warnings: PASS');

for (const kind of ['lux','cct','color']) {
 assert.deepEqual(value(`getTranslator('tcs34725_${kind}')(block('tcs34725_${kind}',{})).toJson()`),{command:'tcs34725_'+kind});
 assert.equal(run(`inspectI2cProgram({command:'tcs34725_${kind}'}).tcsExtended`),true);
 assert.throws(()=>run(`buildProgJson([new arduino_setupNode([raw({command:'tcs34725_${kind}'}),raw({command:'digitalWrite',pin:21,value:1})])])`),/只能擇一/);
}
for (const [kind,field,key,valid,invalid] of [['gain','GAIN','gain',60,2],['time','CYCLES','cycles',256,20],['is_color','COLOR','color',4,5]]) {
 assert.deepEqual(value(`getTranslator('tcs34725_${kind}')(block('tcs34725_${kind}',{${field}:'${valid}'})).toJson()`),{command:'tcs34725_'+kind,[key]:valid});
 assert.throws(()=>run(`getTranslator('tcs34725_${kind}')(block('tcs34725_${kind}',{${field}:'${invalid}'})).toJson()`));
}

run(`warningBlocks=[{type:'tcs34725_lux',isEnabled:()=>true,getFieldValue:()=>null,setWarningText(t){this.warning=t}}]; workspace={getAllBlocks:()=>warningBlocks}; boardTcsSupport=true; boardTcsExtended=false; boardI2cRole='idle'; updateI2cWarnings();`);
assert.match(run('warningBlocks[0].warning'),/更新韌體/);
run('boardTcsExtended=true; updateI2cWarnings();');
assert.equal(run('warningBlocks[0].warning'),null);
