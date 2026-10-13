'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '09-ir-line', name: '紅外線偵測停車',
    desc: '紅外線感測器偵測到東西（例如黑線）就停車，沒有就以 40% 動力前進。',
    hw: 'M1 + 紅外線感測器（數位，GPIO 15）', level: '基礎', series: 'C'},
  setup: [d.stop(0)],
  loop: [
    d.note('紅外線（數位）只回 0 或 1。哪個代表「偵測到」依模組而定：先到「硬體檢測」頁看一次讀值，再決定這裡填 1 還是 0。',
      d.ifElse(d.eq(d.irDigital(15), 1), [d.stop(1)], [d.pwm(1, 'FORWARD', 40)]),
      {w: 290, h: 130}),
    d.note('每圈只判斷一次，延遲 50 毫秒是取樣節拍，不要設 0。', d.delay(50), {w: 240, h: 80})
  ]
};
