const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
let now = 0, tick, transitions = 0;
const documentListeners = {};
const elements = new Map();
function element(id) {
  const e = {id, disabled:false, hidden:false, textContent:'', style:{}, setAttribute(){}, appendChild(child){elements.set(child.id, child);}};
  if(id)elements.set(id,e);return e;
}
for(const id of ['runBtn','saveBtn','autorunToggle'])element(id);
class Socket {
  static OPEN = 1;
  constructor(){this.readyState=1;this.listeners={};this.sent=[];}
  addEventListener(type,fn){(this.listeners[type] ||= []).push(fn);}
  send(raw){this.sent.push(JSON.parse(raw));}
  receive(d){for(const fn of this.listeners.message || [])fn({data:JSON.stringify(d),stopImmediatePropagation(){}});}
}
const doc={hidden:false,body:{prepend(e){elements.set(e.id,e);}},getElementById:id=>elements.get(id),
  createElement:()=>element(),querySelectorAll:()=>[],addEventListener(type,fn){documentListeners[type]=fn;}};
const win={WebSocket:Socket,dispatchEvent(){transitions++;}};
const context={window:win,document:doc,location:{host:'car',href:'http://car/blockly.html',pathname:'/blockly.html'},URL,
  CustomEvent:class {constructor(type,options){this.type=type;this.detail=options.detail;}},
  performance:{now:()=>now},setInterval(fn){tick=fn;},fetch:async()=>({status:404}),console};
vm.runInNewContext(fs.readFileSync('data/competition_client.js','utf8'),context);
const ws=new win.WebSocket('ws://car/ws');
const base={type:'competition_state',boot_id:'boot',match_id:'match',epoch:1,generation:1,paired:true,managed:false,state:'TEACHING',state_fresh:true,remaining_ms:30000};
const send=d=>ws.send(JSON.stringify(d));
ws.receive(base);send({mode:'PROG',setup:[{}],loop:[]});assert.equal(ws.sent.length,1);
ws.receive({...base,managed:true,state:'READY',allowed:false});
send({mode:'joy',y1:80});send({mode:'PROG',setup:[{}],loop:[]});assert.equal(ws.sent.length,1);
assert.equal(elements.get('runBtn').disabled,true);assert.equal(elements.get('saveBtn').disabled,true);
send({cmd:'read',motor:3});assert.equal(ws.sent.at(-1).cmd,'read');
ws.receive({...base,managed:true,state:'RUN_MANUAL',allowed:true,token:11});
send({mode:'joy',y1:80});assert.equal(ws.sent.at(-1)._competition.token,11);
const count=ws.sent.length;
send({mode:'hardwareTest',cmd:'read'});send({command:'servo_control',cmd:'read'});assert.equal(ws.sent.length,count);
now=400;tick();send({cmd:'pwm',motor:1,duty:50});assert.equal(ws.sent.length,count);
ws.receive({...base,generation:2,managed:true,state:'RUN_AUTO',allowed:false});send({cmd:'pwm',motor:1,duty:50});assert.equal(ws.sent.length,count);
doc.hidden=true;documentListeners.visibilitychange();assert.equal(ws.sent.length,count,'AUTO must not depend on browser visibility');doc.hidden=false;
send({mode:'PROG',setup:[],loop:[]});assert.equal(ws.sent.at(-1).cmd,'competition_stop');
ws.receive({...base,generation:3,managed:true,state:'PAUSED',allowed:false});
assert.match(elements.get('competitionStatusText').textContent,/裁判暫停/);
ws.receive({...base,generation:4,managed:true,state:'GAP',allowed:false});
assert.match(elements.get('competitionStatusText').textContent,/換段暫停/);
ws.receive({...base,generation:5,managed:true,state:'FINISHED',allowed:false});assert.equal(elements.get('runBtn').disabled,true);
ws.receive({...base,generation:5,epoch:2});assert.equal(elements.get('runBtn').disabled,false);
send({mode:'PROG',setup:[{}],loop:[]});assert.equal(ws.sent.at(-1).mode,'PROG');
assert(transitions>=7);
// The actual joy page listens for phase changes and reuses its tested neutral latch.
const joy=fs.readFileSync('data/joy.html','utf8');
const listener=joy.slice(joy.indexOf("window.addEventListener('competitionchange'"),joy.indexOf('\n\t});',joy.indexOf("window.addEventListener('competitionchange'"))+6);
let reset;const ui={window:{addEventListener(_type,fn){reset=fn;}},estopActive:false,document:{getElementById:id=>elements.get(id)}};
for(const id of ['ActionSlider_ON_M1','ActionSlider_ON_M2'])element(id).value=80;
vm.runInNewContext(listener,ui);reset();assert.equal(ui.estopActive,true);
assert.equal(elements.get('ActionSlider_ON_M1').value,0);
ws.receive({...base,generation:6,managed:true,state:'RUN_MANUAL',allowed:true,token:12});
doc.hidden=true;documentListeners.visibilitychange();assert.equal(ws.sent.at(-1).cmd,'competition_stop');doc.hidden=false;
ws.receive({...base,generation:6,managed:true,state:'RUN_MANUAL',allowed:true,token:12,state_fresh:false});
const staleViewCount=ws.sent.length;send({mode:'joy',y1:80});assert.equal(ws.sent.length,staleViewCount);
console.log('Original student UI: teaching/manual/auto/pause/gap/end/release, stale token, command precedence and joy reset PASS');
(async () => {
  const requests=[];
  win.prompt=()=> '6';win.confirm=()=>true;win.alert=()=>{};
  context.fetch=async(url,options)=>{requests.push({url,options});return {ok:true,status:202};};
  ws.receive({...base,managed:true,committed:true,channel:1,state:'RUN_AUTO'});
  assert.equal(elements.get('competitionHandover').disabled,true);
  await win.CompetitionStudent.handover();assert.equal(requests.length,0);
  ws.receive({...base,managed:true,committed:false,channel:1,state:'FAULT'});
  win.confirm=()=>false;await win.CompetitionStudent.handover();assert.equal(requests.length,0);
  win.confirm=()=>true;win.prompt=()=> '12';await win.CompetitionStudent.handover();assert.equal(requests.length,0);
  win.prompt=()=> '6';await win.CompetitionStudent.handover();
  assert.equal(requests.length,1);assert.equal(requests[0].url,'/api/competition/handover');
  assert.equal(requests[0].options.method,'POST');assert.deepEqual(JSON.parse(requests[0].options.body),{channel:6});
  ws.receive({...base,managed:true,paired:false,committed:false,pairing_open:true,pairing_remaining_ms:59000,channel:6,state:'FAULT'});
  assert.equal(elements.get('runBtn').disabled,true,'opening consent must not unlock the original run control');
  assert.match(elements.get('competitionStatusText').textContent,/開放接管 59 秒/);
  now+=400;await win.CompetitionStudent.handover();assert.equal(requests.length,1,'stale state cannot start recovery');
  ws.receive({...base,channel:6,controller_mac:'AA:BB:CC:DD:EE:FF'});
  assert.equal(elements.get('runBtn').disabled,false);
  assert.match(elements.get('competitionStatusText').textContent,/AA:BB:CC:DD:EE:FF/);
  console.log('Student handover: active lease, consent, channel validation, stale state and locked waiting window PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
