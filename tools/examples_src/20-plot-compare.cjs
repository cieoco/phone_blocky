'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '20-plot-compare', name: '目標與實際轉速',
    desc: 'M3 跑 120 RPM，Plotter 同時畫「目標」與「實際」兩條線，看它追得準不準。',
    hw: 'M3（含編碼器）', level: '進階', series: 'D'},
  setup: [
    d.note('轉速閉迴路會一直維持，按底列「停止」才會停。', d.speed(3, 120), {h: 80})
  ],
  loop: [
    d.note('名稱不同就是不同的線，全部畫在同一張圖、共用縱軸。板子每 50 毫秒最多送一筆繪圖資料，所以兩個繪圖之間要隔至少 60 毫秒，否則第二筆會被丟掉。Plotter 不畫負數。',
      d.plot('目標', 'rpm', 120), {w: 300, h: 150}),
    d.delay(60),
    d.plot('實際', 'rpm', d.encoder(3, 'rpm')),
    d.delay(60)
  ]
};
