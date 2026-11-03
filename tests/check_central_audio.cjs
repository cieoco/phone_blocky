const assert=require('node:assert/strict');
const Audio=require('../referee_data/competition_audio.js');
const {CentralFeed}=require('../tools/central_audio/www/desk.js');
const nodes=[];
const context={destination:{},createBufferSource(){const source={stopped:false,connect(){},disconnect(){},start(){nodes.push(this);},stop(){this.stopped=true;}};return source;},createGain(){return {gain:{},connect(){},disconnect(){}};}};
const a=new Audio(),b=new Audio();
for(const player of [a,b]){player.context=context;player.ready=true;player.buffers={start:{},end:{},abort:{}};}
const fa=new CentralFeed(a),fb=new CentralFeed(b);
const base={audio_route:'CENTRAL',audio_owner:'speaker',audio_lease_ms:4900,boot_id:'bootA',match_id:'matchA',event_id:0,sound:''};
const other={...base,boot_id:'bootB',match_id:'matchB'};
assert(fa.accept(base,'speaker',50,0));assert(fb.accept(other,'speaker',50,0));
fa.accept({...base,event_id:1,sound:'start'},'speaker',50,100);
fb.accept({...other,event_id:1,sound:'start'},'speaker',50,100);
assert.equal(nodes.length,2);assert.equal(nodes[0].stopped,false,'A must continue when B starts');
assert.equal(nodes[1].stopped,false);
fa.accept({...base,event_id:1,sound:'start'},'speaker',50,200);assert.equal(nodes.length,2,'duplicate snapshots never replay');
fa.accept({...base,event_id:0,sound:''},'speaker',50,250);
fa.accept({...base,event_id:1,sound:'start'},'speaker',50,300);assert.equal(nodes.length,2,'out-of-order snapshots cannot roll the cursor backwards');
fa.expire(1800);assert.equal(fa.online,true); // exact freshness boundary
fa.expire(1801);assert.equal(fa.online,false);assert.equal(nodes[0].stopped,true);assert.equal(nodes[1].stopped,false);
fa.accept({...base,event_id:3,sound:'end'},'speaker',50,1900);assert.equal(nodes.length,2,'reconnect must skip missed events');
fa.accept({...base,event_id:4,sound:'start'},'speaker',50,2000);assert.equal(nodes.length,3);
assert.equal(fa.accept({...base,event_id:5,sound:'end'},'another',50,2100),false);
assert.equal(nodes.length,3);assert.equal(nodes[2].stopped,true);
assert.equal(fb.accept({...other,event_id:2,sound:'end'},'speaker',1501,2200),false);
assert.equal(nodes.length,3,'late responses cannot play');
assert.equal(fb.accept({...other,audio_lease_ms:1999},'speaker',50,2300),false);
assert.equal(fb.accept({...other,audio_route:'WAITING'},'speaker',50,2300),false);
fb.accept({...other,boot_id:'newboot',event_id:99,sound:'abort'},'speaker',50,2400);
assert.equal(nodes.length,3,'board reboot also establishes a silent baseline');
console.log('Central audio: independent overlapping players, lease identity, duplicate/late snapshots, disconnect and reboot baselines PASS');

// Run the actual desk UI with fake browser/network/audio; no physical speaker.
const fs=require('node:fs'),vm=require('node:vm');
const elements=new Map(),timers=[],intervals=[],requests=[],stored={};
for(const id of ['enable','disable','volume','notice',...['A','B'].flatMap(x=>['host','key','status','phase','clock','event','test'].map(p=>p+x))])
  elements.set(id,{value:id==='volume'?'0.8':'',textContent:'',disabled:false,dataset:{}});
class DeskAudio extends Audio {async enable(){this.context=context;this.buffers={start:{},end:{},abort:{}};this.ready=true;}}
context.state='running';
const document={getElementById:id=>elements.get(id)};
const events={};let event=0;
const browser={document,addEventListener(type,fn){events[type]=fn;}};
const environment={window:browser,document,MatchAudio:DeskAudio,console,Date,
  localStorage:{getItem:k=>stored[k]||'',setItem:(k,v)=>{stored[k]=v;}},
  crypto:{getRandomValues:a=>a.fill(1)},performance:{now:()=>0},AbortSignal:{timeout:()=>({})},
  setTimeout:fn=>timers.push(fn),setInterval:fn=>intervals.push(fn),
  fetch:async(url,options)=>{
    if(url==='/api/bootstrap')return {ok:true,json:async()=>({token:'local-token'})};
    const body=JSON.parse(options.body);requests.push(body);
    return {ok:true,json:async()=>({...base,mac:body.host,boot_id:body.host,event_id:event,sound:event?'start':'',audio_owner:body.audio_id})};
  }};
vm.runInNewContext(fs.readFileSync('tools/central_audio/www/desk.js','utf8'),environment);
(async()=>{
  assert.equal(requests.length,0,'opening the page alone must not claim audio');
  elements.get('hostA').value='192.168.1.50';elements.get('hostB').value='192.168.1.51';
  elements.get('keyA').value='fake-secret-A';elements.get('keyB').value='fake-secret-B';
  await elements.get('enable').onclick();assert.equal(elements.get('disable').disabled,false);
  await timers.shift()();await timers.shift()();
  assert.equal(requests.filter(x=>x.operation==='heartbeat').length,2);
  assert.equal(elements.get('statusA').textContent,'中央播放中');assert.equal(elements.get('statusB').textContent,'中央播放中');
  event=1;const previousNodes=nodes.length;await timers.shift()();await timers.shift()();
  assert.equal(nodes.length,previousNodes+2,'two boards dispatch their sounds immediately');
  assert(!Object.values(stored).some(value=>value.includes('secret')),'passwords must not be persisted');
  context.state='suspended';intervals[0]();
  assert.equal(elements.get('disable').disabled,true);
  const heartbeats=requests.filter(x=>x.operation==='heartbeat').length;
  await timers.shift()();await timers.shift()();
  assert.equal(requests.filter(x=>x.operation==='heartbeat').length,heartbeats,'suspended audio cannot keep claiming the speaker');
  assert.equal(requests.filter(x=>x.operation==='release').length,2);
  console.log('Central desk UI: explicit enable, two live fields, overlapping events, no stored passwords and suspension releases PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
