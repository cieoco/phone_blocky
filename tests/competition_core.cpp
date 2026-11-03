#include "../src/competition/Core.h"
#include "../src/competition/Protocol.h"
#include "../src/competition/InputPolicy.h"
#include "../src/competition/StationPolicy.h"
#include <cassert>
#include <iostream>
using namespace Competition;
int main() {
  char apName[33];refereeAPName(apName,sizeof(apName),"A1B2","192.168.1.50");
  assert(!strcmp(apName,"A1B2-192.168.1.50"));
  refereeAPName(apName,sizeof(apName),"FFFF","255.255.255.255");assert(strlen(apName)<=32);
  refereeAPName(apName,sizeof(apName),"A1B2","");assert(!strcmp(apName,"A1B2-STA-offline"));
  StationTicket network;network.epoch=7;network.channel=6;
  assert(network.matches(true,6,7));
  assert(!network.matches(false,6,7));
  assert(!network.matches(true,11,7));
  assert(!network.matches(true,6,8)); // a short disconnect/reconnect cannot resume
  assert(!StationTicket::usable(true,12));
  assert(inputKind("","read",false)==InputKind::Read);
  assert(inputKind("joy","read",false)==InputKind::Motion);
  assert(inputKind("PROG","read",false)==InputKind::Denied);
  assert(inputKind("hardwareTest","pwm",false)==InputKind::Denied);
  assert(inputKind("","read",true)==InputKind::Denied);
  assert(inputKind("","apply_config",false)==InputKind::Denied);
  for(const char* command:{"pwm","speed","move_to","move_by","servo","stop"})assert(inputKind("",command,false)==InputKind::Motion);
  for(Mode matchMode:{Mode::Manual,Mode::Auto,Mode::Mixed})for(Mode first:{Mode::Manual,Mode::Auto}) {
    MatchEngine m;Config config;config.mode=matchMode;config.first=first;config.manualMs=1000;config.autoMs=2000;config.gapMs=1000;
    assert(m.prepare(config,90,0));m.ready();m.begin(0);
    m.tick(700,true,true);m.tick(1200,true,true);assert(m.mode()==(matchMode==Mode::Mixed?first:matchMode));
    const Time end=m.end;m.tick(end,true,true);
    if(matchMode==Mode::Mixed){assert(m.state==State::Gap);m.tick(m.start-500,true,true);m.tick(m.start,true,true);assert(m.mode()!=first);m.tick(m.end,true,true);}
    assert(m.state==State::Finished);
  }
  Config c; c.mode=Mode::Mixed;
  MatchEngine e; assert(e.prepare(c,1,0)); assert(e.ready()); assert(e.begin(100));
  e.tick(800,true,true); assert(e.guardPassed); e.tick(1300,true,true); assert(e.state==State::Manual);
  e.tick(31300,true,true); assert(e.state==State::Gap && e.start==36300);
  assert(e.pause(33300)); assert(e.remaining(99999)==3000);
  assert(e.resume(40000)); assert(e.start==44200);
  e.tick(43700,true,true); e.tick(44200,true,true); assert(e.state==State::Auto);
  assert(e.pause(45000)); auto left=e.remaining(45000); assert(left==29200);
  assert(e.resume(50000)); e.tick(50700,true,true); e.tick(51200,true,true); assert(e.end==80400);
  e.tick(80400,true,true); assert(e.state==State::Finished && e.remaining(80400)==0);
  assert(e.reset()); c.first=Mode::Auto; assert(e.prepare(c,2,0)); e.ready(); e.begin(0);
  e.tick(700,true,false); assert(e.state==State::Fault); assert(!e.resume(900));
  e.reset(); e.prepare(c,3,0); e.tick(6000,false,false); assert(e.state==State::Fault);
  Lease l; assert(l.prepare(10,20,1,Mode::Manual,1)); assert(l.commit(1,2,1200,31200,0));
  assert(!l.allowed(1199)); assert(!l.renew(1,3,500,801,501)); // oversized lease
  assert(l.renew(1,4,950,1250,950)); assert(!l.allowed(1199)); assert(l.allowed(1200));
  assert(!l.renew(1,4,950,1250,1000)); // replay cannot extend
  l.tick(1250); assert(l.faulted); assert(!l.renew(1,5,1251,1551,1251));
  assert(!l.prepare(10,20,2,Mode::Auto,6)); // fault is latched for this match
  assert(l.prepare(10,21,3,Mode::Auto,7)); assert(l.commit(3,8,2000,3000,1500));
  assert(l.renew(3,9,1900,2200,1900)); assert(l.renew(3,10,2150,2450,2150));
  assert(l.renew(3,11,2400,2700,2400)); assert(l.renew(3,12,2650,2950,2650));
  assert(l.renew(3,13,2900,3000,2900)); l.tick(3000); assert(!l.faulted && !l.allowed(3000));
  assert(l.disable(4,14,true)); assert(!l.prepare(10,21,5,Mode::Manual,15));
  Packet p; p.type=Message::Commit; p.length=8; p.sequence=12; p.controllerBoot=UINT64_MAX-1; p.match=123;
  p.generation=42; p.effective=1234; put(p.payload,99999,8);
  uint8_t data[MaxFrame]; size_t len=encode(p,data); Packet q; assert(len==72 && decode(data,len,q));
  data[2]=1;assert(!decode(data,len,q));data[2]=2; // mixed firmware versions fail closed
  assert(q.controllerBoot==p.controllerBoot && q.generation==42 && get(q.payload,8)==99999);
  for(size_t i=0;i<len;++i) assert(!decode(data,i,q));
  data[6]=1; assert(!decode(data,len,q)); data[6]=0; data[3]=255; assert(!decode(data,len,q));
  for(Message type:{Message::View,Message::Release,Message::Status}) {
    p.type=type;p.length=payloadSize(type);len=encode(p,data);assert(decode(data,len,q));assert(q.type==type);
    assert(!decode(data,len-1,q));
  }
  // Fault injection across a 30-second stage: no packet can resurrect an expired lease.
  for(unsigned dropAt=1000;dropAt<30000;dropAt+=113) {
    Lease x;assert(x.prepare(1,1,1,Mode::Auto,1));assert(x.commit(1,2,1000,31000,0));
    uint32_t seq=2;Time lastUntil=0;
    for(Time t=700;t<=31500;t+=10) {
      if(t<dropAt && t%80==60){lastUntil=std::min(t+LeaseMs,Time(31000));assert(x.renew(1,++seq,t,lastUntil,t));}
      x.tick(t);
      if(t>=lastUntil)assert(!x.allowed(t));
      if(x.faulted)assert(!x.renew(1,++seq,t,t+LeaseMs,t));
    }
  }
  // Run the production timeline and lease logic together, including the automatic GAP handover.
  for(bool loseSecondCommit:{false,true}) {
    MatchEngine m;Config cfg;cfg.mode=Mode::Mixed;cfg.manualMs=1000;cfg.autoMs=1000;cfg.gapMs=1000;
    m.prepare(cfg,500,0);m.ready();m.begin(0);
    Lease clients[2];uint32_t seq=0,previousGeneration=0;Time lastRenew=0;bool everMoved=false;
    for(Time t=0;t<5000;++t) {
      if(m.generation!=previousGeneration) {
        previousGeneration=m.generation;
        for(auto& client:clients) {
          if(m.terminal())client.disable(m.generation,++seq,true);
          else {assert(client.prepare(99,m.match,m.generation,m.mode(),++seq));}
        }
        if(m.scheduling())for(int i=0;i<2;++i)if(!(loseSecondCommit && i==1))assert(clients[i].commit(m.generation,++seq,m.start,m.end,t));
      }
      m.tick(t,true,clients[0].committed&&clients[1].committed);
      if((m.running() || (m.scheduling()&&m.guardPassed)) && t-lastRenew>=80) {
        lastRenew=t;for(auto& client:clients)assert(client.renew(m.generation,++seq,t,std::min(t+LeaseMs,m.end),t));
      }
      for(auto& client:clients){client.tick(t);everMoved|=client.allowed(t);assert(!client.faulted);}
      if(m.terminal())break;
    }
    assert(loseSecondCommit ? m.state==State::Fault&&!everMoved : m.state==State::Finished&&everMoved);
  }
  std::cout << "competition core: timeline, pause, fault, lease, replay, terminal and wire validation PASS\n";
}
