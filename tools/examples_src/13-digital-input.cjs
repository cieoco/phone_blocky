'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '13-digital-input', name: '數位輸入控制舵機',
    desc: '讀數位輸入腳 GPIO 15：接高電位舵機轉 90°，否則回 0°。學會通用的數位讀取。',
    hw: 'S1（GPIO 5）+ 開關接 GPIO 15', level: '基礎', series: 'C'},
  setup: [d.servo(1, 0)],
  loop: [
    d.note('數位讀取回 0 或 1。這個腳沒有內建上拉，不接東西會亂跳：開關要一端接 3.3V、另一端接 GPIO 15，並用電阻下拉到 GND。GPIO 15 開機時不要被外部拉成錯的電位。',
      d.ifElse(d.eq(d.digitalRead(15), 1), [d.servo(1, 90)], [d.servo(1, 0)]),
      {w: 300, h: 160}),
    d.delay(50)
  ]
};
