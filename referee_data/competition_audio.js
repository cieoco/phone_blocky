/* Operator-supplied recordings; no audio assets are distributed in this repo. */
(function (root) {
  class MatchAudio {
    constructor() { this.context=null; this.buffers={}; this.active=[]; this.ready=false; this.last=null; this.volume=0.8; }
    async enable(preview=true) {
      const Context=root.AudioContext||root.webkitAudioContext;
      if(!Context) throw new Error('此瀏覽器不支援 Web Audio');
      if(!this.context) this.context=new Context();
      await this.context.resume();
      const names=['start','resume','end','abort'];
      await Promise.all(names.map(async name=>{
        if(this.buffers[name])return;
        const r=await fetch(`audio/${name}.wav`,{cache:'force-cache'});
        if(!r.ok)throw new Error(`缺少 ${name}.wav：repo 不附音檔。請依中央音效台或裁判頁的操作說明，提供可使用的音檔。`);
        this.buffers[name]=await this.context.decodeAudioData(await r.arrayBuffer());
      }));
      this.ready=true;if(preview)this.play('start');
    }
    stop() {
      for(const old of this.active){try{old.stop();}catch(_){}}
      this.active=[];
    }
    reset() { this.stop();this.last=null; }
    play(name) {
      if(!this.ready||!this.buffers[name])return;
      this.stop();
      const source=this.context.createBufferSource(), gain=this.context.createGain();
      source.buffer=this.buffers[name];gain.gain.value=this.volume;source.connect(gain);gain.connect(this.context.destination);
      source.onended=()=>{this.active=this.active.filter(s=>s!==source);source.disconnect();gain.disconnect();};
      this.active.push(source);source.start();
    }
    observe(s,enabled) {
      const key=`${s.boot_id}/${s.match_id}`, previous=this.last;
      if(previous&&previous.key===key&&s.event_id<=previous.event)return;
      this.last={key,event:s.event_id};
      // First snapshot or a new session establishes a baseline; never replay old sounds.
      if(!previous||previous.key!==key)return;
      if(enabled&&this.ready)this.play(s.sound);
    }
  }
  root.MatchAudio=MatchAudio;
  if(typeof module!=='undefined')module.exports=MatchAudio;
})(typeof window!=='undefined'?window:globalThis);
