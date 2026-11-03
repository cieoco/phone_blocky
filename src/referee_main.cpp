#include "competition/Runtime.h"
#include "competition/WebSupport.h"
#include "competition/RefereeStation.h"
using namespace Competition;

namespace {
Settings settings;
Radio radio;
MatchEngine engine;
RefereeStation station;
StationTicket matchNetwork;
AsyncWebServer server(80);
Snapshot snapshot;
Jobs jobs;
uint64_t bootId=0;
bool initialized=false;
bool releasing=false;
struct Player {
  Team team;
  uint64_t boot=0,previousBoot=0;
  uint32_t tx=0, rx=0, readyGen=0, commitGen=0, appliedGen=0, hash=0, disabledGen=0;
  Time lastRx=0, lastStatus=0, lastPing=0, lastSend=0, pingT1=0, lastRenew=0, lastView=0, lastDisable=0;
  int64_t offset=0;
  uint32_t uncertainty=1000;
  bool syncPending=false, locked=true, fault=false, sync=false, owned=true;
};
Player players[MaxPlayers]; size_t playerCount=0, cursor=0;
Time lastSnapshot=0, generationAt=0;
uint32_t observedGen=0;
uint32_t wireSequence=0;
String lastRequest, lastResult, owner;
Time ownerSeen=0;
struct RequestResult {String id,result;};
RequestResult results[16]; size_t resultIndex=0;
String serialLine;

void publish() {
  DynamicJsonDocument d(12288); Time now=nowMs();
  d["role"]="referee"; d["ready"]=initialized && station.ready(); d["boot_id"]=hexId(bootId); d["mac"]=WiFi.macAddress();
  d["wifi_mode"]="AP_STA";d["wifi_connected"]=station.connected();d["wifi_status"]=station.status();
  d["ap_ssid"]=station.apName();d["ap_ip"]=WiFi.softAPIP().toString();
  d["ssid"]=station.ssid();d["ip"]=WiFi.localIP().toString();
  d["releasing"]=releasing;
  d["match_id"]=hexId(engine.match); d["version"]=engine.version; d["generation"]=engine.generation;
  d["state"]=stateName(engine.state); d["mode"]=modeName(engine.config.mode); d["phase_mode"]=modeName(engine.mode());
  d["first_mode"]=modeName(engine.config.first); d["phase"]=engine.phase; d["remaining_ms"]=uint32_t(engine.remaining(now));
  d["manual_ms"]=engine.config.manualMs; d["auto_ms"]=engine.config.autoMs; d["gap_ms"]=engine.config.gapMs;
  d["event_id"]=engine.event; d["sound"]=engine.sound; d["reason"]=engine.reason;
  d["request_id"]=lastRequest; d["request_result"]=lastResult;
  JsonArray history=d.createNestedArray("results");
  for(const auto& result:results)if(result.id.length()){JsonObject item=history.createNestedObject();item["id"]=result.id;item["result"]=result.result;}
  d["channel"]=station.channel(); d["owner"]=owner;
  JsonArray teams=d.createNestedArray("teams");
  for(size_t i=0;i<settings.count;++i) {JsonObject t=teams.createNestedObject();t["mac"]=macString(settings.teams[i].mac);t["name"]=settings.teams[i].name;}
  JsonArray ps=d.createNestedArray("players");
  for(size_t i=0;i<playerCount;++i) {
    Player& p=players[i]; JsonObject x=ps.createNestedObject(); x["mac"]=macString(p.team.mac); x["name"]=p.team.name;
    x["online"]=p.lastStatus && now-p.lastStatus<HealthMs;
    x["prepared"]=p.readyGen==engine.generation; x["committed"]=p.commitGen==engine.generation;
    x["locked_confirmed"]=p.locked && p.appliedGen==engine.generation && p.lastStatus && now-p.lastStatus<HealthMs;
    x["fault"]=p.fault; x["synced"]=p.sync; x["sync_error_ms"]=p.uncertainty; x["program_hash"]=p.hash;
  }
  String s;serializeJson(d,s);snapshot.set(s);
}
bool allHealthy(Time now) {
  if(!playerCount) return false;
  for(size_t i=0;i<playerCount;++i) {
    Player& p=players[i]; if(p.fault || !p.sync || !p.lastStatus || now-p.lastStatus>HealthMs) return false;
  } return true;
}
bool allReady() {if(!playerCount)return false;for(size_t i=0;i<playerCount;++i)if(players[i].readyGen!=engine.generation)return false;return true;}
bool allCommitted() {if(!playerCount)return false;for(size_t i=0;i<playerCount;++i)if(players[i].commitGen!=engine.generation)return false;return true;}
bool allLocked() {for(size_t i=0;i<playerCount;++i)if(!players[i].locked || players[i].appliedGen!=engine.generation || nowMs()-players[i].lastStatus>HealthMs)return false;return true;}
void releasePlayers() {for(size_t i=0;i<playerCount;++i)radio.remove(players[i].team.mac);playerCount=0;}
Mode parseMode(const char* s) {return !strcmp(s,"MANUAL")?Mode::Manual:!strcmp(s,"AUTO")?Mode::Auto:!strcmp(s,"MIXED")?Mode::Mixed:Mode(0);}
const char* action(JsonDocument& d) {
  const char* op=d["action"]|""; Time now=nowMs();
  if(!strcmp(op,"claim")) {
    String id=d["operator"]|""; if(id.length()<8 || id.length()>48) return "invalid_operator";
    if(owner.length() && owner!=id && now-ownerSeen<10000) return "another_operator";
    owner=id;ownerSeen=now;return "ok";
  }
  if(owner.length()==0 || owner!=String(d["operator"]|"")) return "claim_control_first";
  ownerSeen=now;
  if(!strcmp(op,"heartbeat")) return "ok";
  if(!strcmp(op,"stop")) {
    if(String(d["match_id"]|"")!=hexId(engine.match))return "wrong_match";
    return engine.stop()?"ok":"invalid_state";
  }
  if(!d["expected_version"].is<uint32_t>() || d["expected_version"].as<uint32_t>()!=engine.version) return "state_conflict";
  if(!strcmp(op,"prepare")) {
    if(engine.state!=State::Idle)return "invalid_state";
    if(!station.ready())return station.status();
    Config c; c.mode=parseMode(d["mode"]|"");c.first=parseMode(d["first_mode"]|"");
    if(!d["manual_ms"].is<uint32_t>() || !d["auto_ms"].is<uint32_t>() || !d["gap_ms"].is<uint32_t>())return "invalid_time";
    c.manualMs=d["manual_ms"];c.autoMs=d["auto_ms"];c.gapMs=d["gap_ms"];
    JsonArray selected=d["teams"].as<JsonArray>();
    if(!c.valid()||selected.size()<1||selected.size()>MaxPlayers)return "invalid_config";
    Team chosen[MaxPlayers];size_t count=0;
    for(JsonVariant v:selected) {
      uint8_t mac[6];if(!parseMac(v.as<String>(),mac))return "invalid_mac";
      int idx=settings.index(mac);if(idx<0)return "not_paired";
      for(size_t j=0;j<count;++j)if(!memcmp(mac,chosen[j].mac,6))return "duplicate_team";
      chosen[count++]=settings.teams[idx];
    }
    releasePlayers();
    for(size_t i=0;i<count;++i) {
      players[i]=Player();players[i].team=chosen[i];
      if(!radio.add(chosen[i],0)){releasePlayers();return "peer_add_failed";} // follow current STA channel
      ++playerCount;
    }
    matchNetwork=station.ticket();engine.prepare(c,randomId(),now);return "ok";
  }
  if(!strcmp(op,"start"))return engine.begin(now)?"ok":"invalid_state";
  if(!strcmp(op,"pause"))return engine.pause(now)?"ok":"invalid_state";
  if(!strcmp(op,"resume"))return engine.resume(now)?"ok":"invalid_state";
  if(!strcmp(op,"cancel")) {
    if(engine.state!=State::Ready && engine.state!=State::Preparing)return "invalid_state";
    engine.stop();return "ok";
  }
  if(!strcmp(op,"reset")) {
    if(!engine.terminal())return "invalid_state";
    if(!allLocked())return "stop_not_confirmed";
    releasing=true;return "ok"; // Release is acknowledged before removing peers.
  }
  return "unknown_action";
}
void webJobs() {
  WebJob j; if(!jobs.pop(j))return;
  DynamicJsonDocument d(2048); if(deserializeJson(d,j.json))return;
  String id=d["request_id"]|""; if(id.length()<1 || id.length()>48)return;
  for(auto& r:results) if(r.id==id){lastRequest=id;lastResult=r.result;return;}
  lastRequest=id;lastResult=initialized?action(d):"not_initialized";
  results[resultIndex]={lastRequest,lastResult}; resultIndex=(resultIndex+1)%16;
}
void receivePackets() {
  Received r;Packet packet;
  for(int n=0;n<12 && radio.receive(r,packet);++n) {
    Player* pp=nullptr;for(size_t i=0;i<playerCount;++i)if(!memcmp(players[i].team.mac,r.mac,6))pp=&players[i];
    if(!pp || packet.controllerBoot!=bootId)continue; Player& p=*pp;
    if(p.boot && p.boot!=packet.deviceBoot) {
      if(packet.type!=Message::Pong || get(packet.payload,8)!=p.pingT1 || packet.deviceBoot==p.previousBoot)continue;
      engine.fault("student_rebooted");p.previousBoot=p.boot;p.boot=packet.deviceBoot;
      p.rx=p.readyGen=p.commitGen=p.disabledGen=0;p.lastStatus=0;
    }
    if(packet.sequence<=p.rx || !packet.deviceBoot)continue;
    p.boot=packet.deviceBoot;p.rx=packet.sequence;p.lastRx=r.received;
    if(packet.type==Message::Pong) {
      Time t1=get(packet.payload,8),t2=get(packet.payload+8,8),t3=get(packet.payload+16,8),t4=r.received;
      if(t1!=p.pingT1 || t4<t1 || t3<t2 || t4-t1<t3-t2 || t4-t1>100)continue;
      Time rtt=(t4-t1)-(t3-t2); if(rtt>18)continue;
      p.offset=((int64_t(t1)-int64_t(t2))+(int64_t(t4)-int64_t(t3)))/2;
      p.uncertainty=uint32_t((rtt+1)/2)+1;p.syncPending=true;
    } else if(packet.type==Message::Status && packet.match==engine.match) {
      p.lastStatus=r.received;p.appliedGen=packet.generation;p.locked=packet.payload[13];p.fault=packet.payload[3];p.sync=packet.payload[14];
      p.hash=uint32_t(get(packet.payload+8,4));
      p.owned=packet.payload[15];
      if(packet.generation==engine.generation) {
        p.readyGen=packet.payload[0] && p.sync && !p.fault?packet.generation:0;
        p.commitGen=packet.payload[1] && p.sync && !p.fault?packet.generation:0;
      }
    }
  }
}
void transmit() {
  if(!playerCount || !station.ready())return;
  if(engine.active() && !station.matches(matchNetwork))engine.fault("referee_network_changed");
  Time now=nowMs();
  for(size_t n=0;n<playerCount;++n) {
    Player& p=players[(cursor+n)%playerCount];
    if(now-p.lastSend<20)continue;
    Packet q;q.controllerBoot=bootId;q.match=engine.match;q.deviceBoot=p.boot;q.generation=engine.generation;q.sequence=wireSequence+1;q.sent=now;
    if(!q.sequence){engine.fault("sequence_exhausted");return;}
    bool disable=engine.terminal() || engine.state==State::Paused;
    if(releasing && (!p.boot || now-p.lastPing>=700)) {q.type=Message::Ping;q.deviceBoot=0;}
    else if(releasing && p.disabledGen!=engine.generation) {q.type=Message::Disable;q.payload[0]=1;}
    else if(releasing) {q.type=Message::Release;}
    else if(disable && (p.disabledGen!=engine.generation || now-p.lastDisable>=RenewMs)) {q.type=Message::Disable;q.payload[0]=engine.terminal();}
    else if(p.syncPending) {q.type=Message::Sync;put(q.payload,uint64_t(p.offset),8);put(q.payload+8,p.uncertainty,4);}
    else if(!p.boot || now-p.lastPing>=700) {q.type=Message::Ping;q.deviceBoot=0;}
    else if(engine.active() && engine.state!=State::Paused && p.readyGen!=engine.generation) {
      q.type=Message::Prepare;q.payload[0]=uint8_t(engine.mode());q.payload[1]=engine.config.mode!=Mode::Manual;
    } else if(engine.scheduling() && p.commitGen!=engine.generation) {
      q.type=Message::Commit;q.effective=engine.start;put(q.payload,engine.end,8);
    } else if((engine.running() || (engine.scheduling() && engine.guardPassed)) && now-p.lastRenew>=RenewMs) {
      q.type=Message::Renew;q.until=std::min(now+LeaseMs,engine.end);
    } else if(now-p.lastView>=100) {
      q.type=Message::View;q.payload[0]=uint8_t(engine.state);q.payload[1]=uint8_t(engine.mode());put(q.payload+2,engine.remaining(now),4);
    } else continue;
    q.length=payloadSize(q.type);
    if(radio.send(p.team.mac,q)) {
      wireSequence=q.sequence;p.tx=q.sequence;p.lastSend=now;
      if(q.type==Message::Ping){p.lastPing=now;p.pingT1=now;}
      if(q.type==Message::Sync)p.syncPending=false;
      if(q.type==Message::Disable){p.disabledGen=engine.generation;p.lastDisable=now;}
      if(q.type==Message::Renew)p.lastRenew=now;
      if(q.type==Message::View)p.lastView=now;
      cursor=(cursor+n+1)%playerCount;
    }
    return;
  }
}
void serialCommands() {
  while(Serial.available()) {
    char c=char(Serial.read());if(c=='\r')continue;
    if(c!='\n'){if(serialLine.length()<1024)serialLine+=c;else serialLine="";continue;}
    StaticJsonDocument<1024> d; bool parsed=!deserializeJson(d,serialLine);serialLine="";
    if(!parsed)continue;const char* cmd=d["cmd"]|"";
    StaticJsonDocument<512> out;out["ok"]=false;
    if(!strcmp(cmd,"competition_info")) {
      out["ok"]=true;out["role"]="referee";out["mac"]=WiFi.macAddress();out["channel"]=station.channel();
      out["wifi_mode"]="AP_STA";out["wifi_connected"]=station.connected();out["wifi_status"]=station.status();
      out["ap_ssid"]=station.apName();out["ap_ip"]=WiFi.softAPIP().toString();
      out["ssid"]=station.ssid();out["ip"]=WiFi.localIP().toString();out["key"]=settings.secret;
    }
    else if(!strcmp(cmd,"competition_pair") && engine.state==State::Idle) {
      Team t;String name=d["name"]|"";
      if(parseMac(d["mac"]|"",t.mac)&&parseKey(d["key"]|"",t.key)&&name.length()&&name.length()<sizeof(t.name)) {
        name.toCharArray(t.name,sizeof(t.name));out["ok"]=settings.saveTeam(t);
      }
    } else if(!strcmp(cmd,"competition_channel")) {out["error"]="STA channel is assigned by the router";}
    serializeJson(out,Serial);Serial.println();
  }
}
}

