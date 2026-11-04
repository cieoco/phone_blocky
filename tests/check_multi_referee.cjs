// Three independent browsers, delayed HTTP replies, no hardware or real sounds.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..'),sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const browser=await chromium.launch({channel:'chrome',headless:true});
  try {
    const arena=id=>({id,owner:'',state:'IDLE',mode:'MANUAL',phase_mode:'MANUAL',first_mode:'MANUAL',control_mode:'ESP_NOW',manual_ms:30000,auto_ms:30000,gap_ms:5000,remaining_ms:30000,version:0,match_id:'0',event_id:0,sound:'',players:[]});
    const snap={boot_id:'board',ready:true,timer_ready:true,registration_available:true,teams:[],discovered:[],results:[],arenas:[arena('A'),arena('B')]};
    let audioOwner='',audioUntil=0;
    const stats={},requests=[];
    const pages=await Promise.all(['A','B','audio'].map(async label=>{
      const context=await browser.newContext();
      await context.addInitScript(()=>{
        sessionStorage.setItem('competitionKey','fixture');
        window.played=[];
        window.AudioContext=class {
          state='running';destination={};async resume(){} async decodeAudioData(){return {};}
          createBufferSource(){return {connect(){},disconnect(){},stop(){},start(){window.played.push(performance.now());}};}
          createGain(){return {gain:{},connect(){},disconnect(){}};}
        };
      });
      const page=await context.newPage();stats[label]={active:0,max:0,reads:0};
      await page.route('**/*',async route=>{
        const url=new URL(route.request().url());
        if(url.pathname==='/api/competition/state') {
          const st=stats[label];st.active++;st.max=Math.max(st.max,st.active);st.reads++;
          const central=Date.now()<audioUntil;
          const body=JSON.stringify({...snap,audio_owner:audioOwner,audio_route:central?'CENTRAL':'WAITING',audio_lease_ms:Math.max(0,audioUntil-Date.now()),arenas:snap.arenas.map(a=>({...a,audio_route:central?'CENTRAL':'WAITING'}))});
          // An old implementation issues another GET while this snapshot waits.
          await sleep(350);
          try {await route.fulfill({body,contentType:'application/json'});} finally {st.active--;}
          return;
        }
        if(url.pathname==='/api/competition/action') {
          const d=route.request().postDataJSON();requests.push({...d,desk:label,at:Date.now()});
          let result='ok';
          if(d.action==='audio_heartbeat'){audioOwner=d.audio_id;audioUntil=Date.now()+5000;}
          else if(d.action==='audio_release'){audioUntil=0;}
          else {
            assert.equal(d.arena,label,'commands must stay in the selected arena');
            const a=snap.arenas.find(a=>a.id===d.arena);
            if(d.action==='claim')a.owner=d.operator;
            else if(a.owner!==d.operator)result='claim_control_first';
            else if(d.action!=='heartbeat') {
              if(d.expected_version!==a.version)result='state_conflict';
              else {
                if(d.action==='prepare')Object.assign(a,{state:'READY',control_mode:d.control_mode,match_id:String(a.version+1),sound:''});
                if(d.action==='start'){a.state='RUN_MANUAL';a.event_id++;a.sound='start';}
                if(d.action==='pause'){a.state='PAUSED';a.event_id++;a.sound='abort';}
                if(d.action==='stop'){a.state='ABORTED';a.event_id++;a.sound='abort';}
                if(d.action==='reset'){a.state='IDLE';a.sound='';}
                a.version++;
              }
            }
          }
          // Keepalives intentionally have no result entries in the new firmware.
          if(!['heartbeat','audio_heartbeat','audio_release'].includes(d.action))snap.results.push({id:d.request_id,result});
          snap.results=snap.results.slice(-16);
          return route.fulfill({status:202,json:{ok:true,queued:true}});
        }
        if(url.pathname.endsWith('.wav'))return route.fulfill({body:Buffer.from('fixture'),contentType:'audio/wav'});
        const file=path.join(root,'referee_data',path.basename(url.pathname));
        if(!fs.existsSync(file))return route.fulfill({status:404,body:''});
        return route.fulfill({body:fs.readFileSync(file),contentType:file.endsWith('.js')?'text/javascript':'text/html; charset=utf-8'});
      });
      await page.goto(label==='audio'?'http://fixture/audio.html':`http://fixture/competition.html?arena=${label}`);
      return page;
    }));
    const [a,b,audio]=pages;
    await audio.locator('#enable').click();
    await audio.waitForFunction(()=>document.querySelector('#statusA').textContent.includes('中央播放中'));
    await Promise.all([a,b].map(p=>p.locator('#claim').click()));
    await Promise.all([a,b].map(p=>p.waitForFunction(()=>document.querySelector('#online').textContent.includes('本頁持有'))));
    for(let round=0;round<2;round++) {
      await Promise.all([a,b].map(async p=>{
        await p.locator('#controlMode').selectOption('TIMER_ONLY');
        await p.locator('#prepare').click();
        await p.waitForFunction(()=>document.querySelector('#phase').textContent==='計時就緒');
      }));
      await audio.waitForFunction(()=>['A','B'].every(id=>document.querySelector('#phase'+id).textContent==='計時就緒'));
      const before=await audio.evaluate(()=>window.played.length);
      await Promise.all([a,b].map(p=>p.locator('#start').click()));
      await Promise.all([a,b].map(p=>p.waitForFunction(()=>document.querySelector('#phase').textContent==='手動模式')));
      await audio.waitForFunction(n=>window.played.length===n+2,before);
      await a.locator('#pause').click();
      await a.waitForFunction(()=>document.querySelector('#phase').textContent==='裁判暫停');
      assert.equal(snap.arenas[1].state,'RUN_MANUAL');
      await Promise.all([a,b].map(async p=>{
        await p.locator('#stop').click();
        await p.waitForFunction(()=>document.querySelector('#phase').textContent==='比賽已中止');
        await p.locator('#reset').click();
        await p.waitForFunction(()=>document.querySelector('#phase').textContent==='等待準備');
      }));
    }
    for(const label of ['A','B','audio'])assert.equal(stats[label].max,1,`${label}: at most one state request in flight`);
    const beats=requests.filter(r=>r.action==='audio_heartbeat');
    for(let i=1;i<beats.length;i++)assert(beats[i].at-beats[i-1].at>=990,'audio heartbeat throttled');
    assert(stats.audio.reads>beats.length,'state updates continue between keepalives');
    for(const p of [a,b])assert.doesNotMatch(await p.locator('#notice').textContent(),/未收到|狀態已變更|拒絕/);
    console.log('PASS three desks: delayed replies, single-flight polling, independent A/B controls, overlapping audio, throttled heartbeats without result entries');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
