'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '13-digital-input', name: '數位輸入控制舵機',
    desc: 'GPIO 15 設成內建上拉，開關按下（接 GND）讀到 0 就讓舵機轉 90°，放開回 0°。',
    hw: 'S1（GPIO 5）+ 開關（GPIO 15 ↔ GND）', level: '基礎', series: 'C'},
  setup: [
    d.note('INPUT_PULLUP = 腳位內建上拉：沒按讀到 1，按下（接到 GND）讀到 0，不用外接電阻。',
      d.pinMode(15, 'INPUT_PULLUP'), {w: 290, h: 100}),
    d.servo(1, 0)
  ],
  loop: [
    d.note('按下是 0，所以條件寫「= 0」。GPIO 15 開機時不要按住開關。',
      d.ifElse(d.eq(d.digitalRead(15), 0), [d.servo(1, 90)], [d.servo(1, 0)]),
      {w: 270, h: 90}),
    d.delay(50)
  ]
};
