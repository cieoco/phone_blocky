#include "Student.h"
#if COMPETITION_COMPETITOR
#include "../CommandProcessor.h"
#include "Runtime.h"
#include "InputPolicy.h"
#include "WebSupport.h"
#include <new>

extern MotorController motorController;
extern ServoController servoController;
extern PWMManager pwmManager;
extern CommandProcessor cmdProcessor;
using namespace Competition;
namespace {
auto& motors=motorController; auto& servos=servoController;
auto& pwm=pwmManager; auto& processor=cmdProcessor;
Settings settings; Radio radio; Lease lease;
Snapshot snapshot;
AsyncWebSocket* socket=nullptr;
SemaphoreHandle_t mutationMutex=nullptr;
QueueHandle_t commandQueue=nullptr;
struct Input {String json;uint8_t source;uint32_t client,epoch;};
std::atomic<bool> owned{true}, localStop{false};
std::atomic<uint32_t> epoch{0}, owner{0};
bool initialized=false, settingsOk=false, isPaired=false, synced=false, outputsOn=false, programRunning=false;
bool releasePending=false;
bool ownershipDurable=false;
uint64_t bootId=0,controllerBoot=0,previousControllerBoot=0,runMatch=0,runController=0,outputMask=0;
uint32_t txSeq=0,rxSeq=0,runGeneration=0,programHash=0,manualSeq=0;
Time syncedAt=0,statusAt=0,snapshotAt=0,manualDeadline=0,viewAt=0;
int64_t offsetMs=0;uint32_t uncertainty=1000;
State viewState=State::Idle;uint32_t viewRemaining=0;
String program;
ControlTiming::LoopCadence programCadence;
struct Context {uint32_t token=0,generation=0;Time until=0;};
Context contexts[3];unsigned contextIndex=0;
Packet pendingPong;bool havePong=false;
Time controllerNow() {int64_t n=int64_t(nowMs())+offsetMs;return n>0?Time(n):0;}
bool clockHealthy() {return synced && uncertainty<=10 && nowMs()-syncedAt<SyncMaxAgeMs;}
bool manualAllowed() {
  return owned && initialized && !localStop && clockHealthy() && lease.mode==Mode::Manual &&
    lease.allowed(controllerNow()+uncertainty) && controllerNow()>=lease.start+uncertainty;
}
bool permitted() {
  if(!owned)return true;
  if(!initialized || localStop || !clockHealthy() || !lease.allowed(controllerNow()+uncertainty) || controllerNow()<lease.start+uncertainty)return false;
  return lease.mode==Mode::Auto || (owner && nowMs()<manualDeadline);
}
void clearContexts() {for(auto& c:contexts)c=Context();manualDeadline=0;manualSeq=0;owner=0;++epoch;}
void stopOutputs() {
  outputsOn=false;motors.stopAll();processor.stopSysId();processor.i2cStopAll();
  servos.disableOutputs();pwm.disableOutputs();manualDeadline=0;
  uint64_t pins=outputMask|CompetitionGate::digitalOutputs();CompetitionGate::digitalOutputs()=0;
  for(int pin=0;pin<40;++pin)if(pins&(uint64_t(1)<<pin)){pinMode(pin,OUTPUT);digitalWrite(pin,LOW);}
}
bool persistOwned(bool value) {
  Preferences p;if(!p.begin("competition",false))return false;
  bool ok=p.putBool("owned",value)==1 && p.getBool("owned",!value)==value;p.end();
  if(ok)ownershipDurable=value;return ok;
}
bool safeProgram(JsonVariantConst v) {
  if(v.is<JsonArrayConst>()) {for(JsonVariantConst x:v.as<JsonArrayConst>())if(!safeProgram(x))return false;}
  if(!v.is<JsonObjectConst>())return true;
  const char* c=v["cmd"].is<const char*>()?v["cmd"].as<const char*>():v["command"]|"";
  for(const char* key:{"pin","trigPin","echoPin"}) {
    if(!v.as<JsonObjectConst>().containsKey(key))continue;
    int pin=CommandProcessor::parseIntOrString(v[key],-1);
    if(pin!=2 && pin!=4 && pin!=15 && pin!=32 && pin!=33 &&
       !(pin>=0 && pin<40 && (outputMask&(uint64_t(1)<<pin))))return false;
  }
  if(!strncmp(c,"blockly_func",12) || !strcmp(c,"registerFunction") || !strcmp(c,"setExternalValue"))return false;
  if(!strcmp(c,"digitalWrite") || !strcmp(c,"analogWrite") ||
     (!strcmp(c,"pinMode") && (String(v["mode"]|"")=="OUTPUT" || v["mode"].as<int>()==OUTPUT))) {
    int pin=CommandProcessor::parseIntOrString(v["pin"],-1);
    if(pin<0 || pin>=40 || !(outputMask&(uint64_t(1)<<pin)))return false;
  }
  if(!strcmp(c,"arduinoUltrasonic") && (CommandProcessor::parseIntOrString(v["trigPin"],-1)!=2 || CommandProcessor::parseIntOrString(v["echoPin"],-1)!=33))return false;
  for(JsonPairConst x:v.as<JsonObjectConst>())if(!safeProgram(x.value()))return false;
  return true;
}
bool validateProgram(const String& json) {
  DynamicJsonDocument d(16384);if(deserializeJson(d,json) || d.overflowed() || !safeProgram(d.as<JsonVariantConst>()))return false;
  std::vector<BlocklyCommand> a,b;std::vector<BlocklyProcedure> p;
  return processor.parseProgramDoc(d,a,b,p) && (!a.empty() || !b.empty());
}
uint32_t hashProgram(const String& s) {uint32_t h=2166136261u;for(size_t i=0;i<s.length();++i)h=(h^uint8_t(s[i]))*16777619u;return h;}

bool loadSnapshot(bool needAuto,bool newMatch) {
  if(!needAuto){if(newMatch){program="";programHash=0;}return true;}
  if(!newMatch && program.length())return true;
  String candidate=ProgramStore::loadJson();if(!validateProgram(candidate))return false;
  program=candidate;programHash=hashProgram(program);return true;
}
void publish() {
  StaticJsonDocument<1024>d;d["type"]="competition_state";d["role"]="competitor";
  d["managed"]=owned.load();d["paired"]=isPaired;d["ready"]=initialized;
  d["mac"]=WiFi.macAddress();d["channel"]=settings.channel;d["boot_id"]=hexId(bootId);
  d["match_id"]=hexId(lease.match);d["generation"]=lease.generation;d["epoch"]=epoch.load();
  d["mode"]=modeName(lease.mode);d["locked"]=owned && !permitted();
  d["fault"]=owned && lease.faulted;d["synced"]=clockHealthy();d["editable"]=!owned;
  d["state"]=!owned?"TEACHING":lease.faulted?"FAULT":stateName(viewState);
  bool fresh=viewAt && nowMs()-viewAt<500;
  d["state_fresh"]=!owned || fresh;
  d["remaining_ms"]=fresh?viewRemaining:0;
  d["allowed"]=manualAllowed();
  if(manualAllowed()) {
    auto& c=contexts[contextIndex++%3];c.token=esp_random()|1;c.generation=lease.generation;c.until=nowMs()+250;
    d["token"]=c.token;
  }
  String s;serializeJson(d,s);snapshot.set(s);
  if(socket)socket->textAll(s);
}
void sendStatus() {
  if(!isPaired || !controllerBoot)return;
  Packet p;p.controllerBoot=controllerBoot;p.deviceBoot=bootId;p.match=lease.match;p.generation=lease.generation;
  p.sequence=txSeq+1;p.sent=nowMs();
  if(!p.sequence){lease.fail();return;}
  if(havePong){p=pendingPong;p.sequence=txSeq+1;p.sent=nowMs();put(p.payload+16,p.sent,8);}
  else {
    p.type=Message::Status;p.payload[0]=lease.prepared;p.payload[1]=lease.committed;p.payload[2]=outputsOn;p.payload[3]=lease.faulted;
    put(p.payload+4,rxSeq,4);put(p.payload+8,programHash,4);p.payload[12]=uint8_t(lease.mode);p.payload[13]=!outputsOn;p.payload[14]=clockHealthy();p.payload[15]=owned.load();
  }
  p.length=payloadSize(p.type);
  if(radio.send(settings.teams[0].mac,p)){++txSeq;statusAt=nowMs();if(havePong)havePong=false;}
}

void receivePackets() {
  if(!isPaired)return;Received r;Packet p;
  for(int i=0;i<12&&radio.receive(r,p);++i) {
    if(memcmp(r.mac,settings.teams[0].mac,6) || !p.controllerBoot || (p.deviceBoot && p.deviceBoot!=bootId))continue;
    if(p.controllerBoot!=controllerBoot) {
      if(p.type!=Message::Ping || p.controllerBoot==previousControllerBoot)continue;
      if(controllerBoot && owned){lease.fail();stopOutputs();}
      previousControllerBoot=controllerBoot;controllerBoot=p.controllerBoot;rxSeq=0;synced=false;
    }
    if(p.sequence<=rxSeq)continue;
    rxSeq=p.sequence;
    if(p.type==Message::Ping) {
      pendingPong=Packet();pendingPong.type=Message::Pong;pendingPong.controllerBoot=controllerBoot;pendingPong.match=p.match;
      pendingPong.deviceBoot=bootId;pendingPong.generation=p.generation;
      put(pendingPong.payload,p.sent,8);put(pendingPong.payload+8,r.received,8);havePong=true;
    } else if(p.type==Message::Sync) {
      int64_t off=int64_t(get(p.payload,8));uint32_t error=uint32_t(get(p.payload+8,4));
      if(error>10 || llabs(int64_t(r.received)+off-int64_t(p.sent))>100 ||
         (synced && lease.committed && llabs(off-offsetMs)>10)){lease.fail();continue;}
      offsetMs=off;uncertainty=error;syncedAt=nowMs();synced=true;
    } else if(p.type==Message::Prepare) {
      if(!clockHealthy())continue;
      bool newMatch=p.match!=lease.match || p.controllerBoot!=lease.controllerBoot;
      bool newGeneration=newMatch || p.generation!=lease.generation;
      if(!newMatch && (lease.faulted || lease.terminal || p.generation<lease.generation))continue;
      if(newGeneration){
        owned=true;clearContexts();stopOutputs();viewState=State::Preparing;viewAt=0;
        if(!ownershipDurable && !persistOwned(true)){lease.fail();continue;}
      }
      if(!loadSnapshot(p.payload[1]!=0,newMatch)){lease.fail();lease.match=p.match;lease.generation=p.generation;continue;}
      lease.prepare(controllerBoot,p.match,p.generation,Mode(p.payload[0]),p.sequence);
    } else if(p.type==Message::Disable && p.match && !lease.committed &&
              (lease.match==0 || lease.terminal || lease.faulted)) {
      // Cancellation may precede PREPARE (or its validation may have failed).
      // Install a locked tombstone so the referee can confirm all outputs are off.
      owned=true;if(!ownershipDurable && !persistOwned(true))lease.fail();
      lease.controllerBoot=controllerBoot;lease.match=p.match;
      if(lease.disable(p.generation,p.sequence,p.payload[0]!=0)){clearContexts();stopOutputs();}
    } else if(p.match==lease.match && p.controllerBoot==lease.controllerBoot) {
      if(p.type==Message::View && p.generation==lease.generation && p.payload[0]<=uint8_t(State::Fault)) {
        viewState=State(p.payload[0]);viewRemaining=uint32_t(get(p.payload+2,4));viewAt=nowMs();continue;
      }
      if(p.type==Message::Release && p.generation==lease.generation && lease.terminal && !lease.committed) {
        if(owned) {
          clearContexts();stopOutputs();releasePending=true;
        }
        continue;
      }
      Time now=controllerNow()+uncertainty;
      if(p.type==Message::Commit)lease.commit(p.generation,p.sequence,p.effective,get(p.payload,8),now);
      else if(p.type==Message::Renew && clockHealthy())lease.renew(p.generation,p.sequence,p.sent,p.until,now);
      else if(p.type==Message::Disable) {
        if(lease.disable(p.generation,p.sequence,p.payload[0]!=0)){clearContexts();stopOutputs();}
      }
    }
  }
}

void reply(const Input& in,const char* error) {
  StaticJsonDocument<160>d;d["ok"]=false;d["err"]=error;String s;serializeJson(d,s);
  if(in.client && socket)socket->text(in.client,s);else Serial.println(s);
}
bool adminCommand(JsonDocument& d) {
  const char* cmd=d["cmd"]|"";if(strncmp(cmd,"competition_",12))return false;
  StaticJsonDocument<512>out;out["ok"]=false;
  if(!strcmp(cmd,"competition_info")) {
    out["ok"]=true;out["role"]="competitor";out["mac"]=WiFi.macAddress();out["channel"]=settings.channel;
    out["ssid"]=WiFi.softAPSSID();out["key"]=strlen(CONFIG_AP_PASSWORD)>=8?CONFIG_AP_PASSWORD:"12345678";
    out["managed"]=owned.load();
  } else if(!strcmp(cmd,"competition_stop")){if(owned)localStop=true;else stopOutputs();out["ok"]=true;}
  else if(!owned && !strcmp(cmd,"competition_pair")) {
    Team t;String name=d["name"]|"controller";
    if(parseMac(d["mac"]|"",t.mac)&&parseKey(d["key"]|"",t.key)&&name.length()<sizeof(t.name)) {
      name.toCharArray(t.name,sizeof(t.name));out["ok"]=settings.saveTeam(t,true);out["restart_required"]=true;
    }
  } else if(!owned && !strcmp(cmd,"competition_channel")){out["ok"]=settings.setChannel(d["channel"]|0);out["restart_required"]=true;}
  else if(!owned && !strcmp(cmd,"competition_outputs")) {
    uint64_t mask=0;bool valid=d["pins"].is<JsonArray>();
    for(JsonVariant v:d["pins"].as<JsonArray>()){int pin=v|(-1);if(pin!=4&&pin!=15&&pin!=18&&pin!=26&&pin!=32)valid=false;else mask|=uint64_t(1)<<pin;}
    Preferences p;if(valid && p.begin("competition",false)){out["ok"]=p.putULong64("outputs",mask)==8;p.end();out["restart_required"]=true;}
  }
  serializeJson(out,Serial);Serial.println();return true;
}
void processInputs() {
  Input* in=nullptr;
  // One command per pass bounds radio latency, especially original joy processing.
  if(xQueueReceive(commandQueue,&in,0)!=pdTRUE)return;
  if(in->epoch!=epoch.load()){reply(*in,"competition_state_changed");delete in;return;}
  if(in->client && (!socket || !socket->client(in->client) || socket->client(in->client)->status()!=WS_CONNECTED)){delete in;return;}
  DynamicJsonDocument d(16384);
  if(deserializeJson(d,in->json)){reply(*in,"json_parse_failed");delete in;return;}
  if(in->source==Comm::CH_SERIAL && adminCommand(d)){delete in;return;}
  const char* cmd=d["cmd"]|"";
  // A delayed match command must never turn into an unrestricted teaching command.
  if(!owned && d.containsKey("_competition")){reply(*in,"competition_state_changed");delete in;return;}
  if(owned) {
    if(d["message_success"]==true){processor.resetWaitingForResponse();delete in;return;}
    if(!strcmp(cmd,"competition_stop")) {localStop=true;delete in;return;}
    InputKind kind=inputKind(d["mode"]|"",cmd,d.containsKey("command"));
    if(kind!=InputKind::Read) {
      bool motion=kind==InputKind::Motion;
      Context* found=nullptr;uint32_t token=d["_competition"]["token"]|0;
      uint32_t seq=d["_competition"]["sequence"]|0;
      for(auto& c:contexts)if(c.token && c.token==token && c.generation==lease.generation && nowMs()<c.until)found=&c;
      if(!motion || !in->client || !manualAllowed() || !found || !seq || d["_competition"]["generation"]!=lease.generation ||
         (owner && owner!=in->client) || seq<=manualSeq) {
        reply(*in,"competition_locked_or_stale");delete in;return;
      }
      owner=in->client;manualSeq=seq;manualDeadline=found->until;outputsOn=true;
    }
  }
  processor.processCommands(in->json,in->source);delete in;
}
void service() {
  if(!initialized)return;
  // MutationGuard serializes legacy HTTP/USB actions with acquiring ownership.
  if(xSemaphoreTakeRecursive(mutationMutex,0)!=pdTRUE)return;
  if(isPaired)receivePackets();
  if(owned) {
    if(radio.failed() || WiFi.channel()!=settings.channel || localStop.exchange(false))lease.fail();
    if(lease.committed && !clockHealthy())lease.fail();
    lease.tick(controllerNow()+uncertainty);
    if(!permitted() && outputsOn)stopOutputs();
    if(!lease.committed && programRunning){processor.requestProgramStop();programRunning=false;}
  }
  // Called recursively from Blockly yield: do not execute another command there.
  static bool processing=false;
  if(!processing){processing=true;processInputs();processing=false;}
  if(isPaired && (havePong || nowMs()-statusAt>=80))sendStatus();
  if(nowMs()-snapshotAt>=100){snapshotAt=nowMs();publish();}
  xSemaphoreGiveRecursive(mutationMutex);
}
}

