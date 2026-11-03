const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const crypto = require('crypto');
const Audio = require('../referee_data/competition_audio.js');
const audio = new Audio(); audio.ready=true;
const played=[]; audio.play=n=>played.push(n);
const base={boot_id:'one',match_id:'match',event_id:0,sound:''};
audio.observe(base,true);
audio.observe({...base,event_id:1,sound:'start'},true);
audio.observe({...base,event_id:1,sound:'start'},true);
audio.observe({...base,event_id:2,sound:'end'},false);
audio.observe({...base,event_id:2,sound:'end'},true);
audio.observe({...base,event_id:3,sound:'resume'},true);
audio.observe({...base,boot_id:'two',event_id:3,sound:'start'},true);
assert.deepStrictEqual(played,['start','resume']);

const manifest=JSON.parse(fs.readFileSync('referee_data/audio/manifest.json','utf8'));
let localAudioCount=0,originalAudioCount=0;
for(const item of manifest.files){
  assert(/^[a-f0-9]{64}$/.test(item.sha256));
  if(!fs.existsSync('referee_data/audio/'+item.file))continue;
  ++localAudioCount;
  const data=fs.readFileSync('referee_data/audio/'+item.file);
  if(crypto.createHash('sha256').update(data).digest('hex')===item.sha256){
    assert.strictEqual(data.length,item.bytes);++originalAudioCount;
  }
  assert.strictEqual(data.toString('ascii',0,4),'RIFF');
  assert.strictEqual(data.toString('ascii',8,12),'WAVE');
}
for(const file of ['referee_data/competition.js','referee_data/competition_audio.js','data/competition_client.js'])new vm.Script(fs.readFileSync(file,'utf8'),{filename:file});

// A clean checkout deliberately has no recordings. Missing assets must not
// mark audio ready or silently allow an untested sound set.
(async()=>{
  const soundContext={module:{exports:{}},AudioContext:class{async resume(){}},fetch:async()=>({ok:false})};
  vm.runInNewContext(fs.readFileSync('referee_data/competition_audio.js','utf8'),soundContext);
  const missing=new soundContext.module.exports();
  await assert.rejects(missing.enable(),/repo 不附音檔/);
  assert.strictEqual(missing.ready,false);
  console.log(`Competition frontend: audio dedup/session/mute, missing-assets handling, ${localAudioCount} local WAVs (${originalAudioCount} match historical hashes) PASS`);
})().catch(error=>{console.error(error);process.exitCode=1;});
