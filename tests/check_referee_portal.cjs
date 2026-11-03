const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const {arenaState}=require('../referee_data/referee_session.js');
const snap={boot_id:'board',results:[],arenas:[{id:'A',owner:'alice',version:3,match_id:'a'},{id:'B',owner:'bob',version:9,match_id:'b'}]};
assert.equal(arenaState(snap,'A').owner,'alice');assert.equal(arenaState(snap,'B').version,9);
assert.throws(()=>arenaState(snap,'C'));assert.throws(()=>arenaState({},'A'));
const elements=Object.fromEntries(['loginPanel','routes','login','key','submit','logout','notice'].map(id=>[id,{hidden:id==='routes',value:'',textContent:''}]));
const stored={},requests=[];let accepted=false;
const env={document:{getElementById:id=>elements[id]},sessionStorage:{getItem:k=>stored[k],setItem:(k,v)=>stored[k]=v,removeItem:k=>delete stored[k]},AbortSignal:{timeout:()=>({})},
  fetch:async(url,opt)=>{requests.push({url,opt});return {ok:accepted,status:accepted?200:403};}};
vm.runInNewContext(fs.readFileSync('referee_data/portal.js','utf8'),env);
(async()=>{
  elements.key.value='incorrect';await elements.login.onsubmit({preventDefault(){}});
  assert.equal(elements.routes.hidden,true);assert.equal(stored.competitionKey,undefined);
  accepted=true;elements.key.value='fake-secret';await elements.login.onsubmit({preventDefault(){}});
  assert.equal(elements.routes.hidden,false);assert.equal(stored.competitionKey,'fake-secret');assert.equal(elements.key.value,'');
  assert.equal(requests[1].url,'/api/competition/login');assert.equal(requests[1].opt.headers['X-Competition-Key'],'fake-secret');
  elements.logout.onclick();assert.equal(elements.routes.hidden,true);assert.equal(stored.competitionKey,undefined);
  let redirected='';const browser={location:{replace:url=>redirected=url}};
  vm.runInNewContext(fs.readFileSync('referee_data/referee_session.js','utf8'),{window:browser,sessionStorage:env.sessionStorage});
  assert.equal(browser.RefereeSession.requireKey(),null);assert.equal(redirected,'/');
  const html=fs.readFileSync('referee_data/index.html','utf8');
  for(const href of ['competition.html?arena=A','competition.html?arena=B','audio.html'])assert(html.includes(`href="${href}"`));
  console.log('Referee portal: authenticated branching, failed login, session-only key, logout and arena selection PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