namespace Student {
bool managed(){return owned.load();}
bool paired(){return isPaired;}
uint8_t channel(){return settings.channel;}
MutationGuard::MutationGuard():held_(mutationMutex && xSemaphoreTakeRecursive(mutationMutex,portMAX_DELAY)==pdTRUE){}
MutationGuard::~MutationGuard(){if(held_)xSemaphoreGiveRecursive(mutationMutex);}
bool MutationGuard::allowed()const{return held_ && !owned;}
void disconnected(uint32_t client){if(owned && owner.load()==client)localStop=true;}
void commandError(){if(owned)localStop=true;}
bool submit(const String& json,uint8_t source,uint32_t client) {
  if(!commandQueue || json.length()>32768)return false;
  auto* in=new(std::nothrow) Input{json,source,client,epoch.load()};
  if(!in || xQueueSend(commandQueue,&in,0)!=pdTRUE){delete in;if(owned)localStop=true;return false;}return true;
}
void boot() {
  bootId=randomId();mutationMutex=xSemaphoreCreateRecursiveMutex();commandQueue=xQueueCreate(8,sizeof(Input*));
  CompetitionGate::checker()=permitted;CompetitionGate::managedChecker()=managed;
  settingsOk=settings.load();isPaired=settings.count==1;
  Preferences p;
  if(settingsOk && p.begin("competition",true)) {
    owned=p.getBool("owned",false);ownershipDurable=owned.load();outputMask=p.getULong64("outputs",0);p.end();
  }
  outputMask&=(uint64_t(1)<<4)|(uint64_t(1)<<15)|(uint64_t(1)<<18)|(uint64_t(1)<<26)|(uint64_t(1)<<32);
  if(owned)lease.fail(); // reboot cannot restore an old authorization
}
void attach(AsyncWebServer& server,AsyncWebSocket& ws) {
  socket=&ws;
  server.on("/api/competition/stop",HTTP_POST,[](AsyncWebServerRequest* r){
    if(!sameOrigin(r)){errorResponse(r,403,"origin_rejected");return;}
    if(owned)localStop=true;
    r->send(202,"application/json","{\"ok\":true}");
  });
  server.on("/api/competition/state",HTTP_GET,[](AsyncWebServerRequest* r){
    auto* response=r->beginResponse(200,"application/json",snapshot.get());response->addHeader("Cache-Control","no-store");r->send(response);
  });
}
void begin() {
  initialized=settingsOk && mutationMutex && commandQueue;
  if(isPaired) {
    WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);esp_wifi_set_ps(WIFI_PS_NONE);
    initialized=initialized && radio.begin(settings.pmk) && radio.add(settings.teams[0],settings.channel);
  }
  if(!initialized || owned) {owned=true;lease.fail();stopOutputs();}
  processor.setProgramServiceHook(service);publish();
}
void step(bool serialMode) {
  service();
  if(!initialized)return;
  if(owned && permitted() && lease.mode==Mode::Auto &&
     (runGeneration!=lease.generation || runMatch!=lease.match || runController!=lease.controllerBoot)) {
    runGeneration=lease.generation;runMatch=lease.match;runController=lease.controllerBoot;
    outputsOn=true;programRunning=true;processor.processCommands(program,Comm::CH_SERIAL);
  }
  processor.applyPendingProgramIfAny();
  if(releasePending) {
    // Only here: never invalidate program vectors from its own yield callback.
    releasePending=false;
    if(persistOwned(false)){owned=false;programRunning=false;viewState=State::Idle;}
  }
  if(permitted() && (owned || !serialMode)) {
    if(programCadence.ready(millis())){processor.executeLoopCommands();programCadence.complete(millis());}
    processor.updateAngleControl();
  }
  if(socket)socket->cleanupClients();
}
}
#endif
