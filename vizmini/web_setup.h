// vizMini setup page (served at "/" on the hotspot) and firmware update page
// (/update), in the same house style as the control page (web_page.h): yellow
// header bar, cream cards with hard black shadows, square buttons.
// Setup scans for networks (/wifi/scan), test-joins the one you pick
// (/wifi/try) and polls /wifi/status; the bot only saves a network once it has
// joined it, then restarts onto it. Self-contained (no external fonts/libs).
#pragma once
#include <Arduino.h>

#define VZ_BASE_CSS R"CSS(
*{box-sizing:border-box;margin:0;padding:0}[hidden]{display:none!important}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;background:#e8e4dc;color:#000;min-height:100vh}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:#FFD23F;border:3px solid #000;box-shadow:0 5px 0 0 #000}
.logo{font-size:20px;font-weight:800;letter-spacing:-.5px}
.dn{color:#333;font-size:12px;margin-left:10px;font-weight:600}
.hr{display:flex;align-items:center;gap:6px}
.dot{width:10px;height:10px;border-radius:50%;border:2px solid #000;background:#ddd;display:inline-block}.dot.on{background:#88D498}
.sl{color:#333;font-size:11px;font-weight:600}
.wrap{display:flex;flex-direction:column;gap:12px;padding:16px;max-width:560px;margin:8px auto 0}
.card{background:#fffdf5;border:3px solid #000;box-shadow:5px 5px 0 0 #000;padding:16px}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.08em;margin-bottom:12px;font-weight:800;display:flex;justify-content:space-between;align-items:center}
button{background:#e8e8e8;border:2px solid #000;box-shadow:3px 3px 0 0 #000;color:#000;padding:10px 4px;border-radius:0;font-family:inherit;font-size:12px;font-weight:600;cursor:pointer;transition:transform .1s,box-shadow .1s}
button:hover{transform:translate(-1px,-1px);box-shadow:4px 4px 0 0 #000}
button:active{transform:translate(2px,2px);box-shadow:none}
button:disabled{opacity:.45;cursor:default;transform:none;box-shadow:3px 3px 0 0 #000}
.go{background:#B8A9FA;width:100%;margin-top:16px;padding:13px;font-size:15px;font-weight:800}
.sm{font-size:11px;padding:3px 10px;box-shadow:2px 2px 0 0 #000}
.inp{width:100%;padding:10px 12px;border:2px solid #000;box-shadow:3px 3px 0 0 #000;background:#fff;color:#000;font-size:16px;border-radius:0;font-family:inherit}
.inp:focus{outline:none;box-shadow:3px 3px 0 0 #74B9FF}
.row{display:flex;gap:8px;align-items:center}.f1{flex:1;min-width:0}
.lbl{display:block;font-size:13px;color:#555;font-weight:600;margin:14px 0 6px}
.hint{font-size:12px;color:#777;margin-top:10px;line-height:1.45}
.err{font-size:13px;color:#c00;font-weight:700;margin-top:10px}
.kv{display:flex;justify-content:space-between;gap:12px;padding:6px 0;font-size:13px;border-bottom:1px solid #eee}
.kv span:first-child{color:#666;font-weight:600}.kv span:last-child{font-weight:700;overflow-wrap:anywhere;text-align:right}
.btn{display:inline-block;padding:7px 12px;background:#FFD23F;border:2px solid #000;box-shadow:2px 2px 0 0 #000;font-weight:700;font-size:11px;text-transform:uppercase;color:#000;text-decoration:none}
.big{font-size:22px;font-weight:800;letter-spacing:-.5px;line-height:1.15;overflow-wrap:anywhere}
.ok{background:#88D498}.bad{background:#FF8A8A}.mid{background:#FFD23F}
.bar{height:16px;border:2px solid #000;background:#fff;margin-top:16px;overflow:hidden;position:relative}
.bar i{position:absolute;top:0;bottom:0;left:0;width:0;background:#B8A9FA;border-right:2px solid #000;transition:width .2s}
.bar.run i{width:34%;background:#000;border:0;animation:ink 1.15s ease-in-out infinite}
@keyframes ink{0%{left:-34%}100%{left:100%}}
)CSS"

static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>vizMini setup</title><style>)HTML" VZ_BASE_CSS R"HTML(
.nets{display:flex;flex-direction:column;gap:8px}
.net{display:flex;align-items:center;gap:10px;width:100%;padding:12px;text-align:left;font-size:15px;background:#fff}
.net.on{background:#FFD23F}
.nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:700}
.tag{font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.05em;border:2px solid #000;padding:1px 5px;background:#B8A9FA;flex:none}
.lk{width:11px;height:14px;flex:none}
.bars{display:inline-flex;align-items:flex-end;gap:2px;flex:none}
.bars i{width:5px;border:1px solid #000;background:#fff}.bars i.on{background:#000}
.oth{background:#e8e8e8;font-weight:600;font-size:14px}
.steps{margin:14px 0 0 20px;font-size:14px;line-height:1.6;font-weight:500}
.host{display:block;margin-top:14px;padding:12px;border:2px solid #000;box-shadow:3px 3px 0 0 #000;background:#fff;font-size:18px;font-weight:800;text-align:center;color:#000;text-decoration:none}
.rc .hint{color:#000b}
</style></head><body>
<header class=hdr><div class=row style="gap:0"><span class=logo>vizMini</span><span class=dn>setup</span></div>
<div class=hr><span class="dot on"></span><span class=sl id=ap>Hotspot</span></div></header>

<div class=wrap>
<div class=card id=pick><h2>Your WiFi <button class=sm id=rb onclick=scan(1)>&#8635; Rescan</button></h2>
<div class=nets id=nl></div>
<div class=hint id=sc>Looking for networks&hellip;</div>
<form id=fm hidden onsubmit="go();return false" autocomplete=off>
<div id=ob hidden><label class=lbl for=ss>Network name</label>
<input id=ss class=inp maxlength=32 autocapitalize=none autocorrect=off spellcheck=false></div>
<div id=pb><label class=lbl for=pw>Password</label>
<div class=row><input id=pw class="inp f1" type=password maxlength=63 autocapitalize=none autocorrect=off spellcheck=false>
<button type=button class=sm id=sb onclick=shw() style="padding:10px 12px;box-shadow:3px 3px 0 0 #000">Show</button></div></div>
<div class=hint id=op hidden>Open network, no password needed.</div>
<div class=err id=fe hidden></div>
<button class=go>Connect</button>
<div class=hint>He tries it first and only keeps it once it works.</div>
</form></div>

<div class=card id=jn hidden><h2>Connecting</h2>
<div class=big id=jt></div><div class="bar run"><i></i></div>
<div class=hint>Takes about 10 seconds. His face shows it too.</div></div>

<div class="card rc" id=rc hidden></div>

<div class=card><h2>This bot</h2>
<div class=kv><span>Hotspot</span><span id=apn>&ndash;</span></div>
<div class=kv><span>Firmware</span><span id=fw>&ndash;</span></div>
<div class=row style="margin-top:14px"><a class=btn href=/update>&#8593; Firmware update</a></div></div>
</div>

<script>
var N=[],SEL=-1,SS='',ST={},sc=0,fails=0,lost=0;
var LK='<svg class=lk viewBox="0 0 11 14"><rect x=0 y=6 width=11 height=8 fill="#000"/><path d="M2.5 6.5V4a3 3 0 0 1 6 0v2.5" fill=none stroke="#000" stroke-width=2 /></svg>';
function $(i){return document.getElementById(i)}
function esc(t){return String(t).replace(/[&<>"]/g,function(c){return'&#'+c.charCodeAt(0)+';'})}
function J(u,o){return fetch(u,o).then(function(r){return r.json()})}
function bars(r){var n=r>-60?4:r>-67?3:r>-75?2:1,h='';for(var i=0;i<4;i++)h+='<i style="height:'+(5+i*3)+'px"'+(i<n?' class=on':'')+'></i>';return'<span class=bars>'+h+'</span>'}
function show(id){['pick','jn','rc'].forEach(function(k){$(k).hidden=k!=id});window.scrollTo(0,0)}

function info(){J('/wifi/status').then(function(s){ST=s;$('ap').textContent=s.ap;$('apn').textContent=s.ap;$('fw').textContent='v'+s.fw;
 if(s.state=='trying'){$('jt').textContent='Joining '+s.ssid;show('jn');poll()}}).catch(function(){setTimeout(info,1500)})}

function scan(fresh){$('rb').disabled=true;$('sc').hidden=false;$('sc').textContent='Looking for networks…';
 if(fresh){$('nl').innerHTML='';sc=0}
 J('/wifi/scan'+(fresh?'?fresh=1':'')).then(function(j){
  if(j.scanning){if(++sc<25)return setTimeout(scan,700);j.nets=[]}
  sc=0;$('rb').disabled=false;list(j)
 }).catch(function(){if(++sc<25)setTimeout(scan,1000);else{$('rb').disabled=false;list({nets:[]})}})}

function list(j){N=j.nets||[];var h='';
 N.forEach(function(n,i){h+='<button class=net data-i='+i+' onclick=pick('+i+')><span class=nm>'+esc(n.s)+'</span>'+(n.s==j.saved?'<span class=tag>saved</span>':'')+(n.l?LK:'')+bars(n.r)+'</button>'});
 h+='<button class="net oth" data-i=-2 onclick=pick(-2)><span class=nm>Other network&hellip;</span></button>';
 $('nl').innerHTML=h;
 $('sc').textContent=N.length?'':'No networks found. Rescan, or type the name with Other network.';$('sc').hidden=!!N.length;
 var k=-1;N.forEach(function(n,i){if(k<0&&n.s==(SS||j.saved))k=i});if(k>=0)pick(k,1)}

function pick(i,quiet){SEL=i;[].forEach.call($('nl').children,function(b){b.classList.toggle('on',+b.dataset.i==i)});
 var other=i==-2,open=!other&&!N[i].l;
 $('fm').hidden=false;$('ob').hidden=!other;$('pb').hidden=open;$('op').hidden=!open;$('fe').hidden=true;
 if(!quiet){var el=other?$('ss'):open?null:$('pw');if(el)el.focus({preventScroll:true});$('fm').scrollIntoView({behavior:'smooth',block:'nearest'})}}

function shw(){var p=$('pw'),t=p.type=='password';p.type=t?'text':'password';$('sb').textContent=t?'Hide':'Show'}
function fe(t,el){$('fe').textContent=t;$('fe').hidden=false;if(el)el.focus()}

function go(){var other=SEL==-2,ssid=other?$('ss').value.trim():N[SEL].s,pass=$('pw').value,lock=other||N[SEL].l;
 if(!ssid)return fe('Type the network name.',$('ss'));
 if(lock&&!other&&pass.length<8)return fe('WiFi passwords are at least 8 characters.',$('pw'));
 SS=ssid;$('jt').textContent='Joining '+ssid;show('jn');fails=0;lost=0;
 fetch('/wifi/try',{method:'POST',body:new URLSearchParams({ssid:ssid,pass:lock?pass:''})}).then(function(){setTimeout(poll,900)}).catch(function(){setTimeout(poll,900)})}

function poll(){J('/wifi/status').then(function(s){fails=0;
  if(s.state=='trying')return setTimeout(poll,800);
  if(s.state=='ok')return done(s,0);
  if(s.state=='fail')return bad(s);
  if(lost)return; show('pick')
 }).catch(function(){if(++fails==4){lost=1;done({ssid:SS,host:ST.host},1)}if(fails<60)setTimeout(poll,fails<4?1000:2500)})}

function done(s,maybe){var r=$('rc'),u='http://'+s.host+'/';r.className='card rc '+(maybe?'mid':'ok');
 r.innerHTML='<h2>'+(maybe?'Almost there':'Connected')+'</h2><div class=big>'+(maybe?'He&rsquo;s probably on '+esc(s.ssid):'He&rsquo;s on '+esc(s.ssid)+'!')+'</div>'+
 (maybe?'<div class=hint>Your phone dropped off his hotspot while he joined. That usually means it worked.</div>':'')+
 '<ol class=steps><li>Switch your phone back to <b>'+esc(s.ssid)+'</b></li><li>Open his page:</li></ol><a class=host href="'+u+'">'+esc(s.host)+'</a>'+
 '<div class=hint>'+(s.ip?'Or <b>'+esc(s.ip)+'</b>. ':'')+(maybe?'If he isn&rsquo;t there in a minute, join <b>'+esc(ST.ap)+'</b> again and retry.':'He restarts in a few seconds and shows his address on screen.')+'</div>';
 show('rc')}

function bad(s){var r=$('rc');r.className='card rc bad';
 r.innerHTML='<h2>Didn&rsquo;t connect</h2><div class=big>'+esc(s.why)+'</div><div class=hint>Nothing was saved. Check the details and try again.</div><button class=go onclick=again()>Try again</button>';
 show('rc')}
function again(){show('pick');$('pw').value='';pick(SEL)}

['pw','ss'].forEach(function(k){$(k).oninput=function(){$('fe').hidden=true}});
info();scan(0);
</script></body></html>)HTML";

static const char UPDATE_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>vizMini firmware</title><style>)HTML" VZ_BASE_CSS R"HTML(
.drop{display:block;margin-top:14px;border:2px dashed #000;background:#fff;padding:20px 12px;text-align:center;cursor:pointer}
.drop:hover,.drop.ov{background:#fff7d1}.drop b{display:block;font-size:15px;overflow-wrap:anywhere}
.drop .hint{margin-top:4px}
.rc{margin-top:16px;padding:12px;border:2px solid #000;font-size:14px;font-weight:700}
</style></head><body>
<header class=hdr><div class=row style="gap:0"><span class=logo>vizMini</span><span class=dn>firmware</span></div>
<a class=btn href=/ style="background:#fff">&larr; Back</a></header>

<div class=wrap><div class=card><h2>Firmware update</h2>
<div class=kv><span>Running</span><span id=ver>&ndash;</span></div>
<label class=drop id=dz for=f><b id=fn>Choose firmware.bin</b><div class=hint id=fz>From .pio/build/c3-oled/ after a build</div></label>
<input type=file id=f accept=".bin" hidden>
<button class=go id=b onclick=up() disabled>Flash firmware</button>
<div class=bar id=pg hidden><i id=pi></i></div>
<div class=rc id=msg hidden></div>
</div></div>

<script>
var FL=null;
function $(i){return document.getElementById(i)}
function M(t,c){var m=$('msg');m.textContent=t;m.className='rc '+(c||'');m.hidden=!t}
function ver(){return fetch('/state').then(function(r){return r.json()}).then(function(s){$('ver').textContent='v'+s.fw;return s.fw})}
ver().catch(function(){});
function take(fl){FL=null;$('b').disabled=true;M('');if(!fl)return;
 $('fn').textContent=fl.name;$('fz').textContent=(fl.size/1048576).toFixed(2)+' MB';
 if(!/\.bin$/i.test(fl.name))return M('Pick a .bin file.','bad');
 if(fl.size>0x1E0000)return M('Too big for his app space (1.88 MB max).','bad');
 fl.slice(0,1).arrayBuffer().then(function(a){if(new Uint8Array(a)[0]!=0xE9)return M('That isn’t an ESP32 firmware image.','bad');FL=fl;$('b').disabled=false})}
$('f').onchange=function(){take(this.files[0])};
var dz=$('dz');['dragenter','dragover'].forEach(function(e){dz.addEventListener(e,function(v){v.preventDefault();dz.classList.add('ov')})});
['dragleave','drop'].forEach(function(e){dz.addEventListener(e,function(v){v.preventDefault();dz.classList.remove('ov')})});
dz.addEventListener('drop',function(v){take(v.dataTransfer.files[0])});
function up(){if(!FL)return;$('b').disabled=true;$('f').disabled=true;$('pg').hidden=false;M('Uploading…','mid');
 var fd=new FormData();fd.append('firmware',FL);var x=new XMLHttpRequest();x.open('POST','/update');
 x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);$('pi').style.width=p+'%';M('Uploading… '+p+'%','mid')}};
 x.onload=function(){if(x.status==200&&x.responseText.slice(0,2)=='OK'){$('pi').style.width='100%';M('Flashed. He’s restarting…','ok');back(0)}
  else{M('Failed: '+x.responseText,'bad');$('b').disabled=false;$('f').disabled=false}};
 x.onerror=function(){M('Lost the connection. If the upload finished he’s restarting; reload in a few seconds.','bad')};
 x.send(fd)}
function back(n){setTimeout(function(){ver().then(function(v){M('Back up on v'+v+'.','ok')}).catch(function(){if(n<30)back(n+1);else M('He hasn’t come back yet. Check his screen, then reload.','bad')})},n?1500:4000)}
</script></body></html>)HTML";
