(function(root){
  'use strict';
  function arenaState(snapshot,id){
    const arena=snapshot.arenas?.find(x=>x.id===id);
    if(!arena)throw new Error('裁判板未提供此場地，請更新韌體與網頁。');
    return {...snapshot,...arena};
  }
  function requireKey(){
    const key=sessionStorage.getItem('competitionKey');
    if(!key){root.location.replace('/');return null;}
    return key;
  }
  root.RefereeSession={arenaState,requireKey};
  if(typeof module!=='undefined')module.exports=root.RefereeSession;
})(typeof window!=='undefined'?window:globalThis);
