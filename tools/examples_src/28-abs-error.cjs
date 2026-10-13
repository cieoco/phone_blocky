'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '28-abs-error', name: '離目標多遠',
    desc: '畫出「超音波距離離 30 cm 差多少」。用絕對值，太近太遠都是正數。',
    hw: '超音波 TRIG 2 / ECHO 33', level: '基礎', series: 'D'},
  setup: [],
  loop: [
    d.note('距離 − 30 可能是負的；絕對值把負號拿掉，只看差多少。到 Plotter 看曲線。',
      d.plot('誤差', 'cm', d.abs(d.sub(d.ultrasonic(2, 33), 30))), {w: 270, h: 100}),
    d.delay(100)
  ]
};
