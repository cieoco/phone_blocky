(()=>{
  'use strict';const $=id=>document.getElementById(id);
  let socket,allowed=false,token=0,sequence=0,movement='',generation=0,lastContext=0,logged=false;
  $('key').value=sessionStorage.getItem('sumoCarKey')||'';
  const send=o=>{if(socket?.readyState===WebSocket.OPEN)socket.send(JSON.stringify(o));};
  function neutral(){movement='';if(allowed&&token)send({type:'drive',token,sequence:++sequence,motors:[0,0,0,0]});}
  function claim(){send({type:'claim',key:$('key').value});}
  function connect(){socket=new WebSocket(`ws://${location.host}/drive`);socket.onopen=()=>{if(logged)claim();};socket.onclose=()=>{allowed=false;token=0;movement='';$('status').textContent='連線中斷，車輛等待授權／停止';setTimeout(connect,1500);};socket.onmessage=e=>{let d;try{d=JSON.parse(e.data);}catch(_){return;}if(d.type==='error'){$('notice').textContent=d.error;return;}if(d.type!=='context')return;if(generation!==d.generation||!d.allowed){movement='';sequence=0;}generation=d.generation;allowed=d.allowed;token=d.token||0;lastContext=performance.now();$('status').textContent=d.fault?'故障鎖定，請通知裁判':allowed?'手動授權中':d.mode==='AUTO'?'自動階段／等待授權':'等待裁判開放手動控制';};}
  $('login').onclick=async()=>{try{const r=await fetch('/api/competition/login',{method:'POST',headers:{'Content-Type':'application/json','X-Competition-Key':$('key').value},body:'{}'});if(!r.ok)throw new Error('本車密碼不正確');logged=true;sessionStorage.setItem('sumoCarKey',$('key').value);claim();$('notice').textContent='已登入。儲存程式及操控均限本車。';}catch(e){$('notice').textContent=e.message;}};
  for(const b of document.querySelectorAll('[data-move]')){b.onpointerdown=e=>{e.preventDefault();if(!allowed)return;b.setPointerCapture(e.pointerId);movement=b.dataset.move;};b.onpointerup=neutral;b.onpointercancel=neutral;b.onlostpointercapture=neutral;}
  $('neutral').onclick=neutral;$('emergency').onclick=()=>{neutral();send({type:'stop'});};
  $('power').oninput=()=>{$('powerValue').textContent=$('power').value+'%';};
  for(const id of ['left','right','invertLeft','invertRight'])$(id).onchange=neutral;
  document.addEventListener('visibilitychange',()=>{if(document.hidden)neutral();});window.addEventListener('blur',neutral);
  setInterval(()=>{
    if(logged)send({type:'heartbeat'});
    if(!allowed||!token||performance.now()-lastContext>250||document.hidden){movement='';return;}
    const values=[0,0,0,0],power=Number($('power').value),left=Number($('left').value)-1,right=Number($('right').value)-1;
    if(left===right){movement='';$('notice').textContent='左右輪必須選擇不同馬達';}
    const factors={forward:[1,1],back:[-1,-1],left:[-1,1],right:[1,-1]};const f=factors[movement]||[0,0];
    values[left]=f[0]*power*($('invertLeft').checked?-1:1);values[right]=f[1]*power*($('invertRight').checked?-1:1);
    send({type:'drive',token,sequence:++sequence,motors:values});
  },80);connect();
})();
