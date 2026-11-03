#include "CommandProcessor.h"
#include "ProgramPayload.h"
#include "competition/Runtime.h"
#include "competition/WebSupport.h"
using namespace Competition;

namespace {
Settings settings; Radio radio; Lease lease;
MotorController motors; ServoController servos(SERVO1_PIN,SERVO2_PIN);
EncoderHandler encoders; PWMManager pwm;
CommandProcessor processor(motors,servos,encoders,pwm);
ControlTiming::LoopCadence programCadence;
AsyncWebServer server(80); AsyncWebSocket drive("/drive"), blockly("/ws");
Snapshot snapshot; Jobs jobs;
SemaphoreHandle_t programMutex;
String program, programResponse, serialLine;
uint32_t programHash=0, txSeq=0, rxSeq=0, runGeneration=0;
uint64_t runMatch=0,runController=0;
uint64_t bootId=0, controllerBoot=0, previousControllerBoot=0;
Time syncedAt=0, statusAt=0, snapshotAt=0, lastOwnerSeen=0, manualDeadline=0;
int64_t offsetMs=0; uint32_t uncertainty=1000;
bool initialized=false, paired=false, synced=false, outputsOn=false, programRunning=false;
std::atomic<bool> editable{true}, localStop{false};
std::atomic<bool> printAck{false};
std::atomic<uint32_t> disconnected{0};
uint32_t owner=0; uint64_t outputMask=0;
struct Context {uint32_t token=0,generation=0;Time until=0;};
Context contexts[3];unsigned contextIndex=0;uint32_t manualSeq=0;
Packet pendingPong; bool havePong=false;
Time controllerNow() {int64_t n=int64_t(nowMs())+offsetMs;return n>0?Time(n):0;}
bool clockHealthy() {return synced && uncertainty<=10 && nowMs()-syncedAt<SyncMaxAgeMs;}
bool permitted() {
  Time t=controllerNow();
  if(!initialized || localStop.load() || !clockHealthy() || !lease.allowed(t+uncertainty) || t<lease.start+uncertainty)return false;
  return lease.mode==Mode::Auto || (owner && nowMs()<manualDeadline);
}
void clearContexts(){for(auto& c:contexts)c=Context();manualDeadline=0;manualSeq=0;}
void stopOutputs() {
  outputsOn=false; motors.stopAll(); processor.i2cStopAll(); // queues empty program; competition reset does not center servos
  servos.disableOutputs();pwm.disableOutputs();
  for(int pin=0;pin<40;++pin)if(outputMask&(uint64_t(1)<<pin)){pinMode(pin,OUTPUT);digitalWrite(pin,LOW);}
  manualDeadline=0;
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
  if(xSemaphoreTake(programMutex,0)!=pdTRUE)return false;
  String candidate=ProgramStore::loadJson();bool ok=validateProgram(candidate);
  if(ok){program=candidate;programHash=hashProgram(program);}
  xSemaphoreGive(programMutex);return ok;
}
void loadMotorSettings() {
  Preferences p;if(!p.begin("motor",true))return;
  processor.setPIDGains(p.getFloat("kp",DEFAULT_PID_KP),p.getFloat("ki",DEFAULT_PID_KI),p.getFloat("kd",DEFAULT_PID_KD));
  processor.setSpeedLimits(p.getInt("minSpeed",DEFAULT_MIN_SPEED),p.getInt("maxSpeed",DEFAULT_MAX_SPEED));
  processor.setSpeedFeedForward(p.getBool("speedFFEn",DEFAULT_SPEED_FF_ENABLED),p.getFloat("speedFFkS",DEFAULT_SPEED_FF_KS),p.getFloat("speedFFkV",DEFAULT_SPEED_FF_KV),p.getFloat("speedFFkA",DEFAULT_SPEED_FF_KA));
  processor.setMotorHardwareParams(p.getFloat("gearRatio",GEAR_RATIO),p.getFloat("encoderPPR",ENCODER_PPR),p.getInt("encoderPos",DEFAULT_ENCODER_POS));
  processor.setPosCtrlMode(p.getInt("posCtrlMode",DEFAULT_POS_CTRL_MODE));
  processor.setPositionPIDParams(p.getFloat("posKp",DEFAULT_POS_KP),p.getFloat("posKi",DEFAULT_POS_KI),p.getFloat("posKd",DEFAULT_POS_KD),p.getInt("posMaxDuty",DEFAULT_POS_MAX_DUTY),p.getFloat("posTolDeg",DEFAULT_POS_TOLERANCE_DEG));
  processor.setHoldParams(p.getFloat("holdSettle",DEFAULT_HOLD_SETTLE_DEG),p.getFloat("holdKi",DEFAULT_HOLD_KI),p.getInt("holdMax",DEFAULT_HOLD_MAX_DUTY));p.end();
}
void refreshStoredResponse() {
  bool has=ProgramStore::exists();programResponse=ProgramPayload::buildGetResponse(has,has?ProgramStore::loadJson():"",has?ProgramStore::loadXml():"",has?ProgramStore::loadMeta():"{}",false);
}
void publish() {
  StaticJsonDocument<1024> d;d["role"]="competitor";d["ready"]=initialized;d["paired"]=paired;
  d["mac"]=WiFi.macAddress();d["channel"]=settings.channel;d["boot_id"]=hexId(bootId);
  d["match_id"]=hexId(lease.match);d["generation"]=lease.generation;d["mode"]=modeName(lease.mode);
  d["locked"]=!outputsOn;d["fault"]=lease.faulted;d["synced"]=clockHealthy();d["editable"]=editable.load();
  d["prepared"]=lease.prepared;d["committed"]=lease.committed;d["program_hash"]=programHash;
  String s;serializeJson(d,s);snapshot.set(s);
}
void sendStatus() {
  if(!paired || !controllerBoot)return;
  Packet p;p.controllerBoot=controllerBoot;p.deviceBoot=bootId;p.match=lease.match;p.generation=lease.generation;
  p.sequence=txSeq+1;p.sent=nowMs();
  if(!p.sequence){lease.fail();return;}
  if(havePong){p=pendingPong;p.sequence=txSeq+1;p.sent=nowMs();put(p.payload+16,p.sent,8);}
  else {
    p.type=Message::Status;p.payload[0]=lease.prepared;p.payload[1]=lease.committed;p.payload[2]=outputsOn;p.payload[3]=lease.faulted;
    put(p.payload+4,rxSeq,4);put(p.payload+8,programHash,4);p.payload[12]=uint8_t(lease.mode);p.payload[13]=!outputsOn;p.payload[14]=clockHealthy();
  }
  p.length=payloadSize(p.type);
  if(radio.send(settings.teams[0].mac,p)){++txSeq;statusAt=nowMs();if(havePong)havePong=false;}
}
void receivePackets() {
  if(!paired)return;Received r;Packet p;
  for(int i=0;i<12&&radio.receive(r,p);++i) {
    if(memcmp(r.mac,settings.teams[0].mac,6) || !p.controllerBoot || (p.deviceBoot && p.deviceBoot!=bootId))continue;
    if(p.controllerBoot!=controllerBoot) {
      if(p.type!=Message::Ping || p.controllerBoot==previousControllerBoot)continue;
      if(controllerBoot){lease.fail();stopOutputs();}
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
      if(newGeneration){clearContexts();stopOutputs();editable=false;}
      if(!loadSnapshot(p.payload[1]!=0,newMatch)){lease.fail();lease.match=p.match;lease.generation=p.generation;continue;}
      lease.prepare(controllerBoot,p.match,p.generation,Mode(p.payload[0]),p.sequence);
    } else if(p.type==Message::Disable && p.match && !lease.committed &&
              (lease.match==0 || lease.terminal || lease.faulted)) {
      // Cancellation may precede PREPARE (or its validation may have failed).
      // Install a locked tombstone so the referee can confirm all outputs are off.
      lease.controllerBoot=controllerBoot;lease.match=p.match;
      if(lease.disable(p.generation,p.sequence,p.payload[0]!=0)){clearContexts();stopOutputs();if(p.payload[0])editable=true;}
    } else if(p.match==lease.match && p.controllerBoot==lease.controllerBoot) {
      Time now=controllerNow()+uncertainty;
      if(p.type==Message::Commit)lease.commit(p.generation,p.sequence,p.effective,get(p.payload,8),now);
      else if(p.type==Message::Renew && clockHealthy())lease.renew(p.generation,p.sequence,p.sent,p.until,now);
      else if(p.type==Message::Disable) {
        if(lease.disable(p.generation,p.sequence,p.payload[0]!=0)){clearContexts();stopOutputs();if(p.payload[0])editable=true;}
      }
    }
  }
}
void driveError(uint32_t id,const char* e) {StaticJsonDocument<128>d;d["type"]="error";d["error"]=e;String s;serializeJson(d,s);drive.text(id,s);}
void processJobs() {
  WebJob job;for(int i=0;i<4 && jobs.pop(job);++i) {
    StaticJsonDocument<2048>d;if(deserializeJson(d,job.json))continue;
    uint32_t id=d["_client"]|0;const char* type=d["type"]|"";
    if(!strcmp(type,"claim")) {
      if(String(d["key"]|"")!=settings.secret){driveError(id,"authentication_required");continue;}
      if(owner && owner!=id){driveError(id,"another_driver");continue;}
      owner=id;lastOwnerSeen=nowMs();continue;
    }
    if(!owner || id!=owner){driveError(id,"claim_control_first");continue;}
    lastOwnerSeen=nowMs();
    if(!strcmp(type,"heartbeat"))continue;
    if(!strcmp(type,"stop")){lease.fail();clearContexts();stopOutputs();continue;}
    if(strcmp(type,"drive")){driveError(id,"unknown_command");continue;}
    Context* found=nullptr;uint32_t token=d["token"]|0;
    for(auto& c:contexts)if(c.token && c.token==token && c.generation==lease.generation && nowMs()<c.until)found=&c;
    uint32_t seq=d["sequence"]|0;
    if(!found || !seq || seq<=manualSeq || lease.mode!=Mode::Manual || !lease.allowed(controllerNow()+uncertainty)) {driveError(id,"locked_or_stale");continue;}
    JsonArray values=d["motors"].as<JsonArray>();bool valid=values.size()==4;
    for(JsonVariant v:values)if(!v.is<int>() || v.as<int>() < -100 || v.as<int>() > 100)valid=false;
    JsonArray angles=d["servos"].as<JsonArray>();if(angles.size() && angles.size()!=2)valid=false;
    for(JsonVariant v:angles)if(!v.is<int>()||v.as<int>()<0||v.as<int>()>180)valid=false;
    if(!valid){driveError(id,"invalid_drive_values");continue;}
    manualSeq=seq;manualDeadline=found->until;
    if(!permitted())continue;
    outputsOn=true;
    for(int motor=1;motor<=4;++motor)motors.pwmSigned(motor,values[motor-1].as<int>());
    for(size_t s=0;s<angles.size();++s)servos.controlServo(s+1,angles[s]);
  }
}
void service() {
  if(printAck.exchange(false))processor.resetWaitingForResponse();
  receivePackets();
  if(radio.failed() || WiFi.channel()!=settings.channel || localStop.exchange(false))lease.fail();
  if(lease.committed && !clockHealthy())lease.fail();
  lease.tick(controllerNow()+uncertainty);
  uint32_t lost=disconnected.exchange(0);
  if(owner && (lost==owner || nowMs()-lastOwnerSeen>1500)) {
    if(lease.committed && lease.mode==Mode::Manual)lease.fail();
    owner=0;clearContexts();
  }
  processJobs();
  if(!permitted() && outputsOn)stopOutputs();
  if(!lease.committed && programRunning){processor.requestProgramStop();programRunning=false;}
  if(havePong || nowMs()-statusAt>=80)sendStatus();
  if(nowMs()-snapshotAt>=100) {
    snapshotAt=nowMs();publish();
    if(owner) {
      StaticJsonDocument<384>d;d["type"]="context";d["mode"]=modeName(lease.mode);
      bool manual=lease.mode==Mode::Manual && clockHealthy() && lease.allowed(controllerNow()+uncertainty) && controllerNow()>=lease.start+uncertainty;
      d["allowed"]=manual;d["fault"]=lease.faulted;d["generation"]=lease.generation;
      if(manual){Context& c=contexts[contextIndex++%3];c.token=esp_random()|1;c.generation=lease.generation;c.until=nowMs()+250;d["token"]=c.token;}
      String s;serializeJson(d,s);drive.text(owner,s);
    }
  }
}
void serialCommands() {
  while(Serial.available()) {
    char c=char(Serial.read());if(c=='\r')continue;
    if(c!='\n'){if(serialLine.length()<1024)serialLine+=c;else serialLine="";continue;}
    StaticJsonDocument<1024>d;bool parsed=!deserializeJson(d,serialLine);serialLine="";if(!parsed)continue;
    const char* cmd=d["cmd"]|"";StaticJsonDocument<512>out;out["ok"]=false;
    if(!strcmp(cmd,"competition_info")){out["ok"]=true;out["role"]="competitor";out["mac"]=WiFi.macAddress();out["channel"]=settings.channel;out["key"]=settings.secret;out["ssid"]=WiFi.softAPSSID();}
    else if(!strcmp(cmd,"competition_pair") && editable.load()) {
      Team t;String name=d["name"]|"controller";
      if(parseMac(d["mac"]|"",t.mac)&&parseKey(d["key"]|"",t.key)&&name.length()<sizeof(t.name)){name.toCharArray(t.name,sizeof(t.name));out["ok"]=settings.saveTeam(t,true);out["restart_required"]=true;}
    } else if(!strcmp(cmd,"competition_channel") && editable.load()){out["ok"]=settings.setChannel(d["channel"]|0);out["restart_required"]=true;}
    else if(!strcmp(cmd,"competition_stop")){lease.fail();stopOutputs();out["ok"]=true;}
    else if(!strcmp(cmd,"competition_outputs") && editable.load()) {
      uint64_t mask=0;bool valid=d["pins"].is<JsonArray>();
      for(JsonVariant v:d["pins"].as<JsonArray>()) {int pin=v|(-1);if(pin!=4&&pin!=15&&pin!=18&&pin!=26&&pin!=32)valid=false;else mask|=uint64_t(1)<<pin;}
      Preferences p;if(valid && p.begin("competition",false)){out["ok"]=p.putULong64("outputs",mask)==8;p.end();out["restart_required"]=true;}
    }
    serializeJson(out,Serial);Serial.println();
  }
}
}

