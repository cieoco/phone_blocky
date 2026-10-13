'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '27-servo-limit', name: '限制舵機範圍',
    desc: '旋鈕轉到底，舵機也只會在 30–150° 之間動，不會撞到機構。用「限制」積木夾住數值。',
    hw: 'S1 + 旋鈕接類比輸入 GPIO 32', level: '基礎', series: 'D'},
  setup: [d.servo(1, 90)],
  loop: [
    d.note('限制：小於 30 就當 30，大於 150 就當 150。機構轉到某角度會卡住時很好用。',
      d.servo(1, d.constrain(d.map(d.analogRead(32), 0, 4095, 0, 180), 30, 150)), {w: 280, h: 100}),
    d.delay(50)
  ]
};
