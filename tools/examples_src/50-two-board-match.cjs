'use strict';
const d = require('./_dsl.cjs');
const event = (name, body) => d.block(name, {}, {}, {DO: body});
const stop = () => d.callProc('停止夾爪');
module.exports = {
  meta: {id:'50-two-board-match', name:'二板任務：自主前進、手機接手夾爪',
    desc:'存到 Blockly 板後，在主控手機面板按「開始任務」。車以三成出力前進半秒後等候交接。遙控時，將 M1 滑桿往正值推動夾爪（上限 30%）；碰到限位就停止。',
    hw:'二板麥輪套組，M1 夾爪，GPIO33 常開限位開關接 GND', level:'進階', series:'F'},
  phases: [
    event('match_auto', [d.block('match_drive',{f:300,l:0,w:0}), d.delay(500), stop()]),
    event('match_manual', [
      d.funcDeclare(0,'ANALOG',true),
      d.pinMode(33,'INPUT_PULLUP'),
      d.ifElse(d.and(d.gt(d.funcGet(0),0), d.neq(d.digitalRead(33),0)),
        [d.pwm(1,'FORWARD',d.constrain(d.funcGet(0),0,30))], [stop()]),
      d.delay(20)
    ])
  ],
  extra: [d.defProc('停止夾爪',[d.stop(1)])]
};
