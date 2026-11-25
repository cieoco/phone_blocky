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
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
uint32_t clockMs;
uint32_t millis() { return clockMs; }
namespace CompetitionGate { bool allowed() { return true; } }
enum Op { CMD_DELAY, CMD_REPEAT, CMD_WHILE, CMD_MATCH_DRIVE, Motor, Servo };
struct BlocklyCommand {
  Op type;
  int delay_ms = 0;
  int value = 0;
  char direction = 0;
  int16_t axes[3]{};
  std::vector<BlocklyCommand> nested_commands_then;
};
struct Proc { std::string name; std::vector<BlocklyCommand> body; };
struct Functions { int slots[8]{}; void setValue(unsigned i,int x) { slots[i] = x; } };
struct Interpreter {
  KitPhaseLease lease;
  bool phased=true, phaseCancelled=false, autoPending=false, pendingProgram=false;
  int16_t axes[3]{}, phasePage[8]{};
  int motor=0, servo=90, starts=0;
  std::vector<int> runtimeVariables;
  std::vector<Proc> procedures;
  std::vector<BlocklyCommand> loopCommands;
  Functions functions; Functions *externalFunctions_ = &functions;
  std::function<void()> tick;
  void controlTickDuringProgram() {}
  void programYield(int ms) { clockMs += ms; if(tick) tick(); servicePhase(); }
  void stopAllRuntime() { motor=0; }
  int numericArg(const BlocklyCommand&, int v) { return v; }
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
    '      case CMD_MATCH_DRIVE:', '      case CMD_FUNC_SET:') + r'''
      case Motor: motor = 25; ++starts; break;
      case Servo: servo = 110; break;
    }
  }
  void beat(uint16_t w) { acceptPhaseValue(15,int16_t(w),clockMs); }
};
int main() {
  for(auto block : {CMD_DELAY,CMD_REPEAT,CMD_WHILE}) {
    for(bool disconnect : {false,true}) {
      clockMs=1000;
      Interpreter p;
      BlocklyCommand drive{CMD_MATCH_DRIVE}; drive.axes[0]=300;
      BlocklyCommand wait{block}; wait.value=5000; wait.delay_ms=5000;
      p.procedures={{"__kit_match_auto",{{Motor},{Servo},drive,wait,{Motor}}},
                    {"__kit_match_manual",{{Motor},drive}}};
      p.beat(1); p.executeLoopCommands(); assert(!p.motor);
      p.beat(0x4001); assert(p.hostPhaseOwnsOutputs());
      p.functions.slots[0]=1;
      p.tick=[&]() {
        assert(p.axes[0]==300 && p.motor==25);
        if(!disconnect && clockMs>=1100) p.beat(0x8001);
      };
      p.executeLoopCommands();
      assert(clockMs==(disconnect?1300u:1100u));
      int16_t page[8]; assert(p.copyPhasePage(page));
      assert(page[0]==0 && page[4]==0x4d31 && p.starts==1);
      assert(!p.motor && p.servo==110 && !p.functions.slots[0]);
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
  Interpreter p;
  p.procedures={{"__kit_match_auto",{{Motor},{Servo}}}};
  p.beat(7); p.beat(0x4007); p.executeLoopCommands();
  p.beat(0x4007); p.executeLoopCommands();
  assert(p.starts==1 && !p.motor && p.servo==110 && p.lease.phase()==1);
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / 'phase.cpp'
    path.write_text(code, encoding='utf8')
    out = Path(temp) / 'phase'
    subprocess.run(['g++','-std=c++17','-fsanitize=undefined','-I'+str(root/'src'),str(path),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('PASS: firmware auto-once, early finish, manual accessories, STOP/timeout during delay, repeat and while')
