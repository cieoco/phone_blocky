"""Host regression: compile actual OLED/value parser excerpts with ArduinoJson.

Run with python3 tests/check_oled_parser.py (Windows: wsl python3 ...).
Requires g++ and the PlatformIO ArduinoJson dependency. No board access.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/CommandProcessor.h').read_text(encoding='utf-8')

def section(start, end):
    return source.split(start, 1)[1].split(end, 1)[0]

declarations = '\n'.join(re.search(pattern, source, re.S).group(0) for pattern in [
    r'enum CommandType \{.*?\};',
    r'struct BlocklyValueExpression \{.*?\};',
    r'struct BlocklyCommand \{.*?\};',
])
parsers = section('  std::shared_ptr<BlocklyValueExpression> parseValueExpression(',
                  '  int getRuntimeVariable(')
parsers = 'std::shared_ptr<BlocklyValueExpression> parseValueExpression(' + parsers
oled = section('          cmd.type = CMD_OLED;',
               '        } else if (strcmp(commandStr, "plot_print") == 0)')
resources = 'static bool programUsesDownstreamI2c(' + section(
    '  static bool programUsesDownstreamI2c(', '  bool parseProgramDoc(')
# Compile the real parser statements; only hardware/Arduino dependencies are stubbed.
test = r'''
#include <ArduinoJson.h>
#include <memory>
#include <vector>
#include <string>
#include <cassert>
#include <cstdlib>
using String = std::string;
constexpr int OLED_DEFAULT_ADDRESS=60, USER_ULTRASONIC_TRIG_PIN=2,
 USER_ULTRASONIC_ECHO_PIN=33, ENCODER_READ_DEG=0, ENCODER_READ_TICKS=1, ENCODER_READ_RPM=2;
''' + declarations + r'''
int parseIntOrString(JsonVariantConst v, int fallback) {
  if (v.isNull()) return fallback;
  if (v.is<const char *>()) return std::atoi(v.as<const char *>());
  return v.as<int>();
}
''' + parsers + '\nconstexpr int I2C_SLAVE_SDA_PIN=21, I2C_SLAVE_SCL_PIN=22;\n' + resources + '\nbool parseOled(JsonObjectConst jsonObj, BlocklyCommand &cmd) {\n' + r'''
const char *commandStr = jsonObj["command"] | "";
cmd.type = CMD_OLED;
''' + oled + r'''
return true;
}
int main() {
  for (const char *kind : {"tcs34725_lux","tcs34725_cct","tcs34725_color","tcs34725_is_color"}) {
    DynamicJsonDocument doc(512); doc["command"]=kind; doc["color"]=4;
    assert(parseValueExpression(doc.as<JsonVariantConst>()));
    assert(programUsesDownstreamI2c(doc.as<JsonVariantConst>()));
  }
  for (int color : {-1,5,21}) {
    DynamicJsonDocument doc(512); doc["command"]="tcs34725_is_color"; doc["color"]=color;
    assert(!parseValueExpression(doc.as<JsonVariantConst>()));
  }

  for (const char *channel : {"r","g","b","c"}) {
    DynamicJsonDocument doc(512);
    doc["command"]="tcs34725_read"; doc["channel"]=channel;
    auto expr = parseValueExpression(doc.as<JsonVariantConst>());
    assert(expr && expr->auxPin >= 0 && expr->auxPin <= 3);
    assert(programUsesDownstreamI2c(doc.as<JsonVariantConst>()));
    assert(!downstreamResourceConflict(doc.as<JsonVariantConst>()));
  }
  for (const char *channel : {"", "x", "red", "R"}) {
    DynamicJsonDocument doc(512);
    doc["command"]="tcs34725_read"; doc["channel"]=channel;
    assert(!parseValueExpression(doc.as<JsonVariantConst>()));
  }
  DynamicJsonDocument nested(2048);
  assert(!deserializeJson(nested, R"({"procedures":[{"body":[{"command":"oled_value","value":{"command":"tcs34725_read","channel":"c"}}]}]})"));
  assert(programUsesDownstreamI2c(nested.as<JsonVariantConst>()));
  assert(!downstreamResourceConflict(nested.as<JsonVariantConst>()));
  nested["functions"].to<JsonArray>().add().to<JsonObject>()["idx"]=0;
  assert(downstreamResourceConflict(nested.as<JsonVariantConst>()));
  const char *values[] = {
    "0", "123", "-45",
    R"({"command":"legoButton","pin":"4"})",
    R"({"command":"arduinoUltrasonic","trigPin":"2","echoPin":"33"})",
    R"({"command":"variable_get","variableName":"x"})",
    R"({"command":"encoderRead","motor":3,"read":"ticks"})",
    R"({"command":"tcs34725_read","channel":"r"})",
    R"({"command":"tcs34725_read","channel":"g"})",
    R"({"command":"tcs34725_read","channel":"b"})",
    R"({"command":"tcs34725_read","channel":"c"})",
    R"({"command":"math_arithmetic","operator":"ADD","left":1,"right":2})"
  };
  for (int address : {60,61}) for (int row : {0,3}) {
    for (auto value : values) {
      DynamicJsonDocument doc(2048), input(1024);
      assert(!deserializeJson(input, value));
      doc["command"]="oled_value"; doc["address"]=address; doc["row"]=row;
      doc["value"].set(input.as<JsonVariantConst>());
      BlocklyCommand cmd;
      assert(parseOled(doc.as<JsonObjectConst>(),cmd));
      assert(cmd.oledAddress == address && cmd.angle == row && cmd.valueExpr);
      if (cmd.valueExpr->kind == "legoButton") assert(cmd.pin == 4);
      if (cmd.valueExpr->kind == "ultrasonic") assert(cmd.pin == 2 && cmd.value == 33);
    }
    for (auto kind : {"oled_text","oled_clear"}) {
      DynamicJsonDocument doc(512);
      doc["command"]=kind; doc["address"]=address; doc["row"]=row; doc["text"]="OK";
      BlocklyCommand cmd;
      assert(parseOled(doc.as<JsonObjectConst>(),cmd));
      assert(cmd.oledAddress == address && cmd.angle == row);
    }
  }
}
'''
# Also ensure the execution path consumes the independent address, not sensor GPIO.
assert 'oledDisplay.write(cmd.oledAddress,' in source
with tempfile.TemporaryDirectory(prefix='oled-parser-') as folder:
    cpp = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test'
    cpp.write_text(test, encoding='utf-8')
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / '.pio/libdeps/esp32dev/ArduinoJson/src'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('OLED/TCS firmware parser: value sources, channels, nested I2C conflicts and addresses: PASS')
