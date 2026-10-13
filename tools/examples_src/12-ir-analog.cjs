'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '12-ir-analog', name: '紅外線類比門檻',
    desc: '讀紅外線的類比值（0–4095），超過門檻就停車。先看 Plotter 曲線再決定門檻。',
    hw: 'M1 + 紅外線感測器（類比，GPIO 32）', level: '基礎', series: 'C'},
  setup: [d.stop(0)],
  loop: [
    d.note('先開 Plotter 看反射值：有東西和沒東西各是多少，門檻取兩者中間。',
      d.plot('反射值', 'ADC', d.irAnalog(32)), {w: 260, h: 100}),
    d.note('2000 是舉例，請依你的曲線改。越近是越大還是越小依模組而定，如果相反就把 > 改成 <。',
      d.ifElse(d.gt(d.irAnalog(32), 2000), [d.stop(1)], [d.pwm(1, 'FORWARD', 40)]),
      {w: 280, h: 120}),
    d.delay(100)
  ]
};
