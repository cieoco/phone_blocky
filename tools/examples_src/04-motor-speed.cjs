'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '04-motor-speed', name: 'M3 固定轉速',
    desc: '用編碼器閉迴路讓 M3 維持 120 RPM。負載變重時動力會自動加大，轉速不會掉。',
    hw: 'M3（含編碼器）', level: '基礎', series: 'A'},
  setup: [
    d.stop(0)
  ],
  loop: [
    d.note('轉速單位是 RPM，Blockly 只能填 60–250 的正數。和動力 pwm 不同：pwm 固定給多少力，負載一重就變慢；speed 會看編碼器自動補力，轉速維持不變。只有 M3、M4 有編碼器。',
      d.speed(3, 120), {w: 290, h: 150}),
    d.delay(4000),
    d.stop(3),
    d.delay(2000)
  ]
};
