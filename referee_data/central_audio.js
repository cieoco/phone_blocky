(function(root){
  'use strict';
  class CentralFeed {
    constructor(audio){this.audio=audio;this.online=false;this.lastAt=0;}
    disconnect(){this.audio.reset();this.online=false;}
    accept(state,id,age,now){
      if(age>1500||state.audio_route!=='CENTRAL'||state.audio_owner!==id||state.audio_lease_ms<2000||
         !state.boot_id||!state.match_id||!Number.isSafeInteger(state.event_id)){
        this.disconnect();return false;
      }
      if(!this.online)this.audio.reset();
      this.audio.observe(state,true);this.online=true;this.lastAt=now;return true;
    }
    expire(now){if(this.online&&now-this.lastAt>1500)this.disconnect();}
  }
  if(typeof module!=='undefined')module.exports={CentralFeed};
  if(!root.document)return;
  const key=RefereeSession.requireKey();if(!key)return;
  const $=id=>document.getElementById(id),master=new MatchAudio();
  const phases={IDLE:'等待準備',PREPARING:'準備本場',READY:'全員就緒',STARTING:'即將開始',RUN_MANUAL:'手動階段',RUN_AUTO:'自動階段',GAP:'換段暫停',PAUSED:'裁判暫停',FINISHED:'比賽結束',ABORTED:'比賽中止',FAULT:'故障停車'};
  const sounds={start:'開始',resume:'恢復',end:'結束',abort:'中止'};
  let enabled=false,starting=false,generation=0,audioId='',counter=0;
  const fields=['A','B'].map(label=>{
    const audio=new MatchAudio(),feed=new CentralFeed(audio),play=audio.play.bind(audio);
    audio.play=name=>{if(!sounds[name])return;play(name);$('event'+label).textContent=`${label} 場 · ${sounds[name]} · ${new Date().toLocaleTimeString()}`;};
    return {label,audio,feed};
  });
  function controls(){$('enable').disabled=enabled||starting;$('disable').disabled=!enabled;}
  function badge(f,text){$('status'+f.label).textContent=text;}
  async function post(action,id,keepalive=false){
    const response=await fetch('/api/competition/action',{method:'POST',headers:{'Content-Type':'application/json','X-Competition-Key':key},
      body:JSON.stringify({action,audio_id:id,request_id:`${id}-${Date.now()}-${++counter}`}),signal:AbortSignal.timeout(2000),keepalive});
    if(!response.ok)throw new Error(response.status===403?'登入已失效，請回比賽中心登入。':'裁判板未接受音效連線。');
  }
  function stop(){
    const oldId=audioId;enabled=false;++generation;master.stop();
    for(const f of fields){f.feed.disconnect();badge(f,'已停止');$('clock'+f.label).textContent='—';}
    if(oldId)post('audio_release',oldId,true).catch(()=>{});
    $('notice').textContent='中央播放已停止；裁判可在自己的頁面手動接回音效。';controls();
  }
  $('enable').onclick=async()=>{
    if(enabled||starting)return;starting=true;controls();
    try {
      await master.enable(false);
      if(master.context.state!=='running')throw new Error('瀏覽器尚未允許播放，請再次啟用音效。');
      audioId=Array.from(crypto.getRandomValues(new Uint32Array(2)),n=>n.toString(16).padStart(8,'0')).join('');
      if(audioId==='0000000000000000')audioId='0000000000000001';
      for(const f of fields){f.feed.disconnect();f.audio.context=master.context;f.audio.buffers=master.buffers;f.audio.ready=true;f.audio.volume=Number($('volume').value);}
      enabled=true;++generation;$('notice').textContent='正在取得中央播放權，請保持本頁開啟。';
    }catch(e){$('notice').textContent=e.message;}
    finally{starting=false;controls();}
  };
  $('disable').onclick=stop;
  $('volume').oninput=()=>{for(const f of fields)f.audio.volume=Number($('volume').value);};
  root.addEventListener('pagehide',()=>{if(enabled)stop();});
  setInterval(()=>{
    if(enabled&&master.context.state!=='running'){stop();$('notice').textContent='瀏覽器音訊已暫停，請再次啟用中央音效。';}
    for(const f of fields){const was=f.feed.online;f.feed.expire(performance.now());if(was&&!f.feed.online){badge(f,'連線逾時・暫停播放');$('clock'+f.label).textContent='—';}}
  },250);
  (async function poll(){
    const run=generation,id=audioId;
    try {
      if(enabled&&master.context.state==='running'){
        await post('audio_heartbeat',id);
        const sent=performance.now();
        const response=await fetch('/api/competition/state',{cache:'no-store',signal:AbortSignal.timeout(1500)});
        if(!response.ok)throw new Error('裁判板無回應');
        const snapshot=await response.json();
        if(!enabled||run!==generation||master.context.state!=='running')return;
        const central=snapshot.audio_owner===id&&snapshot.audio_route==='CENTRAL';
        $('notice').textContent=central?'中央音效已連線，兩場裁判頁自動靜音。':'等待播放權；若另一台音效台已啟用，請先在該頁停止播放。';
        for(const f of fields){
          const state=RefereeSession.arenaState(snapshot,f.label);
          if(!f.feed.accept(state,id,performance.now()-sent,performance.now())){badge(f,'等待有效播放權');$('clock'+f.label).textContent='—';continue;}
          badge(f,'中央播放中');$('phase'+f.label).textContent=phases[state.state]||state.state;
          const sec=Math.ceil(Math.max(0,Number(state.remaining_ms)||0)/1000);
          $('clock'+f.label).textContent=`${String(Math.floor(sec/60)).padStart(2,'0')}:${String(sec%60).padStart(2,'0')}`;
        }
      }
    }catch(e){if(run===generation){for(const f of fields){f.feed.disconnect();badge(f,'連線中斷');$('clock'+f.label).textContent='—';}$('notice').textContent=e.message;}}
    finally{setTimeout(poll,250);}
  })();
  controls();
})(typeof window!=='undefined'?window:globalThis);
