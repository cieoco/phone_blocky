'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id:'36-color-rgbc', name:'顏色感測器 RGBC 顯示',
    desc:'TCS34725 與 OLED 共用 I2C；四行依序顯示 R、G、B、C 原始讀值。',
    hw:'TCS34725 0x29 ＋ OLED 0x3C，SDA21/SCL22', level:'入門', series:'C'},
  setup: [d.note('兩個模組共用 SDA21/SCL22、3.3V 與 GND，不接上層主控。免初始化；剛開始或讀取失敗為 -1。原始讀值不是 0–255 RGB，也不直接代表顏色名稱。',
    d.block('oled_clear',{ADDRESS:'60'}))],
  loop: ['r','g','b','c'].map((channel,row)=>d.block('oled_value',
    {ADDRESS:'60',ROW:String(row)}, {VALUE:d.block('tcs34725_read',{CHANNEL:channel})}))
    .concat([d.delay(100)])
};
