// vizMini control page (served at "/" in STA mode by web_ui.h).
// House style follows the vizBot firmware page (vizbot/web_server.h) and its
// OTA page (vizbot/ota_update.h): yellow header bar, cream cards with hard
// black shadows, square buttons, yellow = active, collapsible sections.
// Self-contained (no external fonts/libs). Polls /state every >= 2 s (that read
// resets the party timing counters) and /debug/screen once a second for the
// live OLED preview; both pause while the tab is hidden.
#pragma once
#include <Arduino.h>

static const char CONTROL_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>vizMini</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;background:#e8e4dc;color:#000;min-height:100vh}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:#FFD23F;border:3px solid #000;box-shadow:0 5px 0 0 #000}
.logo{font-size:20px;font-weight:800;letter-spacing:-.5px}
.dn{color:#333;font-size:12px;margin-left:10px;font-weight:600}
.hr{display:flex;align-items:center;gap:6px}
.dot{width:10px;height:10px;border-radius:50%;border:2px solid #000;background:#ddd;display:inline-block}
.dot.on{background:#88D498}.dot.err{background:#FF6B6B}
.sl{color:#333;font-size:11px;font-weight:600}
.bars{display:inline-flex;align-items:flex-end;gap:2px;margin-left:4px}
.bars i{width:5px;border:1px solid #000;background:#fff6}.bars i.on{background:#000}
.layout{display:grid;grid-template-columns:1fr;gap:12px;padding:16px;max-width:1100px;margin:0 auto}
@media(min-width:768px){.layout{grid-template-columns:1fr 1fr;padding:20px;gap:14px}}
.col{display:flex;flex-direction:column;gap:12px}
.card{background:#fffdf5;border:3px solid #000;box-shadow:5px 5px 0 0 #000;padding:16px}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.08em;margin-bottom:12px;font-weight:800;display:flex;justify-content:space-between;align-items:center}
.shdr{cursor:pointer;user-select:none;margin-bottom:0}.shdr:not(.shut){margin-bottom:12px}.shdr:hover{color:#555}
.chv{font-size:10px;transition:transform .2s;display:inline-block}.shdr.shut .chv{transform:rotate(-90deg)}
.sbody{overflow:hidden;transition:max-height .3s ease;max-height:2000px}.sbody.shut{max-height:0}
.gx{display:grid;grid-template-columns:repeat(auto-fill,minmax(78px,1fr));gap:6px}
.g3{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.seg{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;gap:6px}
button{background:#e8e8e8;border:2px solid #000;box-shadow:3px 3px 0 0 #000;color:#000;padding:10px 4px;border-radius:0;font-family:inherit;font-size:12px;font-weight:600;cursor:pointer;transition:transform .1s,box-shadow .1s;text-transform:capitalize}
button:hover{transform:translate(-1px,-1px);box-shadow:4px 4px 0 0 #000}
button:active{transform:translate(2px,2px);box-shadow:none}
button.on{background:#FFD23F}
.send{background:#FFA552;font-weight:700;padding:10px 16px}.go{background:#B8A9FA;width:100%;margin-top:14px}
.sm{font-size:11px;padding:3px 10px;box-shadow:2px 2px 0 0 #000;text-transform:none}
.inp{width:100%;padding:10px 12px;border:2px solid #000;box-shadow:3px 3px 0 0 #000;background:#fff;color:#000;font-size:16px;border-radius:0;font-family:inherit}
.inp:focus{outline:none;box-shadow:3px 3px 0 0 #74B9FF}
.row{display:flex;gap:8px;align-items:center}.f1{flex:1}
.srow{display:flex;justify-content:space-between;margin:14px 0 6px;font-size:13px;color:#555;font-weight:600}
input[type=range]{width:100%;height:6px;background:#ddd;border:1px solid #000;-webkit-appearance:none;appearance:none;border-radius:0}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;background:#B8A9FA;border:2px solid #000;box-shadow:2px 2px 0 0 #000;cursor:pointer;border-radius:0}
input[type=range]::-moz-range-thumb{width:16px;height:16px;background:#B8A9FA;border:2px solid #000;border-radius:0}
.trow{display:flex;justify-content:space-between;align-items:center;padding:8px 0;font-size:14px;font-weight:500}
.tog{width:44px;height:24px;background:#ddd;border:2px solid #000;box-shadow:2px 2px 0 0 #000;position:relative;cursor:pointer;transition:background .2s}
.tog.on{background:#B8A9FA}
.tog::after{content:'';position:absolute;width:16px;height:16px;background:#fff;border:2px solid #000;top:2px;left:2px;transition:transform .2s}
.tog.on::after{transform:translateX(20px)}
.lbl{display:block;font-size:13px;color:#555;font-weight:600;margin:14px 0 6px}
.hint{font-size:12px;color:#888;margin-top:8px}
.scr{background:#000;border:3px solid #000;box-shadow:3px 3px 0 0 #000;padding:6px}
canvas{display:block;width:100%;aspect-ratio:2/1;image-rendering:crisp-edges;image-rendering:pixelated;background:#05080b}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:14px}
.chip{border:2px solid #000;padding:3px 8px;font-size:11px;font-weight:800;text-transform:uppercase;letter-spacing:.05em;background:#fff}
.chip b{font-weight:600;color:#0009;margin-right:5px}
.cy{background:#FFD23F}.cb{background:#74B9FF}.cp{background:#B8A9FA}.co{background:#FFA552}.cg{background:#88D498}
.ck{background:#000;color:#fff}.ck b{color:#fffa}
.fx{margin-top:12px;padding:10px 12px;border:2px solid #000;background:#fff;font-size:14px;font-weight:700}
.sw{text-transform:capitalize}.sw i{display:block;height:14px;border:2px solid #000;margin:-2px 0 6px}
.gq{display:grid;grid-template-columns:repeat(6,1fr);gap:6px}.gq button{height:34px;padding:0}
.gq button.on{box-shadow:inset 0 0 0 3px #000,inset 0 0 0 5px #fff,3px 3px 0 0 #000}
input[type=color]{-webkit-appearance:none;appearance:none;border:2px solid #000;box-shadow:3px 3px 0 0 #000;background:#fff;padding:2px;height:40px;width:100%;cursor:pointer;border-radius:0}
input[type=color]::-webkit-color-swatch-wrapper{padding:0}input[type=color]::-webkit-color-swatch{border:0}input[type=color]::-moz-color-swatch{border:0}
.big{width:74px!important;height:auto!important;flex:none}
.cd{display:inline-block;width:9px;height:9px;border:1px solid #000;margin-left:5px}
.kv{display:flex;justify-content:space-between;padding:6px 0;font-size:13px;border-bottom:1px solid #eee}
.kv span:first-child{color:#666;font-weight:600}.kv span:last-child{font-weight:700}
.btn{display:inline-block;padding:7px 12px;background:#FFD23F;border:2px solid #000;box-shadow:2px 2px 0 0 #000;font-weight:700;font-size:11px;text-transform:uppercase;color:#000;text-decoration:none}
.status{text-align:center;color:#555;font-size:11px;padding:12px;font-weight:700;letter-spacing:.05em;text-transform:uppercase;border-top:3px solid #000;background:#fffdf5}
.status.err{color:#c00}
</style></head><body>
<header class=hdr><div class=row style="gap:0"><span class=logo>vizMini</span><span class=dn id=hv></span></div>
<div class=hr><span class=dot id=cd></span><span class=sl id=cl>Connecting</span>
<span class=bars id=sb><i style="height:5px"></i><i style="height:8px"></i><i style="height:11px"></i><i style="height:14px"></i></span></div></header>

<div class=layout><div class=col>
<div class=card><h2>Now <button class="sm on" id=lv onclick=live()>&#9679; Live</button></h2>
<div class=scr><canvas id=cv width=128 height=64></canvas></div>
<div class=chips id=ch></div></div>

<div class=card><h2>Say something</h2>
<form class=row onsubmit="say();return false"><input id=t class="inp f1" maxlength=48 placeholder="hello!" autocomplete=off><button class=send>Send</button></form>
<div class=hint id=sh>Pops up in a speech bubble under his face.</div></div>

<div class=card><h2>Expressions</h2><div class=gx id=ex></div></div>
</div><div class=col>

<div class=card><h2 class=shdr onclick="tg('sL')">Lights <span class=chv>&#9662;</span></h2><div class=sbody id=sL>
<div class=seg id=gL></div>
<div class=srow><span>Brightness</span><span id=lbv></span></div>
<input type=range id=lb min=0 max=255 value=160>
<div id=la><span class=lbl>Pattern</span><div class=gx id=lp></div>
<span class=lbl>Palette</span><div class=gx id=lq></div>
<div id=lc><span class=lbl>Custom palette colours</span><div class=row><input type=color id=k1><input type=color id=k2><input type=color id=k3></div></div>
<div class=srow><span>Speed</span><span id=lsv></span></div>
<input type=range id=ls min=1 max=10 value=3>
<span class=lbl>Auto change pace</span><div class=seg id=gP></div><div class=hint id=lph></div></div>
<div id=lo><span class=lbl>Colour</span><div class=row style="align-items:stretch"><input type=color id=k0 class=big><div class="gq f1" id=lw></div></div></div>
<button class=go onclick="g('/bot/spark')">&#10024; Spark</button>
<div class=hint id=lh></div></div></div>

<div class=card><h2 class=shdr onclick="tg('sM')">Motion <span class=chv>&#9662;</span></h2><div class=sbody id=sM>
<div class=seg id=gM></div><div class=hint id=mh></div></div></div>

<div class=card><h2 class=shdr onclick="tg('sF')">Party mode <span class=chv>&#9662;</span></h2><div class=sbody id=sF>
<div class=seg id=gF></div>
<div class=fx id=fx>&hellip;</div>
<div class=g3 style="margin-top:12px"><button onclick="g('/bot/party?v=1')">Party now</button><button onclick="g('/bot/party?visit=1')">Visit 60s</button><button onclick="g('/bot/party?v=0')">Back to face</button></div>
<span class=lbl>Pattern stacking</span><div class=seg id=gK></div>
<div class=hint>Auto visits: 60 s of party every 8&ndash;15 min. Always: he stays a party drop. Stacking: how many patterns layer at once.</div></div></div>

<div class=card><h2 class=shdr onclick="tg('sS')">Screen <span class=chv>&#9662;</span></h2><div class=sbody id=sS>
<div class=seg id=gS></div>
<div class=hint id=shh></div>
<div class=trow style="margin-top:6px"><span>Falling snow</span><div class=tog id=sn onclick=snow()></div></div>
<div class=srow style="margin-top:4px"><span>Contrast</span><span id=cov></span></div>
<input type=range id=co min=10 max=255 value=180></div></div>

<div class=card><h2 class="shdr shut" onclick="tg('sP')">Playground <span class=chv>&#9662;</span></h2><div class="sbody shut" id=sP>
<span class=lbl style="margin-top:0">Party patterns</span><div class=gx id=fp></div>
<span class=lbl>Tentacles</span><div class=seg id=ft></div>
<span class=lbl>Motion test moves</span><div class=gx id=mv></div>
<span class=lbl>Timing</span><div id=pf></div>
<div class=hint>Party buttons switch him into party mode first.</div></div></div>

<div class=card><h2 class=shdr onclick="tg('sY')">System <span class=chv>&#9662;</span></h2><div class=sbody id=sY>
<div id=sy></div>
<div class=row style="margin-top:14px"><a class=btn href=/update>&#8593; Firmware update</a><a class=btn href=/state style="background:#fff">State JSON</a></div></div></div>
</div></div>
<div class=status id=st>Connecting to vizMini&hellip;</div>

<script>
var X=["neutral","happy","sad","surprised","chill","angry","love","dizzy","thinking","excited","mischief","skeptical","worried","confused","proud","shy","annoyed","focused","winking","devious","shocked","kissing","nervous","glitch","sassy"],
FP=["crown3","crown5","star6","field12","dipole","porcupine","split","wave","breathe","bloom","carousel"],
S={},T0=0,LP=0,pt,pb=0,fails=0,D={},LV=1,sb=0;
function $(i){return document.getElementById(i)}
function g(u){return fetch(u).catch(function(){}).then(function(){soon(400)})}
function mk(id,vals,fn,labs){var e=$(id);vals.forEach(function(v,i){var b=document.createElement('button');b.textContent=labs?labs[i]:v;b.dataset.v=v;b.onclick=function(){fn(v)};e.appendChild(b)})}
function hi(id,v){[].forEach.call($(id).children,function(b){b.classList.toggle('on',b.dataset.v==String(v))})}
function set(k,v){S[k]=v;up()}
function pc(v){return Math.round(v/2.55)+'%'}
function mm(n){n=Math.max(0,n|0);return (n/60|0)+':'+('0'+n%60).slice(-2)}
function esc(t){return String(t).replace(/[&<>"]/g,function(c){return'&#'+c.charCodeAt(0)+';'})}

mk('ex',X.map(function(n,i){return i}),function(i){set('expr',i);g('/bot/expression?v='+i)},X);
function L(k,v,q){set(k,v);g('/bot/leds?'+q+'='+v)}
mk('gL',['mood','ambient','solid','off'],function(v){L('leds',v,'mode')});
mk('lp',['auto','drift','comet','twinkle','swell','bounce','mirror','lava','confetti'],function(v){L('ledPatSel',v,'pat')});
var PAL={},PN=['auto'];"rainbow:ff0000,ff8000,ffff00,00ff00,0000ff,4b0082,8f00ff|lava:000000,ff0000,ff8000,ffff00|ocean:000060,0040ff,00a0a0,00ffff|forest:003000,00a000,a0ff00|party:8000ff,ff40a0,ff8000,ffe000,0060ff|heat:000000,ff0000,ffff00,ffffff|sunset:ff2800,ff005a,7800ff,ff7800|neon:00ffdc,ff00c8,0050ff,b400ff|ice:001040,0080ff,80e0ff,ffffff|candy:ff40a0,ffa0d0,80c0ff,c080ff|matrix:001000,00ff40,004010,80ff80|fire:200000,ff2000,ff8000,ffd000|aurora:00ff80,00a0ff,8000ff,00ffc0|cyber:ff00ff,00ffff,ffff00,ff0080|pastel:ffb0b0,b0ffb0,b0b0ff,ffffb0".split('|').forEach(function(x){x=x.split(':');PN.push(x[0]);PAL[x[0]]=x[1].split(',')});PN.push('custom');
function grad(c){return'linear-gradient(90deg,#'+c.join(',#')+')'}
mk('lq',PN,function(v){L('ledPalSel',v,'pal')});
[].forEach.call($('lq').children,function(b){var i=document.createElement('i'),v=b.dataset.v;i.style.background=v=='auto'?'repeating-linear-gradient(45deg,#bbb 0 4px,#fff 4px 8px)':PAL[v]?grad(PAL[v]):'#fff';b.className='sw';b.prepend(i)});
var QC=['ff0000','ff5a00','ffb000','ffe600','60ff00','00ff50','00ffd0','00a0ff','0030ff','8a00ff','ff00b0','ffc890'];
mk('lw',QC,function(v){L('ledC1',v,'c1')},QC.map(function(){return''}));
[].forEach.call($('lw').children,function(b){b.style.background='#'+b.dataset.v;b.title='#'+b.dataset.v});
mk('gP',['chill','normal','fast'],function(v){L('ledPace',v,'pace')});
mk('gM',['off','rare','often'],function(v){set('motion',v);g('/bot/motion?level='+v)});
mk('gF',['off','auto','on'],function(v){set('partySet',v);g('/bot/party?set='+v)},['Off','Auto visits','Always']);
mk('gK',['off','some','wild'],function(v){set('partyStack',v);g('/bot/party?stack='+v)});
mk('gS',['face','info','party'],function(v){
 g(v=='info'?'/bot/info?v=1':v=='party'?'/bot/party?v=1':S.mode=='party'?'/bot/party?v=0':'/bot/info?v=0');
 if(v!='info')set('mode',v)},['Face','Time / Wx','Party']);
mk('fp',FP,function(v){g('/bot/party?seq='+v)});
mk('ft',['reach','feeler','flick','arm'],function(v){g('/bot/party?hero='+v)});
mk('mv',['tilt','wobble','pendulum','drift','spin','spin2','flip'],function(v){g('/bot/motion?move='+v)});

function sl(id,url,f){var e=$(id),t=0;
 e.oninput=function(){D[id]=1;$(id+'v').textContent=(f||pc)(e.value);if(!t)t=setTimeout(function(){t=0;fetch(url+e.value).catch(function(){})},150)};
 e.onchange=function(){clearTimeout(t);t=0;D[id]=0;g(url+e.value)}}
sl('lb','/bot/leds?bri=');sl('co','/contrast?v=');sl('ls','/bot/leds?speed=',String);
function cp(id,k){var e=$(id),t=0,K='led'+k.toUpperCase(),h=function(){return e.value.slice(1)};
 e.oninput=function(){D[id]=1;S[K]=h();if(!t)t=setTimeout(function(){t=0;fetch('/bot/leds?'+k+'='+h()).catch(function(){})},200)};
 e.onchange=function(){clearTimeout(t);t=0;D[id]=0;L(K,h(),k)}}
cp('k0','c1');cp('k1','c1');cp('k2','c2');cp('k3','c3');
function hx(c){return/^[0-9a-f]{6}$/i.test(c||'')?c.toLowerCase():'000000'}

function say(){var e=$('t'),v=e.value.trim();if(!v)return;g('/bot/say?text='+encodeURIComponent(v));$('sh').textContent='Said: “'+v+'”';e.value=''}
function snow(){set('snow',!S.snow);g('/bot/snow?v='+(S.snow?1:0))}
function tg(id){var b=$(id),sh=b.classList.toggle('shut');b.previousElementSibling.classList.toggle('shut',sh);
 try{var o=JSON.parse(localStorage.vm||'{}');o[id]=sh;localStorage.vm=JSON.stringify(o)}catch(e){}}
try{var o=JSON.parse(localStorage.vm||'{}');for(var k in o){var b=$(k);if(b){b.classList.toggle('shut',o[k]);b.previousElementSibling.classList.toggle('shut',o[k])}}}catch(e){}

function partyText(){var s=S,e=(Date.now()-T0)/1000|0;
 if(s.mode=='party')return s.visitLeftS>=0?'Partying · back to his face in '+mm(s.visitLeftS-e):'Party on · staying';
 if(s.partySet=='off')return'Visits off';
 if(s.partySet=='on')return'Always partying';
 return s.nextVisitS>=0?'Next visit in '+mm(s.nextVisitS-e):'Next visit waits for his face';}
function chip(c,k,v,x){return'<span class="chip '+c+'"><b>'+k+'</b>'+esc(v)+(x||'')+'</span>'}
function tick(){if(!T0)return;var s=S,f=partyText(),M={face:['cy','face'],info:['cb',s.info||'time / wx'],party:['ck','party']}[s.mode]||['','?'];
 $('fx').textContent=f;
 $('ch').innerHTML=chip(M[0],'showing',M[1])+(s.mode=='face'?chip('','face',X[s.expr]||s.expr):'')+
  chip('cp','lights',s.leds=='ambient'?'ambient · '+s.ledPat:s.leds,s.leds=='solid'?'<i class=cd style="background:#'+hx(s.ledC1)+'"></i>':'')+
  chip('co','party',s.partySet=='on'?'always':s.mode!='party'&&s.nextVisitS>=0?'auto · '+mm(s.nextVisitS-((Date.now()-T0)/1000|0)):s.partySet)+
  chip('cg','motion',s.motion)}
function kv(k,v){return'<div class=kv><span>'+k+'</span><span>'+esc(v)+'</span></div>'}
function sig(r){return!r?0:r>=-55?4:r>=-65?3:r>=-75?2:1}
function up(){var s=S;if(!T0)return;
 hi('ex',s.expr);hi('gL',s.leds);hi('gM',s.motion);hi('gF',s.partySet);hi('gK',s.partyStack);hi('gS',s.mode);hi('fp',s.mode=='party'?s.partyPat:'');hi('mv',s.move);
 hi('lp',s.ledPatSel);hi('lq',s.ledPalSel);hi('gP',s.ledPace);hi('lw',hx(s.ledC1));
 $('sn').className='tog'+(s.snow?' on':'');
 if(!D.lb)$('lb').value=s.ledBri;if(!D.co)$('co').value=s.contrast;if(!D.ls&&s.ledSpeed)$('ls').value=s.ledSpeed;
 $('lbv').textContent=pc($('lb').value);$('cov').textContent=pc($('co').value);$('lsv').textContent=$('ls').value;
 ['k0','k1','k2','k3'].forEach(function(id,i){if(!D[id])$(id).value='#'+hx(s['ledC'+(i||1)])});
 $('lq').lastChild.firstChild.style.background=grad([hx(s.ledC1),hx(s.ledC2),hx(s.ledC3)]);
 $('la').style.display=s.leds=='ambient'?'':'none';$('lo').style.display=s.leds=='solid'?'':'none';$('lc').style.display=s.ledPalSel=='custom'?'':'none';
 var P={chill:[20,12],normal:[10,7],fast:[5,3.5]}[s.ledPace]||[10,7];
 $('lph').textContent=s.ledPatSel!='auto'&&s.ledPalSel!='auto'?'Both locked, so nothing auto-changes.':'Auto picks a new '+(s.ledPatSel=='auto'?'pattern every ~'+P[0]+' s':'')+(s.ledPatSel=='auto'&&s.ledPalSel=='auto'?' and ':'')+(s.ledPalSel=='auto'?'palette every ~'+P[1]+' s':'')+'.';
 $('lh').textContent={mood:'Colour and motion follow his face.',ambient:'Now: '+s.ledPat+' on '+s.ledPal+'.',solid:'One steady colour: #'+hx(s.ledC1).toUpperCase()+'.',off:'Lights off. Spark still works.'}[s.leds]||'';
 $('mh').textContent={off:'Face stays still.',rare:'A move every 2–6 min.',often:'Moves a lot.'}[s.motion]+(s.move&&s.move!='none'?' Now: '+s.move+' ('+Math.round(s.angle)+'°)':'');
 $('shh').textContent=s.mode=='info'?'Tap Time / Wx again to flip between time and weather.':'Time and weather flip every 30 s.';
 var n=sig(s.rssi);[].forEach.call($('sb').children,function(b,i){b.classList.toggle('on',i<n)});
 $('hv').textContent='v'+s.fw;
 $('pf').innerHTML=kv('Frame',s.frameMs+' ms')+kv('Face render',s.faceUs+' µs')+kv('Party avg / max',s.partyUs+' / '+s.partyMaxUs+' µs')+
  kv('Panel send',s.sendUs+' µs')+kv('Party pattern',s.mode=='party'?s.partyPat+' · '+s.tents+' tentacles · stack '+s.partyStack:'—')+kv('Panel',s.panel);
 $('sy').innerHTML=kv('Firmware','v'+s.fw)+kv('Address',s.ip)+kv('Network',s.ap?'Setup hotspot':'WiFi')+kv('Signal',s.rssi?s.rssi+' dBm ('+['','weak','ok','good','strong'][n]+')':'—');
 tick();net()}
function net(){var ok=fails<2&&T0;$('cd').className='dot '+(ok?'on':fails>1?'err':'');
 $('cl').textContent=ok?(S.ap?'Hotspot':'Online'):fails>1?'Offline':'Connecting';
 var st=$('st');st.className='status'+(fails>1?' err':'');st.textContent=fails>1?'vizMini isn\'t answering · retrying':ok?'Connected to vizMini · '+S.ip:'Connecting to vizMini…'}

function soon(d){clearTimeout(pt);pt=setTimeout(poll,Math.max(d,2000-(Date.now()-LP)))}
function poll(){if(document.hidden){soon(2500);return}if(pb)return;pb=1;LP=Date.now();
 fetch('/state',{cache:'no-store'}).then(function(r){return r.json()}).then(function(s){S=s;T0=Date.now();fails=0;up()})
 .catch(function(){fails++;net()}).then(function(){pb=0;soon(2500)})}

var cx=$('cv').getContext('2d'),im=cx.createImageData(128,64);
function scr(){if(!LV||sb||document.hidden)return;sb=1;
 fetch('/debug/screen',{cache:'no-store'}).then(function(r){return r.arrayBuffer()}).then(function(a){var b=new Uint8Array(a),d=im.data;if(b.length<1024)return;
  for(var p=0;p<8;p++)for(var x=0;x<128;x++){var v=b[p*128+x];for(var k=0;k<8;k++){var i=((p*8+k)*128+x)*4,o=v>>k&1;d[i]=o?214:5;d[i+1]=o?240:8;d[i+2]=o?255:11;d[i+3]=255}}
  cx.putImageData(im,0,0)}).catch(function(){}).then(function(){sb=0})}
function live(){LV=!LV;var b=$('lv');b.classList.toggle('on',LV);b.innerHTML=LV?'&#9679; Live':'Paused';if(LV)scr()}
document.addEventListener('visibilitychange',function(){if(!document.hidden){soon(0);scr()}});
setInterval(scr,1000);setInterval(tick,1000);poll();
</script></body></html>
)HTML";
