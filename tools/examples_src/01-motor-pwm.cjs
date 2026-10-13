'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '01-motor-pwm', name: '轉一下、停一下',
    desc: '最小可跑程式：給馬達動力、等一秒、停下來。改動力與方向就能看出 duty 的正負。',
    hw: 'M1', level: '入門', series: 'A'},
  setup: [
    d.note('開機先確保馬達是停的，避免上一支程式還留著動力。', d.stop(0), {w: 240, h: 80})
  ],
  loop: [
    d.note('動力是 0–100%。方向改成 Backward 就反轉；loop 會一直重複這四個動作。',
      d.pwm(1, 'FORWARD', 50), {h: 100}),
    d.delay(1000),
    d.stop(1),
    d.delay(1000)
  ]
};
