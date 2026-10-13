'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '11-ultrasonic-stop', name: '超音波避障',
    desc: '前方小於 20 cm 就停車，否則以 40% 動力前進，同時把距離畫到 Plotter。',
    hw: 'M1 + 超音波 TRIG 2 / ECHO 33', level: '基礎', series: 'C'},
  setup: [d.stop(0)],
  loop: [
    d.note('距離會畫到 Plotter（抽屜裡的「開啟獨立 Plotter」）。單位欄位只是圖上的標示。',
      d.plot('距離', 'cm', d.ultrasonic(2, 33)), {h: 100}),
    d.note('條件每一圈重新判斷。20 可以改大改小；延遲 100 毫秒是量測節拍，不要設成 0。',
      d.ifElse(d.lt(d.ultrasonic(2, 33), 20), [d.stop(1)], [d.pwm(1, 'FORWARD', 40)]),
      {w: 270, h: 110}),
    d.delay(100)
  ]
};
