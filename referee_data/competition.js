(function () {
  'use strict';
  const $=id=>document.getElementById(id), audio=new MatchAudio();
  // Fresh per document: duplicated tabs must not inherit the same audio/control owner.
  const operator=Array.from(crypto.getRandomValues(new Uint32Array(4)),x=>x.toString(16)).join('');
  $('key').value=sessionStorage.getItem('competitionKey')||'';
  let state=null,online=false,busy=false,teamSignature='',discoverySignature='',requestCounter=0,lastUpdate=0,lastHeartbeat=0;
  const labels={IDLE:'等待準備',PREPARING:'檢查參賽車輛',READY:'全員就緒',STARTING:'準備啟動',RUN_MANUAL:'手動模式',RUN_AUTO:'自動模式',GAP:'階段間暫停',PAUSED:'裁判暫停',FINISHED:'比賽結束',ABORTED:'比賽已中止',FAULT:'故障停止'};
  const errors={another_operator:'另一位裁判持有控制權',claim_control_first:'請先取得裁判控制權',state_conflict:'狀態已變更，請重新操作',invalid_state:'目前狀態不能執行此操作',invalid_config:'請選擇 1–4 隊並檢查時間設定',stop_not_confirmed:'仍有車輛尚未回覆輸出鎖定，不能清場',not_paired:'隊伍尚未完成配對',participant_unhealthy:'參賽板失聯或回報故障',prepare_timeout:'準備逾時：請檢查配對、頻道與已儲存程式',commit_timeout:'啟動確認逾時，本場已停止'};
  function note(text){$('notice').textContent=text;}
  Object.assign(errors,{referee_network_changed:'裁判 Wi-Fi 連線或頻道已變更，本場已停止；重新連線不會自動續賽',wifi_not_configured:'尚未設定裁判 STA 網路',wifi_disconnected:'裁判尚未連上路由器',unsupported_channel:'路由器須使用 2.4GHz 頻道 1–11'});
  Object.assign(errors,{discovery_stale:'車輛資訊已過期，請重新搜尋',student_consent_required:'請先在學生原網頁允許更換裁判',invalid_name:'隊名需為 1–48 個 UTF-8 bytes',key_exchange_failed:'無法建立配對金鑰，請重新搜尋',pairing_write_failed:'配對資料儲存失敗',peer_add_failed:'無法建立車輛通訊'});
  Object.assign(errors,{central_audio_online:'中央音效台已接管，本頁保持靜音',another_audio_station:'另一個中央音效台已連線'});
  let localPlaying=false;
  function hasControl(){return online&&state&&state.owner===operator;}
  function render() {
    if(!state)return;
    $('network').textContent=['STA','AP_STA'].includes(state.wifi_mode)?`裁判板 ${state.mac} · STA ${state.ssid}：${state.wifi_connected?state.ip:'未連線'} · AP ${state.ap_ssid||''}：${state.ap_ip||''} · 頻道 ${state.channel}`:'';
    $('phase').textContent=state.releasing?'清場中・等待各車解除管制':labels[state.state]||state.state;
    const timing=['RUN_MANUAL','RUN_AUTO','GAP','STARTING'].includes(state.state);
    const ms=Math.max(0,state.remaining_ms-(online&&timing?performance.now()-lastUpdate:0));
    const seconds=Math.ceil(ms/1000);$('timer').textContent=`${String(Math.floor(seconds/60)).padStart(2,'0')}:${String(seconds%60).padStart(2,'0')}`;
    $('next').textContent=state.state==='GAP'?`下一階段：${state.phase_mode==='AUTO'?'自動':'手動'}`:state.state==='PAUSED'?'等待裁判恢復；不會自動繼續':`${state.mode==='MIXED'?'混合比賽':'單一模式'} · 頻道 ${state.channel}`;
    const idle=state.state==='IDLE', own=hasControl();
    $('audio').disabled=!own||busy||state.audio_route==='CENTRAL';
    $('audioState').textContent=state.audio_route==='CENTRAL'?'中央音效台已連線・由中央播放，本頁自動靜音':state.audio_route==='LOCAL'?'本機音效模式・僅本頁持有操作權且啟用音效時播放':'中央音效未連線・目前不播放；可按「本機接回音效」，或選擇靜音比賽';
    $('scan').disabled=!idle||!own||busy||state.pairing||!state.ready;
    $('pairState').textContent=state.pairing?`正在配對 ${state.pair_mac}，等待車輛確認…`:state.pair_result==='ok'?`${state.pair_mac} 配對已確認`:state.pair_result==='pairing_unconfirmed'?`${state.pair_mac} 配對未確認；請查看學生頁的裁判 MAC，必要時重新開放接管。`:state.scanning?'正在搜尋同頻道車輛（15 秒）…':'';
    const discoverySig=JSON.stringify([state.discovered,idle,own,busy,state.pairing]);
    if(discoverySig!==discoverySignature) {
      discoverySignature=discoverySig;
      const names=new Map(Array.from(document.querySelectorAll('#discovered input'),x=>[x.dataset.mac,x.value]));
      $('discovered').replaceChildren();
      for(const car of state.discovered||[]) {
        const row=document.createElement('div'),label=document.createElement('span'),name=document.createElement('input'),button=document.createElement('button');
        label.textContent=`${car.mac} · ${!car.fresh?'資料過期':car.available?'可登記':car.controller_mac?'回報裁判 '+car.controller_mac:'等待學生開放'} `;
        name.dataset.mac=car.mac;name.value=names.get(car.mac)||car.name;name.placeholder='隊伍名稱';name.setAttribute('aria-label','隊伍名稱');
        button.textContent='登記／配對';button.disabled=!idle||!own||busy||state.pairing||!car.available;
        button.onclick=()=>click('pair',{mac:car.mac,name:name.value.trim()});row.append(label,name,button);$('discovered').append(row);
      }
    }
    for(const id of ['mode','first','manual','auto','gap'])$(id).disabled=!idle||!own;
    for(const el of document.querySelectorAll('#teams input'))el.disabled=!idle||!own;
    const allowed={prepare:idle&&state.ready,cancel:['PREPARING','READY'].includes(state.state),start:state.state==='READY',pause:['RUN_MANUAL','RUN_AUTO','GAP'].includes(state.state),resume:state.state==='PAUSED',reset:['FINISHED','ABORTED','FAULT'].includes(state.state)};
    for(const [id,value] of Object.entries(allowed))$(id).disabled=!own||busy||state.releasing||state.pairing||!value;
    $('stop').disabled=!own||state.state==='IDLE';
    $('players').replaceChildren();
    for(const p of state.players||[]) {
      const row=document.createElement('tr');
      const status=p.locked_confirmed?'輸出已鎖定':p.online&&p.committed?'授權階段中':'停止／授權狀態未確認';
      for(const value of [p.name,p.online?'已連線':'未收到新回覆',p.fault?'故障':p.prepared?'就緒':'準備中',status]){const td=document.createElement('td');td.textContent=value;row.append(td);}
      $('players').append(row);
    }
    const sig=JSON.stringify(state.teams);
    if(sig!==teamSignature){teamSignature=sig;const selected=new Set(Array.from(document.querySelectorAll('#teams input:checked'),x=>x.value));$('teams').replaceChildren();for(const t of state.teams||[]){const label=document.createElement('label'),input=document.createElement('input');input.type='checkbox';input.value=t.mac;input.checked=selected.has(t.mac);input.disabled=!idle||!own;label.append(input,document.createTextNode(`${t.name} · ${t.mac}`));$('teams').append(label);}if(!state.teams?.length)$('teams').textContent='尚未登記隊伍，請先搜尋車輛。';}
  }
  async function refresh() {
    const r=await fetch('/api/competition/state',{cache:'no-store',signal:AbortSignal.timeout(2000)});
    if(!r.ok)throw new Error('主控板無回應');state=await r.json();lastUpdate=performance.now();online=true;
    $('online').textContent=state.owner===operator?'已連線 · 本頁持有裁判控制權':'已連線 · 觀看模式';$('online').className='';
    const local=hasControl()&&state.audio_route==='LOCAL'&&!$('mute').checked;
    if(local!==localPlaying){audio.reset();localPlaying=local;}
    audio.observe(state,local);render();
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
  $('scan').onclick=()=>click('scan');
  $('prepare').onclick=()=>click('prepare',{mode:$('mode').value,first_mode:$('first').value,manual_ms:Number($('manual').value)*1000,auto_ms:Number($('auto').value)*1000,gap_ms:Number($('gap').value)*1000,teams:Array.from(document.querySelectorAll('#teams input:checked'),x=>x.value)});
  for(const action of ['pause','resume','stop','cancel','reset'])$(action).onclick=()=>click(action);
  $('start').onclick=()=>{if(state?.audio_route!=='CENTRAL'&&!(state?.audio_route==='LOCAL'&&audio.ready)&&!$('mute').checked){note('請連上中央音效台、啟用本機音效，或明確勾選靜音。');return;}click('start');};
  $('audio').onclick=async()=>{try{await audio.enable(false);await invoke('audio_local');audio.reset();note('本機音效已接回，僅播放接下來的事件。');}catch(e){audio.stop();note(e.message);}};
  $('volume').oninput=()=>{audio.volume=Number($('volume').value);};
  setInterval(render,100);
  (async function poll(){try{await refresh();if(hasControl()&&!busy&&Date.now()-lastHeartbeat>3000){lastHeartbeat=Date.now();await invoke('heartbeat');}}catch(e){online=false;audio.reset();localPlaying=false;$('online').textContent='連線中斷 · 畫面資料已過期；主控板計時仍持續';$('online').className='offline';render();}setTimeout(poll,200);})();
})();
