'use strict';
const d = require('./_dsl.cjs');
const forward = d.defProc('前進', [d.pwm(1, 'FORWARD', 50), d.pwm(2, 'FORWARD', 50)]);
const left = d.defProc('左轉', [d.stop(1), d.pwm(2, 'FORWARD', 50)]);
const right = d.defProc('右轉', [d.pwm(1, 'FORWARD', 50), d.stop(2)]);
const halt = d.defProc('停車', [d.stop(0)]);
module.exports = {
  meta: {id: '52-car-actions', name: '小車：動作副程式',
    desc: '同一條路線，改成呼叫「前進、左轉、停車」副程式，主程式像一句話。副程式沒有參數。',
    hw: '兩輪小車：左輪 M1、右輪 M2（無編碼器）', level: '入門', series: 'G'},
  setup: [
    d.stop(0),
    d.note('新的：副程式。主程式現在讀起來就是「前進、等、左轉、等、前進、等、停車」。',
      d.callProc('前進'), {w: 280, h: 100}),
    d.delay(1000),
    d.callProc('左轉'),
    d.delay(400),
    d.callProc('前進'),
    d.delay(1000),
    d.callProc('停車')
  ],
  loop: [],
  extra: [
    d.note('哪一顆輪子裝反了，只要改這裡（例如把 FORWARD 改 BACKWARD），所有呼叫的地方一起修好。「右轉」這支沒用到，先備著。',
      forward, {w: 300, h: 130}),
    left, right, halt
  ]
};