void setup() {
  Serial.begin(115200);bootId=randomId();programMutex=xSemaphoreCreateMutex();
  CompetitionGate::checker()=permitted;
  motors.begin();servos.initServo();encoders.begin();
  if(!settings.load())return;
  Preferences p;if(p.begin("competition",true)){outputMask=p.getULong64("outputs",0);p.end();}
  outputMask &= (uint64_t(1)<<4)|(uint64_t(1)<<15)|(uint64_t(1)<<18)|(uint64_t(1)<<26)|(uint64_t(1)<<32);
  stopOutputs();loadMotorSettings();
  beginWifi("Sumo-Car-",settings);paired=settings.count==1;
  initialized=radio.begin(settings.pmk) && (!paired || radio.add(settings.teams[0],settings.channel));
  LittleFS.begin(false);refreshStoredResponse();
  processor.setProgramServiceHook(service);
  processor.setSendJsonResponseCallback([](const DynamicJsonDocument& d,uint8_t){
    if(d["ok"]==false || d.containsKey("err"))localStop=true;
    String s;serializeJson(d,s);blockly.textAll(s);
  });
  drive.onEvent([](AsyncWebSocket*,AsyncWebSocketClient* c,AwsEventType type,void* arg,uint8_t* data,size_t len){
    if(type==WS_EVT_DISCONNECT){disconnected=c->id();return;}
    if(type!=WS_EVT_DATA)return;auto* info=static_cast<AwsFrameInfo*>(arg);
    if(!info->final || info->index || info->len!=len || len>1000 || info->opcode!=WS_TEXT){c->close();return;}
    StaticJsonDocument<1536>d;if(deserializeJson(d,data,len)){c->close();return;}
    d["_client"]=c->id();String s;serializeJson(d,s);if(!jobs.push(s.c_str(),s.length()))localStop=true;
  });server.addHandler(&drive);
  blockly.onEvent([](AsyncWebSocket*,AsyncWebSocketClient* c,AwsEventType type,void* arg,uint8_t* data,size_t len){
    if(type!=WS_EVT_DATA)return;auto* info=static_cast<AwsFrameInfo*>(arg);StaticJsonDocument<256>d;
    bool small=info->final && !info->index && info->len==len && len<=256 && info->opcode==WS_TEXT;
    bool parsed=small && !deserializeJson(d,data,len);
    if(parsed && d["message_success"]==true){printAck=true;return;}
    if(parsed && !strcmp(d["cmd"]|"","capabilities")) {
      c->text("{\"type\":\"capabilities\",\"oled_ssd1306_128x32\":true,\"competition\":true}");
    } else c->text("{\"ok\":false,\"err\":\"competition_locked_save_program_then_wait_for_referee\"}");
  });server.addHandler(&blockly);
  server.on("/api/competition/state",HTTP_GET,[](AsyncWebServerRequest* r){r->send(200,"application/json",snapshot.get());});
  server.addHandler(new JsonEndpoint("/api/competition/login",128,[](AsyncWebServerRequest* r,const char*,size_t){
    if(!authenticate(r,settings.secret))return;
    auto* response=r->beginResponse(200,"application/json","{\"ok\":true}");response->addHeader("Set-Cookie",String("competition=")+settings.secret+"; Path=/; HttpOnly; SameSite=Strict");r->send(response);
  }));
  server.on("/api/program",HTTP_GET,[](AsyncWebServerRequest* r){xSemaphoreTake(programMutex,portMAX_DELAY);String s=programResponse;xSemaphoreGive(programMutex);r->send(200,"application/json",s);});
  server.on("/api/program/autorun",HTTP_GET,[](AsyncWebServerRequest* r){r->send(200,"application/json","{\"ok\":true,\"on\":false}");});
  server.addHandler(new JsonEndpoint("/api/program",PROGRAM_STORE_MAX_BYTES*2,[](AsyncWebServerRequest* r,const char* data,size_t len){
    if(!authenticate(r,settings.secret))return;
    if(!editable.load() || xSemaphoreTake(programMutex,0)!=pdTRUE){errorResponse(r,409,"match_locked");return;}
    if(!editable.load()){xSemaphoreGive(programMutex);errorResponse(r,409,"match_locked");return;}
    String body;body.concat(data,len);ProgramPayload::SaveRequest req;
    bool valid=ProgramPayload::parseSaveBody(&body[0],body.length(),req) && validateProgram(req.json);
    bool saved=valid && ProgramStore::save(req.json,req.xml,req.xmlLen,req.source) && ProgramStore::loadJson()==req.json;
    if(saved)refreshStoredResponse();xSemaphoreGive(programMutex);
    if(!saved){errorResponse(r,valid?500:400,valid?"save_failed":"invalid_or_unsafe_program");return;}
    r->send(200,"application/json","{\"ok\":true}");
  }));
  staticFiles(server,"/competition_drive.html");publish();server.begin();
}
void loop() {
  if(!initialized){delay(100);return;}
  service();serialCommands();
  if(permitted() && lease.mode==Mode::Auto &&
     (runGeneration!=lease.generation || runMatch!=lease.match || runController!=lease.controllerBoot)) {
    runGeneration=lease.generation;runMatch=lease.match;runController=lease.controllerBoot;outputsOn=true;programRunning=true;
    processor.processCommands(program,Comm::CH_SERIAL);
  }
  processor.applyPendingProgramIfAny();
  if(permitted() && lease.mode==Mode::Auto) {
    if(programCadence.ready(millis())) {
      processor.executeLoopCommands();
      programCadence.complete(millis());
    }
    // The controller itself gates all entrypoints to >=10 ms, including calls
    // made inside Blockly. Radio/lease service above still runs each outer pass.
    processor.updateAngleControl();
  }
  drive.cleanupClients();blockly.cleanupClients();delay(1);
}
