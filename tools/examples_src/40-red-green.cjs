'use strict';
const d = require('./_dsl.cjs');
const isColor = code => d.block('tcs34725_is_color', {COLOR:code});
const show = text => d.block('oled_text', {ADDRESS:'60', ROW:'0', TEXT:text});
module.exports = {
  meta: {id:'40-red-green', name:'紅綠色卡判斷',
    desc:'用「是否為指定顏色」做條件，OLED 顯示 RED、GREEN 或等候提示。',
    hw:'TCS34725 0x29 ＋ OLED 0x3C，SDA21/SCL22', level:'入門', series:'C'},
  setup: [d.block('oled_clear', {ADDRESS:'60'})],
  loop: [
    d.note('依次把紅色和綠色色卡移到感測器前。設置否則分支，拿走色卡後也會更新畫面。', d.ifElse(isColor('1'), [show('RED')], [
      d.ifElse(isColor('4'), [show('GREEN')], [show('SHOW RED OR GREEN')])
    ])),
    d.delay(150)
  ]
};
