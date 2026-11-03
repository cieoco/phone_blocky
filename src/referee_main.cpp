#include "competition/Runtime.h"
#include "competition/WebSupport.h"
#include "competition/RefereeStation.h"
#include "competition/PairingPolicy.h"
#include "competition/PairingCrypto.h"
#include "competition/AudioRoute.h"
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
PairingCrypto pairingCrypto;
struct Discovered {
  uint8_t mac[6]={},controller[6]={},publicKey[65]={},flags=0;
  char name[33]={};uint64_t boot=0,ticket=0;Time seen=0;
};
Discovered discovered[32];size_t discoveredCount=0;
uint64_t discoveryId=0;Time scanUntil=0,scanSent=0;
struct PairAttempt {
  bool active=false;Team team;uint64_t device=0,session=0,ticket=0;
  Time until=0,sent=0;uint8_t channel=0;
} pairing;
String pairResult,pairMac;
AudioRoute audioRoute;

void publish() {
  DynamicJsonDocument d(32768); Time now=nowMs();
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
  d["audio_route"]=audioRoute.name(now);
  d["audio_owner"]=audioRoute.central(now)?hexId(audioRoute.holder):String("");
  d["audio_lease_ms"]=audioRoute.central(now)?uint32_t(audioRoute.until-now):0;
  d["request_id"]=lastRequest; d["request_result"]=lastResult;
  JsonArray history=d.createNestedArray("results");
  for(const auto& result:results)if(result.id.length()){JsonObject item=history.createNestedObject();item["id"]=result.id;item["result"]=result.result;}
  d["channel"]=station.channel(); d["owner"]=owner;
  d["scanning"]=now<scanUntil;d["pairing"]=pairing.active;
  d["pair_result"]=pairResult;d["pair_mac"]=pairMac;
  JsonArray found=d.createNestedArray("discovered");
  for(size_t i=0;i<discoveredCount;++i) {
    const auto& car=discovered[i];JsonObject t=found.createNestedObject();
    t["mac"]=macString(car.mac);t["name"]=car.name;t["controller_mac"]=car.flags&1?macString(car.controller):String("");
    t["fresh"]=now-car.seen<DiscoveryFreshMs;t["managed"]=bool(car.flags&2);t["committed"]=bool(car.flags&4);
    t["available"]=bool(car.flags&8) && now-car.seen<DiscoveryFreshMs;
  }
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
  // Authenticated by the HTTP endpoint, but does not require or acquire the
  // referee operator's control token. Central audio is a separate lease.
  if(!strcmp(op,"audio_heartbeat") || !strcmp(op,"audio_release")) {
    uint64_t id=0;if(!parseId(d["audio_id"]|"",id) || !id)return "invalid_audio_id";
    bool ok=!strcmp(op,"audio_heartbeat")?audioRoute.heartbeat(id,now):audioRoute.release(id);
    return ok?"ok":"another_audio_station";
  }
  if(!strcmp(op,"claim")) {
    String id=d["operator"]|""; if(id.length()<8 || id.length()>48) return "invalid_operator";
    if(owner.length() && owner!=id && now-ownerSeen<10000) return "another_operator";
    owner=id;ownerSeen=now;return "ok";
  }
  if(owner.length()==0 || owner!=String(d["operator"]|"")) return "claim_control_first";
  ownerSeen=now;
  if(!strcmp(op,"heartbeat")) return "ok";
  if(!strcmp(op,"audio_local"))return audioRoute.takeLocal(now)?"ok":"central_audio_online";
  if(!strcmp(op,"stop")) {
    if(String(d["match_id"]|"")!=hexId(engine.match))return "wrong_match";
    return engine.stop()?"ok":"invalid_state";
  }
  if(!d["expected_version"].is<uint32_t>() || d["expected_version"].as<uint32_t>()!=engine.version) return "state_conflict";
  if(!strcmp(op,"scan")) {
    if(engine.state!=State::Idle || pairing.active)return "invalid_state";
    if(!station.ready())return station.status();
    discoveryId=randomId();scanUntil=now+15000;scanSent=0;discoveredCount=0;return "ok";
  }
  if(!strcmp(op,"pair")) {
    if(engine.state!=State::Idle || pairing.active)return "invalid_state";
    if(!station.ready())return station.status();
    uint8_t mac[6];if(!parseMac(d["mac"]|"",mac))return "invalid_mac";
    Discovered* car=nullptr;for(size_t i=0;i<discoveredCount;++i)if(!memcmp(mac,discovered[i].mac,6))car=&discovered[i];
    if(!car || now-car->seen>=DiscoveryFreshMs)return "discovery_stale";
    if(!(car->flags&8) || car->flags&4)return "student_consent_required";
    Team team;memcpy(team.mac,mac,6);String name=d["name"]|"";
    if(!name.length() || name.length()>=sizeof(team.name))return "invalid_name";
    name.toCharArray(team.name,sizeof(team.name));uint64_t session=randomId();
    if(!pairingCrypto.derive(car->publicKey,bootId,car->boot,session,team.key))return "key_exchange_failed";
    // Store the derived key before sending an offer. ACK loss/reboot must not
    // strand a student whose new binding has already become durable.
    if(!settings.saveTeam(team))return "pairing_write_failed";
    radio.remove(team.mac);
    if(!radio.add(team,0))return "peer_add_failed";
    pairing=PairAttempt();pairing.active=true;pairing.team=team;pairing.device=car->boot;
    pairing.session=session;pairing.ticket=car->ticket;pairing.until=now+12000;pairing.channel=station.channel();
    pairMac=macString(mac);pairResult="pending";return "ok";
  }
  if(!strcmp(op,"prepare")) {
    if(engine.state!=State::Idle || pairing.active)return "invalid_state";
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
    scanUntil=0;matchNetwork=station.ticket();engine.prepare(c,randomId(),now);return "ok";
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
    if(packet.type==Message::Announce) {
      if(nowMs()>=scanUntil || packet.controllerBoot!=bootId || packet.match!=discoveryId || !packet.deviceBoot)continue;
      size_t i=0;while(i<discoveredCount && memcmp(r.mac,discovered[i].mac,6))++i;
      if(i==discoveredCount){if(discoveredCount>=32)continue;++discoveredCount;}
      auto& car=discovered[i];memcpy(car.mac,r.mac,6);car.boot=packet.deviceBoot;car.ticket=get(packet.payload,8);
      memcpy(car.publicKey,packet.payload+8,65);memcpy(car.controller,packet.payload+73,6);car.flags=packet.payload[79];
      memcpy(car.name,packet.payload+80,32);car.name[32]=0;car.seen=nowMs();continue;
    }
    if(packet.type==Message::PairAck) {
      if(pairing.active && !memcmp(r.mac,pairing.team.mac,6) && packet.controllerBoot==bootId &&
         packet.deviceBoot==pairing.device && packet.match==pairing.session && validPairAck(pairing.team.key,packet)) {
        pairing.active=false;pairResult="ok";radio.remove(pairing.team.mac);
      }
      continue;
    }
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
void discoveryTransmit() {
  if(engine.state!=State::Idle)return;
  Time now=nowMs();
  if(pairing.active && (now>=pairing.until || !station.ready() || station.channel()!=pairing.channel)) {
    pairing.active=false;pairResult="pairing_unconfirmed";radio.remove(pairing.team.mac);
  }
  if(!station.ready())return;
  Packet p;p.controllerBoot=bootId;p.sequence=wireSequence+1;p.sent=now;
  if(pairing.active && now-pairing.sent>=300) {
    p.type=Message::PairOffer;p.deviceBoot=pairing.device;p.match=pairing.session;
    memcpy(p.payload,pairing.team.mac,6);put(p.payload+6,pairing.ticket,8);memcpy(p.payload+14,pairingCrypto.bytes,65);
  } else if(now<scanUntil && now-scanSent>=750) {p.type=Message::Discover;p.match=discoveryId;}
  else return;
  p.length=payloadSize(p.type);
  if(radio.send(Radio::broadcastMac(),p)) {
    ++wireSequence;if(p.type==Message::Discover)scanSent=now;else pairing.sent=now;
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
  station.begin(settings.secret,settings.channel);initialized=pairingCrypto.begin() && radio.begin(settings.pmk) && radio.addBroadcast();
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
  webJobs();serialCommands();transmit();discoveryTransmit();
  if(now-lastSnapshot>=100){lastSnapshot=now;publish();}
  delay(1);
}
