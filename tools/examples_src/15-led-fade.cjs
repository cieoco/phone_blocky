'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '15-led-fade', name: 'LED 亮度',
    desc: '用類比寫入（PWM）讓 GPIO 26 的 LED 一階一階變亮，值 0–255。',
    hw: 'LED + 電阻（GPIO 26）', level: '基礎', series: 'C'},
  setup: [d.analogWrite(26, 0)],
  loop: [
    d.note('類比寫入是 0–255 的亮度，不是只有亮／暗。外接 LED 要串電阻。可用的腳位有限制：0、2、4、15、18、21、22、26、32、33。',
      d.analogWrite(26, 20), {w: 290, h: 120}),
    d.delay(600),
    d.analogWrite(26, 80),
    d.delay(600),
    d.analogWrite(26, 255),
    d.delay(600),
    d.analogWrite(26, 0),
    d.delay(600)
  ]
};
