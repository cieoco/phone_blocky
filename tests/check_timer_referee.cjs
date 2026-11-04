// Offline browser integration: needs Playwright and Chrome, never contacts a board.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
(async()=>{
  const browser=await chromium.launch({channel:'chrome',headless:true});
  try {
    for(const arena of ['A','B']) {
      const page=await browser.newPage({viewport:{width:390,height:844}}),requests=[];
      const match=id=>({id,owner:'',state:'IDLE',mode:'MANUAL',first_mode:'MANUAL',phase_mode:'MANUAL',manual_ms:30000,auto_ms:30000,gap_ms:5000,remaining_ms:0,version:0,match_id:'0',event_id:0,sound:'',players:[],control_mode:'ESP_NOW',audio_route:'WAITING'});
      const snap={boot_id:'test',ready:false,timer_ready:true,channel:0,results:[],teams:[{mac:'AA:BB:CC:DD:EE:FF',name:'Test car',arena:''}],arenas:[match('A'),match('B')]};
      const current=snap.arenas.find(a=>a.id===arena);
      await page.addInitScript(()=>sessionStorage.setItem('competitionKey','test-only'));
      await page.route('**/*',async route=>{
        const url=new URL(route.request().url());
        if(url.pathname==='/api/competition/state')return route.fulfill({json:snap});
        if(url.pathname==='/api/competition/action') {
          const d=route.request().postDataJSON();requests.push(d);assert.equal(d.arena,arena);
          if(d.action==='claim')current.owner=d.operator;
          if(d.action==='prepare')Object.assign(current,{control_mode:d.control_mode,timer_teams:d.timer_teams,mode:d.mode,first_mode:d.first_mode,state:'READY',match_id:'123',version:1});
          if(d.action==='pair') {
            snap.teams.push({mac:d.mac,name:d.name,arena:''});
            snap.pair_result='ok';snap.pair_mac=d.mac;
          }
          snap.results.push({id:d.request_id,result:'ok'});
          return route.fulfill({status:202,json:{ok:true,queued:true}});
        }
        const name=path.basename(url.pathname),file=path.join(root,'referee_data',name);
        if(!fs.existsSync(file))return route.fulfill({status:404,body:''});
        return route.fulfill({body:fs.readFileSync(file),contentType:name.endsWith('.js')?'text/javascript':'text/html; charset=utf-8'});
      });
      await page.goto(`http://referee/competition.html?arena=${arena}`);
      await page.locator('#claim').click();
      await page.waitForFunction(()=>document.querySelector('#online').textContent.includes('本頁持有'));
      assert.equal(await page.locator('#prepare').isDisabled(),true,'wireless not ready');
      await page.locator('#controlMode').selectOption('TIMER_ONLY');
      assert.equal(await page.locator('#prepare').isEnabled(),true,'timer works without radio readiness');
      assert.equal(await page.locator('#wirelessRegistration').isVisible(),false);
      assert.equal(await page.locator('#teams').isVisible(),false);
      await page.locator('#timerTeams').fill('紅隊 vs 藍隊');
      await page.locator('#mode').selectOption('MIXED');
      await page.locator('#first').selectOption('AUTO');
      await page.locator('#prepare').click();
      await page.waitForFunction(()=>document.querySelector('#phase').textContent==='計時就緒');
      assert.equal(await page.locator('#timer').textContent(),'00:30');
      const d=requests.find(r=>r.action==='prepare');
      assert.equal(d.control_mode,'TIMER_ONLY');assert.deepEqual(d.teams,[]);assert.equal(d.timer_teams,'紅隊 vs 藍隊');
      assert.equal(d.first_mode,'AUTO');assert.equal(d.manual_ms,30000);
      assert.equal(await page.locator('#controlMode').isDisabled(),true);
      assert.equal(await page.locator('#stop').textContent(),'停止本場計時');
      assert.match(await page.locator('#controlNotice').textContent(),/不會啟動、切換或停止車輛/);
      assert.equal(snap.arenas.find(a=>a.id!==arena).state,'IDLE');
      await page.locator('#start').click();
      assert.equal(requests.some(r=>r.action==='start'),false,'audio acknowledgement still required');
      await page.locator('#mute').check();await page.locator('#start').click();
      await page.waitForFunction(()=>document.querySelector('#start').disabled===false);
      assert.equal(requests.some(r=>r.action==='start'),true);
      assert.equal(requests.some(r=>['scan','pair'].includes(r.action)),false);
      fs.mkdirSync(path.join(root,'.pio/timer-referee-check'),{recursive:true});
      await page.screenshot({path:path.join(root,`.pio/timer-referee-check/${arena}.png`),fullPage:true});
      // Selecting a discovered car registers and checks it without student UI steps.
      Object.assign(current,{state:'IDLE',control_mode:'ESP_NOW'});
      Object.assign(snap,{ready:true,registration_available:true,scanning:true,channel:8,discovered:[{mac:'12:34:56:78:90:AB',name:'New car',channel:8,fresh:true,available:true}]});
      await page.locator('#controlMode').selectOption('ESP_NOW');
      await page.locator('#discovered input').fill('藍隊');
      assert.match(await page.locator('#pairState').textContent(),/持續搜尋中/);
      Object.assign(snap.discovered[0],{fresh:false,available:false});
      await page.waitForFunction(()=>document.querySelector('#discovered button').disabled);
      assert.match(await page.locator('#discovered').textContent(),/失聯（超過 5 秒未回覆）/);
      assert.equal(await page.locator('#discovered input').inputValue(),'藍隊');
      Object.assign(snap.discovered[0],{fresh:true,available:true});
      await page.waitForFunction(()=>!document.querySelector('#discovered button').disabled);
      assert.equal(await page.locator('#discovered input').inputValue(),'藍隊','reconnect preserves typed team name');
      await page.locator('#discovered button').click();
      const selected=page.locator('#teams input[value="12:34:56:78:90:AB"]');
      await selected.waitFor();
      await page.waitForFunction(()=>document.querySelector('#teams input[value="12:34:56:78:90:AB"]')?.checked);
      assert.equal(requests.filter(r=>r.action==='pair').length,1);
      assert.equal(requests.find(r=>r.action==='pair').name,'藍隊');
      assert.equal(await selected.isChecked(),true);
      await page.close();
      console.log(`PASS ${arena}: timer without radio, locked mode, audio gate, isolated arena; wireless enrollment selects the car`);
    }
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