void setup() {
  Serial.begin(115200);bootId=randomId();
  if(!settings.load()){Serial.println("competition settings failed");return;}
  station.begin(settings.secret,settings.channel);initialized=radio.begin(settings.pmk);
  bool fs=LittleFS.begin(false); if(!fs)Serial.println("Upload referee filesystem before opening the web page.");
  server.on("/api/competition/state",HTTP_GET,[](AsyncWebServerRequest* r){r->send(200,"application/json",snapshot.get());});
  server.addHandler(new JsonEndpoint("/api/competition/action",1535,[](AsyncWebServerRequest* r,const char* data,size_t n){
    if(!authenticate(r,settings.secret))return;
    StaticJsonDocument<2048> d;if(deserializeJson(d,data,n) || !d["request_id"].is<const char*>()){errorResponse(r,400,"invalid_request");return;}
    if(!jobs.push(data,n)){errorResponse(r,503,"queue_full");return;}
    r->send(202,"application/json","{\"ok\":true,\"queued\":true}");
  }));
  staticFiles(server,"/competition.html");publish();server.begin();
}
void loop() {
  if(!initialized){delay(100);return;}
  Time now=nowMs();station.tick();receivePackets();
  if(releasing) {
    bool released=playerCount>0;
    for(size_t i=0;i<playerCount;++i)if(players[i].owned || players[i].appliedGen!=engine.generation || now-players[i].lastStatus>HealthMs)released=false;
    if(released){engine.reset();releasePlayers();releasing=false;}
  }
  if(engine.generation!=observedGen){observedGen=engine.generation;generationAt=now;}
  if(radio.failed())engine.fault("radio_queue_or_callback_failure");
  if(engine.active() && !station.matches(matchNetwork))engine.fault("referee_network_changed");
  if(engine.state==State::Preparing && allReady())engine.ready();
  bool healthy=allHealthy(now);
  engine.tick(now,healthy,allCommitted()); // deadlines take precedence over UI actions
  webJobs();serialCommands();transmit();
  if(now-lastSnapshot>=100){lastSnapshot=now;publish();}
  delay(1);
}
