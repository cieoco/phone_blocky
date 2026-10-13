'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '07-encoder-plot', name: '讀編碼器角度',
    desc: '用手轉 M3 的軸，把編碼器讀到的角度即時畫到 Plotter。學會 encoder_read 積木。',
    hw: 'M3（含編碼器）', level: '基礎', series: 'B'},
  setup: [
    d.stop(3),
    d.note('在這個位置設零點，之後正轉為正、反轉為負。', d.zero(3), {w: 240, h: 80})
  ],
  loop: [
    d.note('編碼器積木可選角度（°）、計數、轉速。到 ☰ → 開啟獨立 Plotter 看曲線，用手轉軸就會動。延遲 100 毫秒是取樣節拍，不要設 0。',
      d.plot('角度', '°', d.encoder(3, 'deg')), {w: 290, h: 130}),
    d.delay(100)
  ]
};
