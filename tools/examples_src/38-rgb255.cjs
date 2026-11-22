const d = require('./_dsl.cjs');
module.exports = {
  meta:{id:'38-rgb255',name:'讀取 RGB（0～255）',desc:'OLED 前三行依序顯示紅、綠、藍分量。',hw:'TCS34725 與 OLED 0x3C',level:'入門',series:'C'},
  setup:[d.block('oled_clear',{ADDRESS:'60'})],
  loop:['r','g','b'].map((channel,row)=>d.block('oled_value',{ADDRESS:'60',ROW:String(row)},{VALUE:d.block('tcs34725_rgb',{CHANNEL:channel})})).concat(d.delay(100))
};
