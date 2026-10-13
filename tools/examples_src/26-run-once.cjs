'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '26-run-once', name: '只做一次的旗標',
    desc: '用 true／false 當旗標：馬達在 loop 裡只轉一次，做完把旗標改成 true，之後不再執行。',
    hw: 'M1', level: '進階', series: 'D'},
  setup: [
    d.stop(0),
    d.note('旗標 done：false 代表還沒做。', d.setVar('done', d.bool(false)), {h: 70})
  ],
  loop: [
    d.note('「不是」done 才進來；做完把 done 設 true，下一圈就跳過。要再做一次就重新按執行。',
      d.ifThen(d.not(d.getVar('done')), [
        d.pwm(1, 'FORWARD', 50),
        d.delay(1500),
        d.stop(1),
        d.setVar('done', d.bool(true))
      ]), {w: 280, h: 110}),
    d.delay(50)
  ]
};
