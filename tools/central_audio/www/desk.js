(function(root){
  'use strict';
  // One cursor/player per board: simultaneous fields never cancel each other.
  class CentralFeed {
    constructor(audio){this.audio=audio;this.online=false;this.lastAt=0;}
    disconnect(){this.audio.reset();this.online=false;}
    accept(state,id,age,now){
      if(age>1500||state.audio_route!=='CENTRAL'||state.audio_owner!==id||state.audio_lease_ms<2000||
         !state.boot_id||!state.match_id||!Number.isSafeInteger(state.event_id)){
        this.disconnect();return false;
      }
      if(!this.online)this.audio.reset(); // reconnect establishes a fresh baseline
      this.audio.observe(state,true);this.online=true;this.lastAt=now;return true;
    }
    expire(now){if(this.online&&now-this.lastAt>1500)this.disconnect();}
  }
  if(typeof module!=='undefined')module.exports={CentralFeed};
  if(!root.document)return;
  const $=id=>document.getElementById(id), master=new MatchAudio();
  const phases={IDLE:'等待準備',PREPARING:'準備本場',READY:'全員就緒',STARTING:'即將開始',RUN_MANUAL:'手動階段',RUN_AUTO:'自動階段',GAP:'換段暫停',PAUSED:'裁判暫停',FINISHED:'比賽結束',ABORTED:'比賽中止',FAULT:'故障停車'};
  const sounds={start:'開始',resume:'恢復',end:'結束',abort:'中止'};
  let enabled=false,starting=false,generation=0,token='',audioId='';
  const fields=['A','B'].map(label=>{
    const audio=new MatchAudio(),feed=new CentralFeed(audio);
    const play=audio.play.bind(audio);
    audio.play=name=>{if(!sounds[name])return;play(name);$('event'+label).textContent=`${label} 場 · ${sounds[name]} · ${new Date().toLocaleTimeString()}`;};
    try{$('host'+label).value=localStorage.getItem('centralHost'+label)||'';}catch(_){}
    $('test'+label).onclick=async()=>{try{await master.enable(false);shareAudio(audio);audio.play('start');}catch(e){$('notice').textContent=e.message;}};
    return {label,audio,feed,host:'',key:'',mac:''};
  });
  function shareAudio(audio){audio.context=master.context;audio.buffers=master.buffers;audio.ready=master.ready;audio.volume=Number($('volume').value);}
  function badge(f,text,online=false){const el=$('status'+f.label);el.textContent=text;el.dataset.online=String(online);}
  function renderControls(){
    $('enable').disabled=enabled||starting;$('disable').disabled=!enabled;
    for(const f of fields){$('host'+f.label).disabled=enabled||starting;$('key'+f.label).disabled=enabled||starting;$('test'+f.label).disabled=enabled||starting;}
  }
  async function post(f,id,operation='heartbeat',keepalive=false){
    const response=await fetch('/api/observe',{method:'POST',headers:{'Content-Type':'application/json','X-Local-Token':token},
      body:JSON.stringify({host:f.host,key:f.key,audio_id:id,operation}),signal:AbortSignal.timeout(3000),keepalive});
    const result=await response.json();if(!response.ok)throw new Error(result.error||'中央服務無回應');return result;
  }
  function stop(){
    const oldId=audioId;enabled=false;++generation;master.stop();
    for(const f of fields){f.feed.disconnect();badge(f,'已停止');if(f.host&&token)post(f,oldId,'release',true).catch(()=>{});}
    $('notice').textContent='中央播放已停止。裁判板失去心跳後約 5 秒顯示離線，可手動接回本機音效。';renderControls();
  }
  $('enable').onclick=async()=>{
    if(enabled||starting)return;starting=true;renderControls();
    try{
      for(const f of fields){f.host=$('host'+f.label).value.trim();f.key=$('key'+f.label).value;f.mac='';f.feed.disconnect();}
      if(!fields.some(f=>f.host))throw new Error('至少填入一塊裁判板的 IP。');
      if(fields.some(f=>f.host&&!f.key))throw new Error('請填入已選裁判板的操作密碼。');
      // AudioContext is created/resumed directly from this user gesture.
      await master.enable(false);
      if(master.context.state!=='running')throw new Error('瀏覽器尚未允許播放，請再次啟用音效。');
      const response=await fetch('/api/bootstrap');if(!response.ok)throw new Error('本機服務未啟動。');token=(await response.json()).token;
      audioId=Array.from(crypto.getRandomValues(new Uint32Array(2)),n=>n.toString(16).padStart(8,'0')).join('');
      if(audioId==='0000000000000000')audioId='0000000000000001';
      for(const f of fields){shareAudio(f.audio);try{localStorage.setItem('centralHost'+f.label,f.host);}catch(_){} }
      enabled=true;++generation;$('notice').textContent='中央音效已啟用，正在連接裁判板。請保持此頁開啟。';
    }catch(e){$('notice').textContent=e.message;}
    finally{starting=false;renderControls();}
  };
  $('disable').onclick=stop;
  $('volume').oninput=()=>{for(const f of fields)f.audio.volume=Number($('volume').value);master.volume=Number($('volume').value);};
  root.addEventListener('pagehide',()=>{if(enabled)stop();});
  setInterval(()=>{
    if(enabled&&master.context.state!=='running'){stop();$('notice').textContent='瀏覽器音訊已暫停。請按「啟用中央音效」恢復。';}
    for(const f of fields){const was=f.feed.online;f.feed.expire(performance.now());if(was&&!f.feed.online)badge(f,'連線逾時・暫停播放');}
  },250);
  for(const f of fields)(async function poll(){
    const run=generation,id=audioId;
    try{
      if(enabled&&f.host&&master.context.state==='running'){
        const sent=performance.now(),state=await post(f,id);
        if(!enabled||run!==generation||master.context.state!=='running')return;
        if(state.mac&&fields.some(other=>other!==f&&other.mac===state.mac))throw new Error('兩欄連到同一塊裁判板，請修改其中一欄。');
        f.mac=state.mac;
        if(!f.feed.accept(state,id,performance.now()-sent,performance.now()))throw new Error('播放權或資料已過期，等待重新確認。');
        badge(f,'中央播放中',true);$('phase'+f.label).textContent=phases[state.state]||'等待狀態';
        const sec=Math.ceil(Math.max(0,Number(state.remaining_ms)||0)/1000);
        $('clock'+f.label).textContent=`${String(Math.floor(sec/60)).padStart(2,'0')}:${String(sec%60).padStart(2,'0')}`;
      }else if(enabled&&!f.host)badge(f,'未使用');
    }catch(e){if(run===generation){f.feed.disconnect();badge(f,e.message);$('clock'+f.label).textContent='—';}}
    finally{setTimeout(poll,250);}
  })();
  renderControls();
})(typeof window!=='undefined'?window:globalThis);
