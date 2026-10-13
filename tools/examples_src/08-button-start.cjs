'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '08-button-start', name: '按鈕才開始',
    desc: '程式先停在原地等你按 LEGO 按鈕，按下後馬達轉 2 秒再停。學會「直到…為止」等待迴圈。',
    hw: 'M1 + LEGO 按鈕（GPIO 4）', level: '基礎', series: 'C'},
  setup: [
    d.stop(0),
    d.note('「直到」迴圈：條件成立之前一直重複裡面的動作，成立就往下走。按鈕腳內建上拉電阻，沒按是 1、按下去通常讀到 0；如果你的接法相反，把 0 改成 1。',
      d.untilDo(d.eq(d.legoButton(4), 0), [d.delay(20)]), {w: 300, h: 150}),
    d.pwm(1, 'FORWARD', 50),
    d.delay(2000),
    d.note('等待迴圈裡的延遲 20 毫秒是讓出時間給板子，不要拿掉。放在 setup 只跑一遍；想每次都重新等就改放 loop。',
      d.stop(1), {w: 280, h: 110})
  ],
  loop: []
};
