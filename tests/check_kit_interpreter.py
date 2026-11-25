"""Exercise production phase dispatch, snapshot publication and loop cancellation on a mock clock."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'src/CommandProcessor.h').read_text(encoding='utf8')

def method(declaration):
    start = src.index('  ' + declaration)
    end = re.search(r'^  }$', src[start:], re.M)
    return src[start:start + end.end()]

def between(first, after):
    return src[src.index(first):src.index(after, src.index(first))]

code = r'''
#include "KitPhaseLease.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
''' + (root / 'src/BlocklyFunctions.h').read_text(encoding='utf8').replace('#include <Arduino.h>', '') + r'''
#define NUM_SERVOS 2
#define constrain std::clamp
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
uint32_t clockMs;
uint32_t millis() { return clockMs; }
namespace CompetitionGate { bool allowed() { return true; } }
enum Op { CMD_DELAY, CMD_REPEAT, CMD_WHILE, CMD_MATCH_DRIVE, Motor, CMD_SERVO };
struct BlocklyCommand {
  Op type;
  int delay_ms = 0;
  int value = 0;
  char direction = 0;
  int16_t axes[3]{};
  std::vector<BlocklyCommand> nested_commands_then;
  int servo_id=1, angle=110, slot=-1;
};
struct Proc { std::string name; std::vector<BlocklyCommand> body; };
struct ServoSink {
  int writes=0;
  void controlServo(int, int) { ++writes; }
};
struct Interpreter {
  KitPhaseLease lease;
  bool phased=true, phaseCancelled=false, autoPending=false, pendingProgram=false;
  int16_t axes[3]{}, phasePage[8]{};
  int motor=0, starts=0, phaseZeroMask=-1;
  int servoDeg[3]{0,90,90};
  ServoSink servoCtrl;
  bool readUnsetAccessory=false;
  std::vector<int> runtimeVariables;
  std::vector<Proc> procedures;
  std::vector<BlocklyCommand> loopCommands;
  BlocklyFunctions functions;
  BlocklyFunctions *externalFunctions_ = &functions;
  Interpreter() { for (int i=0; i<8; ++i) functions.declare(i,3); }
  std::function<void()> tick;
  void controlTickDuringProgram() {}
  void programYield(int ms) { clockMs += ms; if(tick) tick(); servicePhase(); }
  void stopAllRuntime() { motor=0; }
  struct Expression { std::string kind; int funcIdx; };
  int numericArg(const BlocklyCommand &cmd, int v) {
    if (cmd.slot < 0) return v;
    const Expression input{"func_get",cmd.slot};
    const Expression *expr=&input;
''' + between('    if (expr->kind == "func_get") {', '    if (expr->kind == "arduino_millis")') + r'''
    return v;
  }
  int readBlocklyValue(const BlocklyCommand &cmd) { return cmd.value; }
  void requestProgramStop(bool phaseOnly) {
''' + between('    if (phaseOnly) {','    if (progMutex)') + r'''
    assert(false);
  }
''' + '\n'.join(method(d) for d in [
    'void publishPhase()', 'bool hostPhaseOwnsOutputs()', 'bool copyPhasePage(',
    'bool acceptPhaseValue(', 'void cancelPhaseOutputs()', 'void servicePhase()',
    'void executeLoopCommands()', 'void executeCommandList(']) + r'''
  void executeSingleCommand(const BlocklyCommand &cmd) {
    switch(cmd.type) {
''' + between('      case CMD_DELAY:', '      case CMD_MOTOR:') + between(
    '      case CMD_REPEAT: {', '      case CMD_PRINT: {') + between(
    '      case CMD_MATCH_DRIVE:', '      case CMD_FUNC_SET:') + between(
    '      case CMD_SERVO:', '      case CMD_IF:') + r'''
      case Motor: motor = 25; ++starts; break;

    }
  }
  void configure(uint8_t mask) { acceptPhaseValue(14,0x5a00 | mask,clockMs); }
  void beat(uint16_t w) { acceptPhaseValue(15,int16_t(w),clockMs); }
};
int main() {
  for(auto block : {CMD_DELAY,CMD_REPEAT,CMD_WHILE}) {
    for(bool disconnect : {false,true}) {
      clockMs=1000;
      Interpreter p;
      BlocklyCommand drive{CMD_MATCH_DRIVE}; drive.axes[0]=300;
      BlocklyCommand wait{block}; wait.value=5000; wait.delay_ms=5000;
      p.procedures={{"__kit_match_auto",{{Motor},{CMD_SERVO},drive,wait,{Motor}}},
                    {"__kit_match_manual",{{Motor},drive}}};
      p.beat(1); p.configure(1); p.executeLoopCommands(); assert(!p.motor);
      p.beat(0x4001); assert(p.hostPhaseOwnsOutputs());
      p.functions.setValue(0,1);
      p.tick=[&]() {
        assert(p.axes[0]==300 && p.motor==25);
        if(!disconnect && clockMs>=1100) p.beat(0x8001);
      };
      p.executeLoopCommands();
      assert(clockMs==(disconnect?1300u:1100u));
      int16_t page[8]; assert(p.copyPhasePage(page));
      assert(page[0]==0 && page[4]==0x4d31 && p.starts==1);
      assert(!p.motor && p.servoDeg[1]==110 && !p.functions.valueAt(0));
      if(disconnect) {
        p.beat(0x4001); p.executeLoopCommands(); assert(p.starts==1);
      } else {
        p.beat(0xc001); p.executeLoopCommands();
        assert(p.starts==2 && p.motor && !p.axes[0]);
        assert(!p.acceptPhaseValue(0,34,clockMs));
        assert(p.acceptPhaseValue(9,1000,clockMs));
      }
    }
  }
  // A servo driven from an accessory slot runs through the production CMD_SERVO handler.
  // Check the first handoff, stored angle, explicit zero and selective reset on lease loss.
  {
    Interpreter q;
    BlocklyCommand first{CMD_SERVO}, s1{CMD_SERVO}, s2{CMD_SERVO};
    s1.slot=4; s2.slot=5; s2.servo_id=2;
    q.procedures={{"__kit_match_auto",{first}}, {"__kit_match_manual",{s1,s2}}};
    q.beat(41); q.beat(0x4029); assert(q.lease.phase()==0);
    q.configure(1u << 2); q.beat(0x4029); q.executeLoopCommands();
    q.functions.setValue(2,60); q.functions.setValue(5,127);
    assert(q.servoDeg[1]==110 && q.servoCtrl.writes==1);
    q.beat(0x8029); q.beat(0xc029); q.executeLoopCommands();
    assert(!q.functions.valueAt(2) && q.functions.valueAt(5)==127 && !q.functions.hasValue(4));
    assert(q.servoDeg[1]==110 && q.servoDeg[2]==127 && q.servoCtrl.writes==2);
    assert(!q.acceptPhaseValue(4,0,clockMs)); q.functions.setValue(4,0); q.executeLoopCommands();
    assert(q.servoDeg[1]==0 && q.servoCtrl.writes==4);
    q.functions.setValue(4,83); q.functions.setValue(2,35);
    clockMs+=300; q.servicePhase();
    assert(!q.functions.valueAt(2) && q.functions.valueAt(4)==83 && q.functions.valueAt(5)==127);
    assert(q.phaseZeroMask<0);
    q.beat(42); q.configure(1u << 5); q.beat(0x402a); q.beat(0x802a); q.beat(0xc02a);
    q.executeLoopCommands(); assert(q.servoDeg[1]==83 && q.servoDeg[2]==0);
    q.functions.setValue(5,32); q.beat(42);
    assert(q.functions.valueAt(4)==83 && !q.functions.valueAt(5));
  }
  Interpreter p;
  p.procedures={{"__kit_match_auto",{{Motor},{CMD_SERVO}}}};
  p.beat(7); p.configure(1); p.beat(0x4007); p.executeLoopCommands();
  p.beat(0x4007); p.executeLoopCommands();
  assert(p.starts==1 && !p.motor && p.servoDeg[1]==110 && p.lease.phase()==1);
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / 'phase.cpp'
    path.write_text(code, encoding='utf8')
    out = Path(temp) / 'phase'
    subprocess.run(['g++','-std=c++17','-fsanitize=undefined','-I'+str(root/'src'),str(path),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('PASS: firmware auto-once, early finish, selective reset, servo handoff and explicit zero, STOP/timeout during delay, repeat and while')
