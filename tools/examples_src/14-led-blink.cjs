'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '14-led-blink', name: 'LED 閃爍',
    desc: '把 GPIO 2 設成輸出，每 0.5 秒亮暗一次。學會設定腳位模式與數位寫入。',
    hw: 'LED（GPIO 2，Wemos D1 R32 板載 LED）', level: '入門', series: 'C'},
  setup: [
    d.note('先設成輸出（OUTPUT）才能控制電壓。GPIO 2 也是超音波 TRIG 的預設腳，兩個不要同時接。',
      d.pinMode(2, 'OUTPUT'), {w: 270, h: 100})
  ],
  loop: [
    d.note('HIGH = 3.3V 亮、LOW = 0V 暗。外接 LED 一定要串電阻（約 220 Ω）。',
      d.digitalWrite(2, 'HIGH'), {h: 90}),
    d.delay(500),
    d.digitalWrite(2, 'LOW'),
    d.delay(500)
  ]
};
