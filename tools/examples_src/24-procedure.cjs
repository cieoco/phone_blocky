'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '24-procedure', name: '副程式：抖一下',
    desc: '把「舵機左右抖一下」包成副程式，主程式呼叫兩次。同一段動作只要寫一遍。',
    hw: 'S1（GPIO 5）', level: '進階', series: 'D'},
  setup: [d.servo(1, 90)],
  loop: [
    d.note('呼叫副程式 = 把它的動作整段執行一遍。呼叫兩次就抖兩下。', d.callProc('抖一下'), {h: 80}),
    d.delay(1000),
    d.callProc('抖一下'),
    d.delay(2000)
  ],
  extra: [
    d.note('副程式定義放在 setup／loop 外面。改這裡，所有呼叫的地方都跟著變。副程式不能呼叫自己。',
      d.defProc('抖一下', [d.servo(1, 60), d.delay(200), d.servo(1, 120), d.delay(200), d.servo(1, 90)]),
      {w: 280, h: 110})
  ]
};
