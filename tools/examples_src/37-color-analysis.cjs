const d = require('./_dsl.cjs');
module.exports = {
 meta:{id:'37-color-analysis',name:'顏色判斷與照度',desc:'設定曝光，顯示估算照度、色溫、顏色代碼及紅色判斷。',hw:'TCS34725 與 OLED，SDA21/SCL22',level:'進階',series:'C'},
 setup:[d.note('辨色受光源、距離影響，代碼 0未知/1紅/2黃/3藍/4綠；-1 為準備中、故障或飽和。',d.block('tcs34725_gain',{GAIN:'1'})),d.block('tcs34725_time',{CYCLES:'10'}),d.block('oled_clear',{ADDRESS:'60'})],
 loop:['lux','cct','color'].map((kind,row)=>d.block('oled_value',{ADDRESS:'60',ROW:String(row)},{VALUE:d.block('tcs34725_'+kind)})).concat([
 d.block('oled_text',{ADDRESS:'60',ROW:'3',TEXT:'OTHER'}),
 d.block('controls_if',{}, {IF0:d.block('tcs34725_is_color',{COLOR:'1'})},{DO0:[d.block('oled_text',{ADDRESS:'60',ROW:'3',TEXT:'RED'})]}),d.delay(100)])
};
