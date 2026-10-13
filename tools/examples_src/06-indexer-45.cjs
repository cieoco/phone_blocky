'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '06-indexer-45', name: '分度盤 45°',
    desc: 'M3 每 1.5 秒再轉 45°，8 次剛好一圈。學會相對角度 move_by 與等到位再下一步。',
    hw: 'M3（含編碼器）', level: '基礎', series: 'B'},
  setup: [
    d.stop(3),
    d.zero(3)
  ],
  loop: [
    d.note('move_by 是「從現在的位置再轉多少度」，不是從上一個目標算。所以要先等上一步到位（延遲 1500 毫秒），否則會少轉。轉得慢就把延遲加長，不要設太短。',
      d.moveBy(3, 45), {w: 290, h: 140}),
    d.delay(1500)
  ]
};
