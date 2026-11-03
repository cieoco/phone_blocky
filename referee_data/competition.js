(function () {
  'use strict';
  const $=id=>document.getElementById(id), audio=new MatchAudio();
  // Fresh per document: duplicated tabs must not inherit the same audio/control owner.
  const operator=Array.from(crypto.getRandomValues(new Uint32Array(4)),x=>x.toString(16)).join('');
  $('key').value=sessionStorage.getItem('competitionKey')||'';
  let state=null,online=false,busy=false,teamSignature='',requestCounter=0,lastUpdate=0,lastHeartbeat=0;
  const labels={IDLE:'等待準備',PREPARING:'檢查參賽車輛',READY:'全員就緒',STARTING:'準備啟動',RUN_MANUAL:'手動模式',RUN_AUTO:'自動模式',GAP:'階段間暫停',PAUSED:'裁判暫停',FINISHED:'比賽結束',ABORTED:'比賽已中止',FAULT:'故障停止'};
  const errors={another_operator:'另一位裁判持有控制權',claim_control_first:'請先取得裁判控制權',state_conflict:'狀態已變更，請重新操作',invalid_state:'目前狀態不能執行此操作',invalid_config:'請選擇 1–4 隊並檢查時間設定',stop_not_confirmed:'仍有車輛尚未回覆輸出鎖定，不能清場',not_paired:'隊伍尚未完成配對',participant_unhealthy:'參賽板失聯或回報故障',prepare_timeout:'準備逾時：請檢查配對、頻道與已儲存程式',commit_timeout:'啟動確認逾時，本場已停止'};
  function note(text){$('notice').textContent=text;}
  function hasControl(){return online&&state&&state.owner===operator;}
  function render() {
    if(!state)return;
    $('phase').textContent=labels[state.state]||state.state;
    const timing=['RUN_MANUAL','RUN_AUTO','GAP','STARTING'].includes(state.state);
    const ms=Math.max(0,state.remaining_ms-(online&&timing?performance.now()-lastUpdate:0));
    const seconds=Math.ceil(ms/1000);$('timer').textContent=`${String(Math.floor(seconds/60)).padStart(2,'0')}:${String(seconds%60).padStart(2,'0')}`;
    $('next').textContent=state.state==='GAP'?`下一階段：${state.phase_mode==='AUTO'?'自動':'手動'}`:state.state==='PAUSED'?'等待裁判恢復；不會自動繼續':`${state.mode==='MIXED'?'混合比賽':'單一模式'} · 頻道 ${state.channel}`;
    const idle=state.state==='IDLE', own=hasControl();
    for(const id of ['mode','first','manual','auto','gap'])$(id).disabled=!idle||!own;
    for(const el of document.querySelectorAll('#teams input'))el.disabled=!idle||!own;
    const allowed={prepare:idle,cancel:['PREPARING','READY'].includes(state.state),start:state.state==='READY',pause:['RUN_MANUAL','RUN_AUTO','GAP'].includes(state.state),resume:state.state==='PAUSED',reset:['FINISHED','ABORTED','FAULT'].includes(state.state)};
    for(const [id,value] of Object.entries(allowed))$(id).disabled=!own||busy||!value;
    $('stop').disabled=!own||state.state==='IDLE';
    $('players').replaceChildren();
    for(const p of state.players||[]) {
      const row=document.createElement('tr');
      const status=p.locked_confirmed?'輸出已鎖定':p.online&&p.committed?'授權階段中':'停止／授權狀態未確認';
      for(const value of [p.name,p.online?'已連線':'未收到新回覆',p.fault?'故障':p.prepared?'就緒':'準備中',status]){const td=document.createElement('td');td.textContent=value;row.append(td);}
      $('players').append(row);
    }
    const sig=JSON.stringify(state.teams);
    if(sig!==teamSignature){teamSignature=sig;const selected=new Set(Array.from(document.querySelectorAll('#teams input:checked'),x=>x.value));$('teams').replaceChildren();for(const t of state.teams||[]){const label=document.createElement('label'),input=document.createElement('input');input.type='checkbox';input.value=t.mac;input.checked=selected.has(t.mac);label.append(input,document.createTextNode(`${t.name} · ${t.mac}`));$('teams').append(label);}if(!state.teams?.length)$('teams').textContent='尚未登記隊伍，請先使用 USB 配對工具。';}
  }
  async function refresh() {
    const r=await fetch('/api/competition/state',{cache:'no-store',signal:AbortSignal.timeout(2000)});
    if(!r.ok)throw new Error('主控板無回應');state=await r.json();lastUpdate=performance.now();online=true;
    $('online').textContent=state.owner===operator?'已連線 · 本頁持有裁判控制權':'已連線 · 觀看模式';$('online').className='';
    audio.observe(state,hasControl()&&!$('mute').checked);render();
    if(state.reason)note(errors[state.reason]||state.reason);
  }
  async function invoke(action,extra={}) {
    const id=`${operator.slice(0,16)}-${Date.now()}-${++requestCounter}`;
    const payload={action,operator,request_id:id,expected_version:state?.version,match_id:state?.match_id,...extra};
    const response=await fetch('/api/competition/action',{method:'POST',headers:{'Content-Type':'application/json','X-Competition-Key':$('key').value},body:JSON.stringify(payload),signal:AbortSignal.timeout(2500)});
    if(!response.ok)throw new Error(response.status===403?'裁判密碼不正確':`主控板拒絕請求 (${response.status})`);
    const deadline=performance.now()+4000;
    while(performance.now()<deadline){await new Promise(r=>setTimeout(r,120));await refresh();const result=state.results?.find(r=>r.id===id);if(result){if(result.result!=='ok')throw new Error(errors[result.result]||result.result);return;}}
    throw new Error('未收到操作結果；請查看主控板狀態，不要假設已成功。');
  }
  async function click(action,extra) {busy=true;render();try{await invoke(action,extra);note('');}catch(e){note(e.message);}finally{busy=false;render();}}
  $('claim').onclick=async()=>{sessionStorage.setItem('competitionKey',$('key').value);await click('claim');};
  $('prepare').onclick=()=>click('prepare',{mode:$('mode').value,first_mode:$('first').value,manual_ms:Number($('manual').value)*1000,auto_ms:Number($('auto').value)*1000,gap_ms:Number($('gap').value)*1000,teams:Array.from(document.querySelectorAll('#teams input:checked'),x=>x.value)});
  for(const action of ['pause','resume','stop','cancel','reset'])$(action).onclick=()=>click(action);
  $('start').onclick=()=>{if(!audio.ready&&!$('mute').checked){note('請先啟用音效，或明確勾選本場靜音。');return;}click('start');};
  $('audio').onclick=async()=>{try{await audio.enable();$('audioState').textContent='四種音效已載入，可離線播放。';}catch(e){$('audioState').textContent=e.message;}};
  $('volume').oninput=()=>{audio.volume=Number($('volume').value);};
  setInterval(render,100);
  (async function poll(){try{await refresh();if(hasControl()&&!busy&&Date.now()-lastHeartbeat>3000){lastHeartbeat=Date.now();await invoke('heartbeat');}}catch(e){online=false;$('online').textContent='連線中斷 · 畫面資料已過期；主控板計時仍持續';$('online').className='offline';render();}setTimeout(poll,200);})();
})();
