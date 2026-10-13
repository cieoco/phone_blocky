'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '21-counter', name: '計數器',
    desc: '用變數記住「第幾圈」：每圈加 1，並把數字顯示在訊息列。學會設定、讀取、增加變數。',
    hw: '不用接任何東西', level: '基礎', series: 'D'},
  setup: [
    d.note('變數像一個有名字的盒子。先把 count 設成 0，每次執行都從 0 開始。', d.setVar('count', d.num(0)), {h: 90})
  ],
  loop: [
    d.note('每圈把 count 加 1（改 1 看看）。', d.changeVar('count', d.num(1)), {h: 70}),
    d.note('數字會出現在畫面下方的訊息列。延遲不要設 0，否則訊息刷太快。', d.print(d.getVar('count')), {h: 90}),
    d.delay(500)
  ]
};
