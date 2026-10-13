'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '03-motor-reverse', name: '正轉反轉三次',
    desc: '用「重複」積木讓馬達正轉 1 秒、反轉 1 秒，連做 3 次後停下來，只執行一遍。',
    hw: 'M1', level: '入門', series: 'A'},
  setup: [
    d.stop(0),
    d.note('「重複」裡面的動作會做指定次數，做完才往下走。放在 setup 所以只跑一遍；把 3 改成別的數字試試。',
      d.repeat(3, [
        d.pwm(1, 'FORWARD', 50),
        d.delay(1000),
        d.pwm(1, 'BACKWARD', 50),
        d.delay(1000)
      ]), {w: 280, h: 110}),
    d.note('做完一定要停；延遲不要設 0，否則正反轉來不及反應。', d.stop(1), {w: 240, h: 80})
  ],
  loop: []
};
