'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '31-patrol', name: '巡邏車',
    desc: '按按鈕出發，前進；前方 20 cm 內有障礙就停下來、舵機掃一遍，沒擋了再繼續走。',
    hw: 'M1 + S1 + 超音波 TRIG 2 / ECHO 33 + LEGO 按鈕（GPIO 4）', level: '進階', series: 'E'},
  setup: [
    d.stop(0),
    d.servo(1, 90),
    d.note('先等按鈕（按下讀到 0；接法相反就改 1），按了才進入 loop。',
      d.untilDo(d.eq(d.legoButton(4), 0), [d.delay(20)]), {h: 90})
  ],
  loop: [
    d.note('有障礙：停車、舵機左右掃一遍，下一圈再量。沒障礙：前進。',
      d.ifElse(d.lt(d.ultrasonic(2, 33), 20),
        [d.stop(1), d.servo(1, 30), d.delay(500), d.servo(1, 150), d.delay(500), d.servo(1, 90), d.delay(300)],
        [d.pwm(1, 'FORWARD', 40)]), {w: 280, h: 110}),
    d.delay(100)
  ]
};
