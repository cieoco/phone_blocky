'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '10-knob-servo', name: '旋鈕控制舵機',
    desc: '轉可變電阻旋鈕，舵機跟著轉。把類比讀值 0–4095 用 map 換算成角度 0–180。',
    hw: 'S1（GPIO 5）+ 旋鈕接類比輸入 GPIO 32', level: '基礎', series: 'C'},
  setup: [
    d.servo(1, 90)
  ],
  loop: [
    d.note('類比讀值是 0–4095，舵機只有 0–180°，所以用「對應」積木換算：讀值 0 → 0°、4095 → 180°。換算不會自動夾限，接線要讓讀值落在這個範圍。',
      d.servo(1, d.map(d.analogRead(32), 0, 4095, 0, 180)), {w: 300, h: 140}),
    d.note('延遲 50 毫秒是更新節拍，不要設 0。', d.delay(50), {w: 240, h: 80})
  ]
};
