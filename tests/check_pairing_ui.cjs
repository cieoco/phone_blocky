const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const elements=new Map(),intervals=[],requests=[];
function element(id='') {
  const e={id,children:[],dataset:{},value:'',textContent:'',disabled:false,className:'',
    append(...xs){this.children.push(...xs);},replaceChildren(...xs){this.children=xs;this.textContent='';},setAttribute(){}};
  if(id)elements.set(id,e);return e;
}
for(const id of ['key','notice','network','phase','timer','next','mode','first','manual','auto','gap','scan','pairState','discovered','prepare','cancel','start','pause','resume','reset','stop','players','teams','online','claim','audio','volume','mute','audioState'])element(id);
const doc={getElementById:id=>elements.get(id),createElement:tag=>Object.assign(element(),{tag}),createTextNode:text=>({textContent:text}),
  querySelectorAll(selector){const root=elements.get(selector.startsWith('#discovered')?'discovered':'teams');
    return root.children.flatMap(row=>row.children||[]).filter(x=>x.tag==='input'&&(!selector.includes(':checked')||x.checked));}};
const operator='12345678123456781234567812345678';
let state={state:'IDLE',ready:true,owner:operator,version:1,match_id:'one',players:[],teams:[],results:[],remaining_ms:30000,
  channel:6,mode:'MANUAL',discovered:[{mac:'AA:BB:CC:DD:EE:FF',name:'<script>unsafe</script>',available:true,fresh:true}]};
const ctx={document:doc,crypto:{getRandomValues:a=>a.fill(0x12345678)},sessionStorage:{getItem:()=>'',setItem(){}},
  MatchAudio:class{observe(){}},performance:{now:()=>100},AbortSignal:{timeout:()=>({})},Date:{now:()=>100},
  setInterval:fn=>intervals.push(fn),setTimeout:(fn,ms)=>{if(ms===120)fn();},console,
  fetch:async(url,options)=>{
    if(options?.method==='POST'){const body=JSON.parse(options.body);requests.push(body);state.results.push({id:body.request_id,result:'ok'});}
    return {ok:true,json:async()=>state};
  }};
vm.runInNewContext(fs.readFileSync('referee_data/competition.js','utf8'),ctx);
async function settle(){for(let i=0;i<12;++i)await Promise.resolve();}
(async()=>{
  await settle();assert.equal(elements.get('scan').disabled,false);
  const row=elements.get('discovered').children[0];assert.equal(row.children[1].value,'<script>unsafe</script>');
  row.children[1].value='A 隊';row.children[2].onclick();await settle();
  const pair=requests.find(x=>x.action==='pair');assert.equal(pair.name,'A 隊');assert.equal(pair.mac,'AA:BB:CC:DD:EE:FF');
  await elements.get('scan').onclick();await settle();assert(requests.some(x=>x.action==='scan'));
  state.pairing=true;intervals[0]();assert.equal(elements.get('prepare').disabled,true);assert.equal(elements.get('scan').disabled,true);
  assert.equal(elements.get('discovered').children[0].children[2].disabled,true);
  state.pairing=false;state.owner='another';intervals[0]();assert.equal(elements.get('scan').disabled,true);
  state.owner=operator;state.discovered[0].available=false;state.discovered[0].controller_mac='11:22:33:44:55:66';intervals[0]();
  assert.equal(elements.get('discovered').children[0].children[2].disabled,true);
  assert.match(elements.get('discovered').children[0].children[0].textContent,/11:22:33:44:55:66/);
  state.pair_result='pairing_unconfirmed';intervals[0]();assert.match(elements.get('pairState').textContent,/未確認/);
  console.log('Referee discovery UI: scan/pair requests, literal names, ownership, consent and unconfirmed result PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
