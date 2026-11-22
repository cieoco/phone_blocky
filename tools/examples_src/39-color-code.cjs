'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id:'39-color-code', name:'在手機查看顏色代碼',
    desc:'只接顏色感測器，在頁面的訊息區查看辨色結果和代碼對照。',
    hw:'TCS34725，SDA21/SCL22', level:'入門', series:'C'},
  setup: [],
  loop: [
    d.print(d.note('代碼對照：1 紅、2 黃、3 藍、4 綠、0 未知、-1 尚無可用結果。用色紙靠近感測器，保持相近的距離與光線，再觀察代碼。', d.block('tcs34725_color'))),
    d.delay(500)
  ]
};
