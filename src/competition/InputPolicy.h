#pragma once
#include <string.h>
#include <initializer_list>
namespace Competition {
enum class InputKind { Denied, Read, Motion };
// Match CommandProcessor dispatch precedence. A read-looking cmd must never
// bypass the gate when mode selects PROG/hardwareTest or legacy translation.
inline InputKind inputKind(const char* mode,const char* cmd,bool legacy) {
  if(legacy)return InputKind::Denied;
  if(!strcmp(mode,"joy"))return InputKind::Motion;
  if(*mode)return InputKind::Denied;
  if(!strcmp(cmd,"capabilities") || !strcmp(cmd,"ping") || !strcmp(cmd,"read") || !strcmp(cmd,"read_config"))return InputKind::Read;
  for(const char* c:{"pwm","speed","move_to","move_by","servo","stop"})if(!strcmp(cmd,c))return InputKind::Motion;
  return InputKind::Denied;
}
}
