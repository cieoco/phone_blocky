'use strict';
const d = require('./_dsl.cjs');
const P = ['左輪', '右輪'];
const drive = d.defProc('開車', [
  d.pwm(1, 'FORWARD', d.getVar('左輪')), d.pwm(2, 'FORWARD', d.getVar('右輪'))], P);
const go = (l, r) => d.callProc('開車', P, [l, r]);
const dodge = d.defProc('閃避', [go(-40, -40), d.delay(500), go(50, -50), d.delay(400), go(0, 0)]);
module.exports = {
  meta: {id: '56-car-avoid', name: '小車：自動閃避',
    desc: '直走；前方 20 cm 內有東西就呼叫「閃避」（後退再右轉）。副程式加上超音波感測。',
    hw: '兩輪小車：左輪 M1、右輪 M2（無編碼器）+ 超音波 TRIG 2 / ECHO 33', level: '進階', series: 'G'},
  setup: [d.stop(0)],
  loop: [
    d.note('新的：副程式配感測器。loop 一直重複，每圈先量距離再決定。超音波沒回波會讀到 −1，也會當成「有障礙」。',
      d.ifElse(d.lt(d.ultrasonic(2, 33), 20), [d.callProc('閃避')], [go(50, 50)]), {w: 290, h: 130}),
    d.delay(50)
  ],
  extra: [
    d.note('閃避的內容整包放這，要改後退多久、轉多大角度只改這裡。', dodge, {w: 270, h: 100}),
    drive
  ]
};
