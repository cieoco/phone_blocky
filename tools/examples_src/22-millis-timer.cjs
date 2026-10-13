'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '22-millis-timer', name: '不卡住的計時',
    desc: '舵機每秒換一次角度，同時還能即時偵測按鈕。用開機毫秒數相減，取代會卡住程式的延遲。',
    hw: 'S1 + M1 + LEGO 按鈕（GPIO 4）', level: '進階', series: 'D'},
  setup: [
    d.servo(1, 0),
    d.stop(0),
    d.setVar('last', d.num(0)),
    d.setVar('up', d.num(0))
  ],
  loop: [
    d.note('現在時間 − 上次換角度的時間 ≥ 1000，才換一次。中間程式沒有睡著，下面的按鈕馬上有反應。',
      d.ifThen(d.gte(d.sub(d.millis(), d.getVar('last')), 1000), [
        d.ifElse(d.eq(d.getVar('up'), 0),
          [d.servo(1, 90), d.setVar('up', d.num(1))],
          [d.servo(1, 0), d.setVar('up', d.num(0))]),
        d.setVar('last', d.millis())
      ]), {w: 280, h: 110}),
    d.note('按住按鈕（讀到 0；接法相反就改 1）馬達轉，放開就停。',
      d.ifElse(d.eq(d.legoButton(4), 0), [d.pwm(1, 'FORWARD', 40)], [d.stop(1)]), {h: 90}),
    d.delay(20)
  ]
};
