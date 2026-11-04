const assert=require('node:assert/strict');
const Audio=require('../referee_data/competition_audio.js');
const {CentralFeed}=require('../referee_data/central_audio.js');
const nodes=[];
const context={destination:{},createBufferSource(){const source={stopped:false,connect(){},disconnect(){},start(){nodes.push(this);},stop(){this.stopped=true;}};return source;},createGain(){return {gain:{},connect(){},disconnect(){}};}};
const a=new Audio(),b=new Audio();
for(const player of [a,b]){player.context=context;player.ready=true;player.buffers={start:{},end:{},abort:{}};}
const fa=new CentralFeed(a),fb=new CentralFeed(b);
const base={audio_route:'CENTRAL',audio_owner:'speaker',audio_lease_ms:4900,boot_id:'bootA',match_id:'matchA',event_id:0,sound:''};
const other={...base,boot_id:'bootB',match_id:'matchB',control_mode:'TIMER_ONLY'};
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

// Exercise the actual same-origin browser UI, including two competing desks.
const fs=require('node:fs'),vm=require('node:vm');
const elements=new Map(),timers=[],intervals=[],requests=[];
for(const id of ['enable','disable','volume','notice',...['A','B'].flatMap(x=>['status','phase','clock','event'].map(p=>p+x))])
  elements.set(id,{value:id==='volume'?'0.8':'',textContent:'',disabled:false});
class DeskAudio extends Audio {async enable(){this.context=context;this.buffers={start:{},end:{},abort:{}};this.ready=true;}}
context.state='running';
const document={getElementById:id=>elements.get(id)},events={};let event=0,holder='',offline=false;
const browser={document,addEventListener(type,fn){events[type]=fn;}};
const environment={window:browser,document,MatchAudio:DeskAudio,console,Date,
  RefereeSession:{...require('../referee_data/referee_session.js'),requireKey:()=>'fake-secret'},
  crypto:{getRandomValues:a=>a.fill(1)},performance:{now:()=>0},AbortSignal:{timeout:()=>({})},
  setTimeout:fn=>timers.push(fn),setInterval:fn=>intervals.push(fn),
  fetch:async(url,options)=>{
    assert(url.startsWith('/api/competition/'),'requests go only to this referee origin');
    if(offline)throw new Error('offline');
    if(options?.method==='POST'){
      const body=JSON.parse(options.body);requests.push(body);
      assert.equal(options.headers['X-Competition-Key'],'fake-secret');
      assert(!('operator' in body)&&!('arena' in body),'audio never claims either match');
      if(body.action==='audio_heartbeat'&&!holder)holder=body.audio_id;
      if(body.action==='audio_release'&&holder===body.audio_id)holder='';
      return {ok:true};
    }
    return {ok:true,json:async()=>({boot_id:'board',audio_route:'CENTRAL',audio_owner:holder,audio_lease_ms:4900,
      arenas:['A','B'].map(id=>({id,match_id:id,event_id:event,sound:event?'start':'',state:'RUN_AUTO',remaining_ms:30000,audio_route:'CENTRAL'}))})};
  }};
vm.runInNewContext(fs.readFileSync('referee_data/central_audio.js','utf8'),environment);
(async()=>{
  assert.equal(requests.length,0,'opening page does not acquire audio');
  await elements.get('enable').onclick();await timers.shift()();
  assert.equal(elements.get('statusA').textContent,'中央播放中');assert.equal(elements.get('statusB').textContent,'中央播放中');
  event=1;const before=nodes.length;await timers.shift()();assert.equal(nodes.length,before+2);
  await timers.shift()();assert.equal(nodes.length,before+2,'no duplicate sounds');
  offline=true;await timers.shift()();assert.equal(elements.get('clockA').textContent,'—');
  offline=false;event=3;await timers.shift()();assert.equal(nodes.length,before+2,'reconnect skips missed sounds');
  holder='another-desk';event=4;await timers.shift()();assert.equal(nodes.length,before+2,'another owner cannot play');
  assert.equal(elements.get('statusA').textContent,'等待有效播放權');
  context.state='suspended';intervals[0]();await Promise.resolve();assert.equal(elements.get('disable').disabled,true);
  assert(requests.some(x=>x.action==='audio_release'));assert.equal(holder,'another-desk');
  context.state='running';await elements.get('enable').onclick();events.pagehide();await Promise.resolve();
  assert.equal(elements.get('disable').disabled,true);
  console.log('Same-board browser audio: explicit enable, A/B overlap, stale baseline, ownership, suspension and pagehide PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
