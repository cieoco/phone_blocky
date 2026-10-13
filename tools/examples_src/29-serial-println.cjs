'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '29-serial-println', name: '序列輸出',
    desc: '同一個數字送兩個地方：顯示訊息到網頁，序列輸出到 USB 序列監視器。',
    hw: '不用接；序列輸出需用 USB 線接電腦', level: '基礎', series: 'D'},
  setup: [d.setVar('n', d.num(0))],
  loop: [
    d.changeVar('n', d.num(1)),
    d.note('顯示訊息：出現在網頁的訊息列，手機上看得到。', d.print(d.getVar('n')), {h: 80}),
    d.note('序列輸出：只從 USB 送出，在電腦的序列監視器（115200）看到「[PROG] serial_print=數字」，網頁上看不到。',
      d.serialPrintln(d.getVar('n')), {w: 280, h: 110}),
    d.delay(500)
  ]
};
