'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '02-servo-angle', name: '舵機轉到 90°',
    desc: '舵機在 0° 與 90° 之間來回。角度只能 0–180°，舵機會自己保持住位置。',
    hw: 'S1（GPIO 5）', level: '入門', series: 'A'},
  setup: [
    d.note('開機先回到 0°，每次執行都從同一個起點開始。舵機接 S1（GPIO 5）。',
      d.servo(1, 0), {w: 250, h: 90})
  ],
  loop: [
    d.note('舵機角度只能 0–180°，而且會自己保持住位置（不需要編碼器，也沒有「設零點」這回事）。改成 45 或 180 看看。',
      d.servo(1, 90), {w: 270, h: 110}),
    d.delay(2000),
    d.servo(1, 0),
    d.delay(2000)
  ]
};
