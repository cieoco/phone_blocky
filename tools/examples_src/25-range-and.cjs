'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '25-range-and', name: '距離在範圍內才走',
    desc: '前方 20–60 cm 之間才前進，太近或太遠都停。用「而且」把兩個條件合在一起。',
    hw: 'M1 + 超音波 TRIG 2 / ECHO 33', level: '進階', series: 'D'},
  setup: [d.stop(0)],
  loop: [
    d.note('「而且」兩邊都成立才算成立。換成「或者」則任一邊成立就算。超音波沒回波會讀到 -1，也會停。',
      d.ifElse(d.and(d.gt(d.ultrasonic(2, 33), 20), d.lt(d.ultrasonic(2, 33), 60)),
        [d.pwm(1, 'FORWARD', 40)], [d.stop(1)]), {w: 290, h: 120}),
    d.delay(100)
  ]
};
