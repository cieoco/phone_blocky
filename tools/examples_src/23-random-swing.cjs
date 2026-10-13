'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '23-random-swing', name: '隨機擺頭',
    desc: '舵機每 0.8 秒轉到 30–150° 之間隨機的角度，看起來像在東張西望。',
    hw: 'S1（GPIO 5）', level: '基礎', series: 'D'},
  setup: [d.servo(1, 90)],
  loop: [
    d.note('隨機整數：兩端都包含。範圍改 0–180 看看；不要超出 0–180。',
      d.servo(1, d.randomInt(30, 150)), {h: 90}),
    d.delay(800)
  ]
};
