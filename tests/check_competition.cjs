const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const crypto = require('crypto');
const Audio = require('../referee_data/competition_audio.js');
const audio = new Audio(); audio.ready=true;
const played=[]; audio.play=n=>played.push(n);
const base={boot_id:'one',match_id:'match',event_id:0,sound:''};
audio.observe(base,true);
audio.observe({...base,event_id:1,sound:'start'},true);
audio.observe({...base,event_id:1,sound:'start'},true);
audio.observe({...base,event_id:2,sound:'end'},false);
audio.observe({...base,event_id:2,sound:'end'},true);
audio.observe({...base,event_id:3,sound:'resume'},true);
audio.observe({...base,boot_id:'two',event_id:3,sound:'start'},true);
assert.deepStrictEqual(played,['start','resume']);

const manifest=JSON.parse(fs.readFileSync('referee_data/audio/manifest.json','utf8'));
let localAudioCount=0,originalAudioCount=0;
for(const item of manifest.files){
  assert(/^[a-f0-9]{64}$/.test(item.sha256));
  if(!fs.existsSync('referee_data/audio/'+item.file))continue;
  ++localAudioCount;
  const data=fs.readFileSync('referee_data/audio/'+item.file);
  if(crypto.createHash('sha256').update(data).digest('hex')===item.sha256){
    assert.strictEqual(data.length,item.bytes);++originalAudioCount;
  }
  assert.strictEqual(data.toString('ascii',0,4),'RIFF');
  assert.strictEqual(data.toString('ascii',8,12),'WAVE');
}
for(const file of ['referee_data/competition.js','referee_data/competition_audio.js','data/competition_drive.js'])new vm.Script(fs.readFileSync(file,'utf8'),{filename:file});

const elements=new Map();
function el(id){if(!elements.has(id))elements.set(id,{value:'',checked:false,textContent:'',dataset:{},setPointerCapture(){}});return elements.get(id);}
el('left').value='1';el('right').value='2';el('power').value='50';
const buttons=['forward','back','left','right'].map(name=>({...el('button-'+name),dataset:{move:name}}));
let tick,now=0,socket;const sent=[];
class WS {static OPEN=1;constructor(){socket=this;this.readyState=1;}send(s){sent.push(JSON.parse(s));}}
const ctx={document:{getElementById:el,querySelectorAll:()=>buttons,addEventListener(){},hidden:false},window:{addEventListener(){}},location:{host:'car'},WebSocket:WS,sessionStorage:{getItem(){return '';},setItem(){}},setInterval(fn){tick=fn;},setTimeout(){},performance:{now:()=>now},fetch:async()=>({ok:true}),console};
vm.runInNewContext(fs.readFileSync('data/competition_drive.js','utf8'),ctx);
function receive(data){socket.onmessage({data:JSON.stringify({type:'context',generation:1,mode:'MANUAL',...data})});}
receive({allowed:false});buttons[0].onpointerdown({preventDefault(){},pointerId:1});tick();assert(!sent.some(s=>s.type==='drive'));
receive({allowed:true,token:1});tick();assert.deepStrictEqual(sent.at(-1).motors,[0,0,0,0]);
buttons[0].onpointerdown({preventDefault(){},pointerId:1});tick();assert.deepStrictEqual(sent.at(-1).motors,[50,50,0,0]);
receive({allowed:false,mode:'AUTO',generation:2});receive({allowed:true,token:2,generation:3});tick();assert.deepStrictEqual(sent.at(-1).motors,[0,0,0,0]);
buttons[0].onpointerdown({preventDefault(){},pointerId:1});now=300;const before=sent.length;tick();assert.strictEqual(sent.length,before,'expired UI context cannot send motion');
// A clean checkout deliberately has no recordings. Missing assets must not
// mark audio ready or silently allow an untested sound set.
(async()=>{
  const soundContext={module:{exports:{}},AudioContext:class{async resume(){}},fetch:async()=>({ok:false})};
  vm.runInNewContext(fs.readFileSync('referee_data/competition_audio.js','utf8'),soundContext);
  const missing=new soundContext.module.exports();
  await assert.rejects(missing.enable(),/repo 不附音檔/);
  assert.strictEqual(missing.ready,false);
  console.log(`Competition frontend: audio dedup/session/mute, missing-assets handling, ${localAudioCount} local WAVs (${originalAudioCount} match historical hashes), locked controls, mode transitions and stale contexts PASS`);
})().catch(error=>{console.error(error);process.exitCode=1;});
