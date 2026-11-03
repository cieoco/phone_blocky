(function(){
  'use strict';
  const $=id=>document.getElementById(id);
  function show(ready){$('loginPanel').hidden=ready;$('routes').hidden=!ready;}
  async function login(key){
    $('submit').disabled=true;
    try {
      const response=await fetch('/api/competition/login',{method:'POST',headers:{'Content-Type':'application/json','X-Competition-Key':key},body:'{}',signal:AbortSignal.timeout(3000)});
      if(!response.ok)throw new Error(response.status===403?'裁判密碼不正確':'裁判板未回應，請確認連線與韌體版本。');
      sessionStorage.setItem('competitionKey',key);$('key').value='';show(true);$('notice').textContent='';
    }catch(e){sessionStorage.removeItem('competitionKey');show(false);$('notice').textContent=e.message;}
    finally{$('submit').disabled=false;}
  }
  $('login').onsubmit=e=>{e.preventDefault();return login($('key').value);};
  $('logout').onclick=()=>{sessionStorage.removeItem('competitionKey');show(false);};
  const stored=sessionStorage.getItem('competitionKey');if(stored)login(stored);
})();
