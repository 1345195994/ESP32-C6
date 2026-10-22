#pragma once
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 时钟设置</title><style>
*{box-sizing:border-box}body{margin:20px auto;padding:0 14px;max-width:800px;background:#101823;color:#e4edf7;font:15px/1.6 system-ui,sans-serif}h1{font-size:25px}fieldset{border:1px solid #405168;border-radius:10px;margin:16px 0;padding:16px}legend{padding:0 8px}label{display:block;margin:10px 0}input,select,textarea,button{font:inherit;border:1px solid #51657d;border-radius:6px;padding:7px;background:#1b293b;color:inherit}input:not([type=color]),select,textarea{width:100%}input[type=color]{width:100%;height:38px;padding:2px}textarea{min-height:155px}button{cursor:pointer;margin:5px 5px 5px 0}button:disabled{opacity:.5;cursor:wait}.grid{display:grid;grid-template-columns:1fr 1fr;gap:0 16px}.active{background:#207564}.hint,#status{color:#a8c7dd}pre{white-space:pre-wrap;overflow-wrap:anywhere}.tabs{margin:12px 0}@media(max-width:500px){.grid{grid-template-columns:1fr}}
</style><h1>时钟控制台</h1><p id="summary">正在读取设置…</p>
<label>屏幕切页<select id="jump"><option value="0">1</option><option value="1">2</option><option value="2">3</option><option value="3">4</option></select></label><button id="jumpButton" type="button">切换</button>
<form id="settings"><fieldset><legend>通用设置</legend><div id="general" class="grid"></div></fieldset>
<nav class="tabs"><button type="button" data-tab="1">1 · 时钟</button><button type="button" data-tab="2">2 · 课表</button><button type="button" data-tab="3">3 · 文字</button><button type="button" data-tab="4">4 · 网络</button></nav>
<fieldset data-panel="1"><legend>第一页（始终启用）</legend><div id="page1" class="grid"></div><label>天气地址（城市或区县）<input id="weatherCity" name="weatherCity" maxlength="32" placeholder="例如：广州、海淀区"></label><button id="weatherSearch" type="button">查询天气地址</button><label>选择匹配地点<select id="weatherMatches"><option value="">先查询地址</option></select></label><p class="hint">查询后选择正确地点，也可直接填写经纬度。设备联网、首次校时完成后获取天气，之后每 30 分钟更新；失败会重试。天气来源：<a href="https://open-meteo.com/" target="_blank" rel="noopener">Open-Meteo</a>。</p><p id="weatherStatus" class="hint"></p></fieldset>
<fieldset data-panel="2" hidden><legend>第二页</legend><div id="page2" class="grid"></div><label>本周课表 CSV<textarea id="scheduleCsv" name="scheduleCsv" spellcheck="false"></textarea></label><label>导入 CSV 文件<input id="scheduleFile" type="file" accept=".csv,text/csv"></label><p class="hint">每行：星期,开始时间,结束时间,课程名称,地点。星期 1–7 表示周一至周日；最多 42 节、3500 字节。示例：<br>1,08:00,08:45,高等数学,教室A<br>不支持字段中包含逗号。始终显示当天全部课程；20 分钟内开课的行高亮。屏幕只显示开始时间、课程名称、教室，超出一屏每 5 秒翻页。</p></fieldset>
<fieldset data-panel="3" hidden><legend>第三页</legend><label>自定义文字<input id="thirdMessage" name="thirdMessage" maxlength="120"></label><div id="page3" class="grid"></div><p class="hint">网页按实际文字生成原生字模，减少缺字；长文字完整参与滚动。静止模式超出一屏时每 4 秒分段显示，不使用省略号。</p></fieldset>
<fieldset data-panel="4" hidden><legend>网络状态</legend><pre id="networkInfo"></pre><p class="hint">每次开机联网校时一次，成功后停止网络校时。</p></fieldset>
<button id="save" type="submit" disabled>保存设置</button></form><p id="status" role="status"></p>
<script>
'use strict';
const $=id=>document.getElementById(id), fields=[];
const booleanOptions=[['1','启用'],['0','关闭']];
function control(panel,id,label,type,options){
 const row=document.createElement('label');row.textContent=label;
 const input=document.createElement(type==='select'?'select':'input');input.id=id;input.name=id;
 if(type==='select')for(const pair of options){const o=document.createElement('option');o.value=pair[0];o.textContent=pair[1];input.append(o);}
 else{input.type=type;if(options){input.min=options[0];input.max=options[1];input.step=1;}}
 row.append(input);$(panel).append(row);fields.push(id);
}
control('general','landscape','屏幕方向','select',[['1','横屏'],['0','纵屏']]);
control('general','autoCarousel','自动轮播','select',booleanOptions);
control('general','carouselSeconds','每页停留（秒）','number',[2,60]);
control('general','brightness','背光亮度（10–50%）','number',[10,50]);
control('general','bg','背景颜色','color');control('general','footerColor','底部数字颜色','color');
control('page1','largeTimeSize','时间字号','number',[16,48]);
control('page1','weatherEnabled','显示天气','select',booleanOptions);
control('page1','weatherLat','天气纬度（-90～90）','number',[-90,90]);
control('page1','weatherLon','天气经度（-180～180）','number',[-180,180]);
$('weatherLat').step='any';$('weatherLon').step='any';
for(const pair of [['clock','时间'],['date','日期和星期'],['network','Wi-Fi 状态'],['ipColor','IP 地址']])control('page1',pair[0],pair[1]+'颜色','color');
control('page2','page2Enabled','启用第二页','select',booleanOptions);
control('page2','smallTimeSize','小时间字号','number',[12,30]);
control('page2','scheduleTextSize','课表字号','number',[10,24]);
for(const pair of [['page2TimeColor','时间'],['page2DateColor','日期'],['messageColor','课表文字']])control('page2',pair[0],pair[1]+'颜色','color');
control('page3','page3Enabled','启用第三页','select',booleanOptions);
control('page3','thirdTimeSize','时间字号','number',[12,42]);
control('page3','thirdTimeFormat','时间格式','select',[['0','24 小时（含秒）'],['1','12 小时（含秒）'],['2','24 小时（时:分）']]);
control('page3','thirdTextSize','文字字号','number',[10,30]);
control('page3','marqueeMode','滚动方式','select',[['5','不滚动'],['0','水平往返'],['1','向左'],['2','向右'],['3','向上'],['4','向下'],['6','斜向右上'],['7','斜向左下']]);
control('page3','rainbowText','彩虹文字','select',booleanOptions);
control('page3','page3TimeColor','时间颜色','color');control('page3','page3MessageColor','文字颜色','color');
fields.push('scheduleCsv','thirdMessage','weatherCity');
let device=null,busy=false,resolvedWeatherCity='',weatherMatches=[];
for(const id of ['weatherLat','weatherLon'])$(id).addEventListener('input',()=>resolvedWeatherCity=$('weatherCity').value.trim());
$('weatherSearch').addEventListener('click',async()=>{
 const name=$('weatherCity').value.trim();if(!name){$('weatherStatus').textContent='请填写城市或区县';return;}
 $('weatherSearch').disabled=true;$('weatherStatus').textContent='正在查询…';
 try{
  const response=await fetch('https://geocoding-api.open-meteo.com/v1/search?'+new URLSearchParams({name,count:'8',language:'zh',format:'json'}));
  if(!response.ok)throw Error('地址服务暂不可用');
  const data=await response.json();if($('weatherCity').value.trim()!==name)return;
  weatherMatches=data.results||[];$('weatherMatches').replaceChildren();
  const empty=document.createElement('option');empty.value='';empty.textContent='请选择匹配地点';$('weatherMatches').append(empty);
  weatherMatches.forEach((place,index)=>{const option=document.createElement('option');option.value=String(index);option.textContent=[place.name,place.admin1,place.country].filter(Boolean).join(' · ');$('weatherMatches').append(option);});
  $('weatherStatus').textContent=weatherMatches.length?'请选择地点，然后保存':'没有匹配地点，请尝试城市名、拼音或手动填写经纬度';
 }catch(error){$('weatherStatus').textContent='查询失败：'+error.message+'；可直接填写经纬度';}
 finally{$('weatherSearch').disabled=false;}
});
$('weatherMatches').addEventListener('change',()=>{if($('weatherMatches').value==='')return;const place=weatherMatches[Number($('weatherMatches').value)];if(!place)return;$('weatherLat').value=place.latitude;$('weatherLon').value=place.longitude;resolvedWeatherCity=$('weatherCity').value.trim();$('weatherStatus').textContent='地点已选择，请保存设置';});
function tab(n){document.querySelectorAll('[data-panel]').forEach(p=>p.hidden=p.dataset.panel!==n);document.querySelectorAll('[data-tab]').forEach(b=>b.classList.toggle('active',b.dataset.tab===n));}
document.querySelectorAll('[data-tab]').forEach(b=>b.addEventListener('click',()=>tab(b.dataset.tab)));tab('1');
async function request(url,data){const opts={cache:'no-store'};if(data){opts.method='POST';opts.headers={'Content-Type':'application/x-www-form-urlencoded'};opts.body=new URLSearchParams(data);}const r=await fetch(url,opts);if(!r.ok)throw Error(await r.text());return r.json();}
function showState(s){device=s;resolvedWeatherCity=s.weatherCity||'';$('weatherStatus').textContent=s.weatherStatus||'';for(const id of fields){if(s[id]!==undefined)$(id).value=typeof s[id]==='boolean'?(s[id]?'1':'0'):s[id];} $('jump').value=String(s.currentPage);$('summary').textContent='IP：'+(s.ip||'未连接')+' · '+(s.timeSynced?'本次开机已校时':'等待校时');$('networkInfo').textContent='SSID: '+s.ssid+'\nIP: '+(s.ip||'未连接')+'\n信号: '+s.rssi+' dBm\n可用堆: '+s.freeHeap+' 字节\n最大连续块: '+s.maxHeap+' 字节\n运行: '+s.uptime+' 秒';}
function fontPacket(s){
 const c=document.createElement('canvas'),ctx=c.getContext('2d',{willReadFrequently:true});
 const groups=[
  {size:Number(s.scheduleTextSize),text:'时间课程名称教室20 分钟内即将上课今日课表今天没有课程0123456789:- '+s.scheduleCsv},
  {size:Number(s.thirdTextSize),text:s.thirdMessage||' '},
  {size:20,text:s.ssid||' '}
 ];
 const all=[],metrics=[];
 for(let group=0;group<groups.length;group++){
  const size=groups[group].size,font='600 '+size+'px "Microsoft YaHei","Noto Sans CJK SC","PingFang SC",sans-serif';
  ctx.font=font;const probe=ctx.measureText('Ag国');
  let ascent=Math.ceil(probe.actualBoundingBoxAscent??size),descent=Math.max(0,Math.ceil(probe.actualBoundingBoxDescent??size/4));
  const chars=Array.from(new Set(Array.from(groups[group].text))).filter(ch=>ch.codePointAt(0)>=32).sort((a,b)=>a.codePointAt(0)-b.codePointAt(0));
  for(const ch of chars){
   ctx.font=font;const m=ctx.measureText(ch),left=Math.ceil(m.actualBoundingBoxLeft??0),right=Math.ceil(m.actualBoundingBoxRight??m.width);
   let up=Math.max(0,Math.ceil(m.actualBoundingBoxAscent??size)),down=Math.max(0,Math.ceil(m.actualBoundingBoxDescent??size/4));
   const w=Math.max(0,left+right),h=up+down;
   if(w>64||h>64)throw Error('某个字符字形过大，请减小字号');
   ascent=Math.max(ascent,up);descent=Math.max(descent,down);
   let data=new Uint8Array(Math.ceil(w*h/8));
   if(w&&h){
    c.width=w;c.height=h;ctx.font=font;ctx.textBaseline='alphabetic';ctx.fillStyle='white';ctx.fillText(ch,left,up);
    const rgba=ctx.getImageData(0,0,w,h).data;
    for(let p=0;p<w*h;p++)if(rgba[p*4+3]>=96)data[p>>3]|=0x80>>(p&7);
   }
   // A one-pixel bearing and full ink-width advance keep italic/overhanging glyphs inside the label.
   all.push({code:ch.codePointAt(0),advance:Math.max(Math.ceil(m.width),w)+2,w,h,x:1,y:-down,group,data});
  }
  metrics.push({height:ascent+descent+4,baseline:descent+2});
 }
 if(all.length>768)throw Error('本次不同字符过多，请减少课表内容');
 const bytes=new Uint8Array(32+all.length*16+all.reduce((n,g)=>n+g.data.length,0)),dv=new DataView(bytes.buffer);
 if(bytes.length>32768)throw Error('本次字模超过 32 KB，请减小课表或第三页的字号后保存');
 dv.setUint32(0,0x32504647,true);dv.setUint16(4,all.length,true);bytes[6]=3;
 metrics.forEach((m,i)=>{dv.setUint16(8+i*4,m.height,true);dv.setUint16(10+i*4,m.baseline,true);});
 let offset=32+all.length*16;
 all.forEach((g,i)=>{const p=32+i*16;dv.setUint32(p,g.code,true);dv.setUint32(p+4,offset,true);dv.setUint16(p+8,g.advance,true);bytes[p+10]=g.w;bytes[p+11]=g.h;dv.setInt8(p+12,g.x);dv.setInt8(p+13,g.y);bytes[p+14]=g.group;bytes.set(g.data,offset);offset+=g.data.length;});
 return {bytes,count:all.length};
}
async function syncFonts(s){
 if(!s.ip||!s.timeSynced)throw Error('等待设备联网并完成首次校时后，再保存或刷新网页加载课表字模');
 if(s.fontReady)return;
 if(document.fonts)await document.fonts.ready;
 const packet=fontPacket(s);await request('/font/start',{key:s.fontKey,size:packet.bytes.length});
 for(let offset=0;offset<packet.bytes.length;offset+=512){let b='';for(const x of packet.bytes.subarray(offset,offset+512))b+=String.fromCharCode(x);await request('/font/chunk',{offset,data:btoa(b)});}
 const done=await request('/font/end',{key:s.fontKey});s.fontReady=true;s.fontCached=done.cached;
}
$('jumpButton').addEventListener('click',async()=>{try{await request('/page',{index:$('jump').value});$('status').textContent='屏幕已切换';}catch(e){$('status').textContent=e.message;}});
$('scheduleFile').addEventListener('change',async e=>{const f=e.target.files[0];if(!f)return;if(f.size>3500){$('status').textContent='CSV 超过 3500 字节';return;}$('scheduleCsv').value=(await f.text()).replace(/^\uFEFF/,'');$('status').textContent='课表已载入，请保存';});
$('settings').addEventListener('submit',async e=>{
 e.preventDefault();if(busy||!device)return;busy=true;$('save').disabled=true;$('status').textContent='正在保存…';
 if($('weatherEnabled').value==='1' && $('weatherCity').value.trim()!==resolvedWeatherCity){$('status').textContent='天气地址已修改，请查询并选择地点，或手动填写经纬度';busy=false;$('save').disabled=false;return;}
 let applied=false;
 try{const s=await request('/save',new FormData($('settings')));showState(s);applied=true;await syncFonts(s);$('status').textContent='设置与字模已应用'+(s.fontCached?'':'（较大字模重启后需打开网页恢复）');}
 catch(err){$('status').textContent=(applied?'设置已保存，字模更新失败：':'保存失败：')+err.message;}
 finally{busy=false;$('save').disabled=false;}
});
(async()=>{busy=true;try{const s=await request('/state');showState(s);await syncFonts(s);$('status').textContent='已载入；修改后保存即可';}catch(e){$('status').textContent='读取失败：'+e.message;}finally{busy=false;$('save').disabled=!device;}})();
</script></html>
)HTML";
