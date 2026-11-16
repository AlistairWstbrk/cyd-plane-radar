#pragma once
// HTML/CSS/JS for the portal. Served from flash.
//
// One stylesheet (/pr.css) and one header script (kHead) are shared by our own
// pages and by WiFiManager's pages (Wi-Fi setup, info, update), so every page
// gets the same header, navigation, buttons and cards.

namespace web::pages {

static const char kCss[] = R"CSS(
:root{--bg:#08111f;--card:#0f1b2e;--card2:#13223a;--line:#22365a;--tx:#e9eef7;--dim:#8d9dba;--ac:#3ddc84;--pri:#2f9bff;--dg:#ff5a5a;--warn:#ffb020;--r:12px}
*{box-sizing:border-box}
html{background:var(--bg)}
body{background:linear-gradient(180deg,#0c1a31 0,var(--bg) 320px) no-repeat,var(--bg);color:var(--tx);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;margin:0;-webkit-text-size-adjust:100%}
.wrap{max-width:560px;margin:0 auto;padding:12px 16px 96px;text-align:left;display:block;min-width:0}
.wrap>h1,.wrap>h3,.wrap>br,form>br,.pr-sec>br{display:none}
#showpass{margin:2px 8px 12px 0}#showpass+label{display:inline;color:var(--dim)}
form[action*='refresh'] button{background:transparent;box-shadow:inset 0 0 0 1px var(--line);color:var(--tx)}
.pr-sec>div>a[data-ssid]{margin-bottom:6px}
.hd{display:flex;align-items:center;justify-content:space-between;padding:6px 0 10px}
.brand{display:flex;align-items:center;gap:10px;color:var(--tx);font-weight:700;font-size:1.2rem;letter-spacing:.03em;text-decoration:none}
.brand:before{content:"";width:22px;height:22px;border-radius:50%;border:2px solid var(--ac);background:conic-gradient(from 30deg,rgba(61,220,132,.8),transparent 30%)}
.tabs{display:flex;background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:4px;gap:4px;margin-bottom:14px}
.tabs a{flex:1;text-align:center;padding:8px 4px;border-radius:9px;color:var(--dim);font-weight:600;font-size:.92rem;text-decoration:none}
.tabs a.on{background:var(--card2);color:var(--tx);box-shadow:inset 0 0 0 1px var(--line)}
h2{font-size:1.02rem;margin:0 0 .7rem;display:flex;align-items:center;gap:8px}
.card,.pr-sec{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:14px;margin:0 0 12px}
.pr-sec h3{margin:0 0 .3rem;font-size:1rem;color:var(--tx);display:block}
.pr-hint,small{color:var(--dim);font-size:.85rem}
.pr-hint{margin:.1rem 0 .7rem}
label{display:block;color:var(--dim);font-size:.85rem;margin:2px 0 4px}
input,select,textarea{background:#0a1426;border:1px solid var(--line);color:var(--tx);border-radius:10px;padding:11px 12px;font:inherit;width:100%;margin:0 0 10px}
input[type=checkbox],input[type=radio]{width:auto;margin:0 8px 0 0;accent-color:var(--ac)}
input[type=range]{padding:0;accent-color:var(--ac);background:none;border:0}
input:focus,select:focus,textarea:focus{outline:2px solid var(--pri);border-color:transparent}
button,input[type=submit],.btn{background:var(--pri);border:0;border-radius:10px;color:#fff;font:600 1rem/2.75rem system-ui,sans-serif;width:100%;cursor:pointer;display:block;text-align:center;text-decoration:none;padding:0 14px;margin:0}
button.S{background:var(--pri)}
button.G,.btn.G{background:transparent;box-shadow:inset 0 0 0 1px var(--line);color:var(--tx)}
button.D{background:var(--dg)}
button.sm{width:auto;line-height:2.1rem;font-size:.88rem;padding:0 12px}
button:disabled{opacity:.45}
a{color:var(--pri);font-weight:600;text-decoration:none}
hr{border:0;border-top:1px solid var(--line);margin:14px 0}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1 1 120px}
.tg{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-top:1px solid var(--line)}
.tg span{font-size:.95rem}
.sw{position:relative;width:46px;height:26px;flex:none;margin:0}
.sw input{opacity:0;width:0;height:0;margin:0;position:absolute}
.sw i{position:absolute;inset:0;background:#2a3a57;border-radius:20px;transition:.2s}
.sw i:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#cfd8e6;border-radius:50%;transition:.2s}
.sw input:checked+i{background:var(--ac)}
.sw input:checked+i:before{transform:translateX(20px);background:#fff}
.pill{display:inline-block;padding:3px 10px;border-radius:20px;background:var(--card2);color:var(--dim);font-size:.78rem;font-weight:600;border:1px solid var(--line)}
.pill.ok{color:var(--ac);border-color:rgba(61,220,132,.4)}.pill.warn{color:var(--warn);border-color:rgba(255,176,32,.4)}
.ok{color:var(--ac)}.bad{color:var(--dg)}
.sr{display:flex;gap:12px;align-items:center;padding:10px 4px;border-top:1px solid var(--line);cursor:pointer}
.sr:hover{background:var(--card2)}.sr b{color:var(--ac);min-width:48px}.sr span{flex:1;line-height:1.3}.sr em{color:var(--dim);font-style:normal;font-size:.85rem}
.pick{background:var(--card2);border:1px dashed var(--line);border-radius:10px;padding:10px 12px;color:var(--dim);margin:2px 0 6px}
.pick.on{border-style:solid;color:var(--tx)}.pick.on b{color:var(--ac);margin-right:6px}
.pick small{display:block}
.pick.err{border-color:var(--dg);color:var(--dg)}
.tiles{display:grid;gap:10px;margin:0 0 12px}
.tile{display:block;background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:14px;color:var(--tx);text-decoration:none}
.tile b{display:block;font-size:1rem}.tile small{display:block;margin-top:2px}
.tile:hover{border-color:var(--pri)}
.wrap>form{margin:0 0 10px}
form[action='/info'] button,form[action='/update'] button,form[action='/restart'] button,form[action='/exit'] button,form[action='/erase'] button{background:transparent;box-shadow:inset 0 0 0 1px var(--line);color:var(--tx)}
form[action='/restart'] button{color:var(--dg)}
a[data-ssid]{display:block;color:var(--tx);padding:12px;background:var(--card);border:1px solid var(--line);border-radius:10px;margin:0 0 6px}
.q{float:right;margin:14px 12px 0 0;filter:invert(1)}
.msg{background:var(--card);border:1px solid var(--line);border-left:4px solid var(--pri);border-radius:10px;padding:12px 14px;margin:12px 0;color:var(--tx)}
.msg.S{border-left-color:var(--ac)}.msg.D{border-left-color:var(--dg)}
dl{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:12px 14px}dt{color:var(--dim);font-size:.85rem}dd{margin:0 0 .6em}
table{width:100%;border-collapse:collapse;font-size:.88rem}
th{color:var(--dim);font-weight:500;text-align:left;padding:6px 4px;border-bottom:1px solid var(--line)}
td{padding:8px 4px;border-bottom:1px solid var(--line)}
td.r,th.r{text-align:right}
.ap{display:flex;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--line)}
.ap b{color:#c8a8ff;min-width:56px}
.ap .n{flex:1;color:var(--dim);font-size:.85rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bar{height:8px;background:var(--card2);border-radius:6px;overflow:hidden;margin:8px 0}
.bar i{display:block;height:100%;width:0;background:var(--pri);transition:width .3s}
details{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:0 14px;margin:0 0 12px}
details[open]{padding-bottom:12px}
summary{cursor:pointer;padding:14px 0;font-weight:600;list-style:none}
summary:after{content:"+";float:right;color:var(--dim)}details[open] summary:after{content:"-"}
.savebar{position:fixed;left:0;right:0;bottom:0;padding:10px 16px calc(10px + env(safe-area-inset-bottom));background:rgba(8,17,31,.92);border-top:1px solid var(--line);backdrop-filter:blur(6px)}
.savebar button{max-width:528px;margin:0 auto}
.toast{position:fixed;left:50%;bottom:76px;transform:translateX(-50%);background:var(--card2);border:1px solid var(--ac);color:var(--tx);padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none;z-index:9}
.toast.on{opacity:1}
canvas{width:100%;max-width:520px;aspect-ratio:1;display:block;margin:0 auto;border-radius:var(--r)}
)CSS";

// <head> extras for every page (WiFiManager adds it via setCustomHeadElement;
// our pages include it too). Builds the shared header + tabs, renames the
// WiFiManager menu entries and marks the setup steps on the Wi-Fi page.
static const char kHead[] = R"HTML(<link rel="stylesheet" href="/pr.css"><meta name="theme-color" content="#08111f"><script>
document.addEventListener('DOMContentLoaded',function(){var w=document.querySelector('.wrap');if(!w||document.querySelector('.hd'))return;
var p=location.pathname,t=p.indexOf('/settings')==0?1:p.indexOf('/airports')==0?2:p.indexOf('/live')==0?3:0;
var h=document.createElement('div');h.innerHTML="<header class='hd'><a class='brand' href='/'>Plane Radar</a><span class='pill' id='hdst'>&hellip;</span></header><nav class='tabs'><a href='/'>Home</a><a href='/settings'>Radar</a><a href='/airports'>Airports</a><a href='/live'>Live</a></nav>";
w.insertBefore(h,w.firstChild);h.querySelectorAll('nav a')[t].className='on';
var names={'/wifi':'Wi-Fi & home airport','/info':'Device info','/update':'Firmware update','/restart':'Restart'};
document.querySelectorAll('form[action] button').forEach(function(b){var a=b.form.getAttribute('action');if(names[a])b.textContent=names[a]});
if(p=='/wifi'||p=='/0wifi'){var f=document.querySelector("form[action='wifisave']");if(f){var s=document.createElement('div');s.className='pr-sec';s.innerHTML='<h3>Wi-Fi network</h3><p class=pr-hint>Tap your network, then type its password.</p>';
document.querySelectorAll('a[data-ssid]').forEach(function(a){s.appendChild(a.parentNode)});var k=f.firstChild;while(k&&!(k.classList&&k.classList.contains('pr-sec'))){var nx=k.nextSibling;s.appendChild(k);k=nx}f.insertBefore(s,f.firstChild)}}
fetch('/api/state').then(function(r){return r.json()}).then(function(j){var e=document.getElementById('hdst'),o=j.status.online;
e.textContent=(o?'Online':'Setup mode')+(j.home?' · '+j.home.ident:'');e.className='pill '+(o?'ok':'warn');
var tl=document.querySelector('.tiles'),wf=document.querySelector("form[action='/wifi']");if(o&&tl&&wf)wf.parentNode.insertBefore(tl,wf)}).catch(function(){})});
</script>)HTML";

// Extra entries on WiFiManager's home page.
static const char kMenu[] = R"HTML(<div class="tiles">
<a class="tile" href="/settings"><b>Radar settings</b><small>Home airport, range, labels, units, night mode</small></a>
<a class="tile" href="/airports"><b>Airports</b><small>Find airports and add their runways</small></a>
<a class="tile" href="/live"><b>Live traffic</b><small>What the radar sees right now</small></a>
</div>
)HTML";

static const char kShellStart[] = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Plane Radar</title>)HTML";

static const char kShellBody[] = R"HTML(</head><body><div class="wrap"><div class="toast" id="toast"></div><script>
function $(i){return document.getElementById(i)}
function toast(t){var e=$('toast');e.textContent=t;e.classList.add('on');clearTimeout(e._t);e._t=setTimeout(function(){e.classList.remove('on')},2200)}
function api(m,u,b){return fetch(u,{method:m,headers:b?{'Content-Type':'application/json'}:{},body:b?JSON.stringify(b):undefined}).then(function(r){return r.json().then(function(j){if(!r.ok)throw new Error(j.error||r.status);return j})})}
function esc(s){return String(s==null?'':s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function where(a){return esc((a.city?a.city+', ':'')+a.region)}
// Live airport search box: input id, results id, callback(airport)
function finder(inp,res,cb,extra){var t;$(inp).oninput=function(){clearTimeout(t);t=setTimeout(function(){var q=$(inp).value.trim();if(!q){$(res).innerHTML='';return}
api('GET','/api/search?q='+encodeURIComponent(q)).then(function(r){var h='';r.results.slice(0,8).forEach(function(a,i){h+='<div class="sr" data-i="'+i+'"><b>'+esc(a.ident)+'</b><span>'+esc(a.name)+'<br><em>'+where(a)+' &middot; '+a.km+' km</em></span></div>'});
if(!r.results.length)h='<p class="pr-hint">'+(extra||'No match. Try the city or the airport code.')+'</p>';$(res).innerHTML=h;
$(res).querySelectorAll('.sr').forEach(function(el){el.onclick=function(){$(res).innerHTML='';$(inp).value='';cb(r.results[+el.dataset.i])}})})},200)};
$(inp).onkeydown=function(e){if(e.key==='Enter'){e.preventDefault();var f=$(res).querySelector('.sr');if(f)f.click();else if(e.target._enter)e.target._enter()}}}
</script>)HTML";

static const char kShellEnd[] = R"HTML(</div></body></html>)HTML";

// Shown when the login is cancelled or wrong (sent with the 401).
static const char kLoginFail[] = R"HTML(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Plane Radar</title></head><body style="font:16px system-ui,sans-serif;background:#08111f;color:#e6edf6;max-width:420px;margin:12vh auto;padding:0 16px"><h2 style="margin:0 0 8px">Login required</h2><p style="color:#9fb0c6;line-height:1.5">User name <b style="color:#e6edf6">admin</b>. The password is the 8-character code on the radar: open <b style="color:#e6edf6">Settings &gt; Device info</b> on the screen (it is also the setup Wi-Fi password).</p><p><a href="/" style="color:#4fb3ff">Try again</a></p></body></html>)HTML";

// ------------------------------------------------------------- settings ----
static const char kSettings[] = R"HTML(
<div class="card"><h2>Home airport</h2>
<div id="home" class="pick">&hellip;</div>
<input id="hq" placeholder="Change: name, city or code" autocomplete="off"><div id="hres"></div>
<p class="pr-hint" style="margin:0">The radar is centred on this airport. Changes apply straight away.</p></div>

<div class="card"><h2>Display</h2>
<div class="row"><div><label>Range (3rd ring)</label><select id="range_idx"></select></div>
<div><label>Theme</label><select id="theme"><option value="0">Classic</option><option value="1">Amber</option><option value="2">Ice</option><option value="3">Night (red)</option></select></div></div>
<label>Aircraft labels</label><select id="label_mode"><option value="0">Off</option><option value="1">Callsign</option><option value="2">Callsign, type and altitude</option></select>
<div class="tg"><span>Runways</span><label class="sw"><input type="checkbox" id="show_runways"><i></i></label></div>
<div class="tg"><span>Flight trails</span><label class="sw"><input type="checkbox" id="show_trails"><i></i></label></div>
<div class="tg"><span>Colour by altitude</span><label class="sw"><input type="checkbox" id="alt_colors"><i></i></label></div>
<div class="tg"><span>Sweep animation</span><label class="sw"><input type="checkbox" id="sweep"><i></i></label></div>
<div class="tg"><span>Aircraft on the ground</span><label class="sw"><input type="checkbox" id="show_ground"><i></i></label></div>
<div class="tg"><span>Hide aircraft above</span><select id="max_alt_kft" style="width:auto;margin:0"><option value="0">No limit</option><option value="5">5,000 ft</option><option value="10">10,000 ft</option><option value="20">20,000 ft</option><option value="30">30,000 ft</option></select></div>
</div>

<div class="card"><h2>Units</h2><div class="row">
<div><label>Distance</label><select id="dist_units"><option value="0">km</option><option value="1">miles</option><option value="2">nautical miles</option></select></div>
<div><label>Altitude</label><select id="alt_units"><option value="0">feet</option><option value="1">metres</option></select></div>
<div><label>Speed</label><select id="speed_units"><option value="0">knots</option><option value="1">km/h</option><option value="2">mph</option></select></div></div></div>

<div class="card"><h2>Alerts</h2>
<label>Proximity alert</label><select id="alert_km"><option value="0">Off</option><option value="2">Within 2 km</option><option value="3">Within 3 km</option><option value="5">Within 5 km</option><option value="10">Within 10 km</option><option value="20">Within 20 km</option></select>
<div class="tg"><span>Flash the LED on alerts</span><label class="sw"><input type="checkbox" id="alert_led"><i></i></label></div>
<div class="tg"><span>Status LED (blink on each update)</span><label class="sw"><input type="checkbox" id="status_led"><i></i></label></div></div>

<div class="card"><h2>Screen &amp; clock</h2>
<label>Brightness <span id="bv"></span></label><input type="range" id="brightness" min="5" max="100" step="5">
<div class="tg"><span>24-hour clock</span><label class="sw"><input type="checkbox" id="clock_24h"><i></i></label></div>
<div class="tg"><span>Rotate 180&deg;</span><label class="sw"><input type="checkbox" id="flip"><i></i></label></div>
<div class="tg"><span>Invert colours</span><label class="sw"><input type="checkbox" id="invert"><i></i></label></div>
<label style="margin-top:8px">Time zone</label><select id="tzsel"></select>
<input id="tz" placeholder="POSIX TZ string, e.g. CET-1CEST,M3.5.0,M10.5.0/3" style="display:none"></div>

<div class="card"><h2>Night mode</h2>
<div class="tg" style="border:0;padding-top:0"><span>Dim the screen at night</span><label class="sw"><input type="checkbox" id="night_mode"><i></i></label></div>
<div class="row"><div><label>From</label><select id="night_start_h"></select></div><div><label>Until</label><select id="night_end_h"></select></div></div>
<label>Night brightness <span id="nbv"></span></label><input type="range" id="night_brightness" min="5" max="60" step="5">
<div class="tg"><span>Red night colours</span><label class="sw"><input type="checkbox" id="night_theme"><i></i></label></div></div>

<details><summary>Data feed</summary>
<label>Refresh every</label><select id="fetch_s"><option value="3">3 s</option><option value="5">5 s</option><option value="10">10 s</option><option value="15">15 s</option><option value="30">30 s</option></select>
<label>Demo traffic</label><select id="sim"><option value="0">Off (live data)</option><option value="10">10 aircraft</option><option value="40">40 aircraft</option><option value="80">80 aircraft</option></select>
<p class="pr-hint" id="feed" style="margin:0"></p></details>

<p class="pr-hint" style="text-align:center" id="ver"></p>
<div class="savebar"><button onclick="save()">Save changes</button></div>
<script>
var TZ=[["UTC","UTC0"],["London, Dublin, Lisbon","GMT0BST,M3.5.0/1,M10.5.0"],["Amsterdam, Berlin, Paris, Rome","CET-1CEST,M3.5.0,M10.5.0/3"],["Athens, Helsinki, Kyiv","EET-2EEST,M3.5.0/3,M10.5.0/4"],["Moscow, Istanbul","MSK-3"],["Dubai","<+04>-4"],["India","IST-5:30"],["Singapore, Perth","<+08>-8"],["Tokyo, Seoul","JST-9"],["Brisbane","AEST-10"],["Sydney, Melbourne","AEST-10AEDT,M10.1.0,M4.1.0/3"],["Auckland","NZST-12NZDT,M9.5.0,M4.1.0/3"],["Hawaii","HST10"],["Alaska","AKST9AKDT,M3.2.0,M11.1.0"],["US Pacific","PST8PDT,M3.2.0,M11.1.0"],["US Mountain","MST7MDT,M3.2.0,M11.1.0"],["Arizona","MST7"],["US Central","CST6CDT,M3.2.0,M11.1.0"],["US Eastern","EST5EDT,M3.2.0,M11.1.0"],["Sao Paulo","<-03>3"],["Johannesburg","SAST-2"],["Custom...",""]];
var IANA={"Europe/London":1,"Europe/Dublin":1,"Europe/Lisbon":1,"Europe/Amsterdam":2,"Europe/Berlin":2,"Europe/Paris":2,"Europe/Rome":2,"Europe/Madrid":2,"Europe/Brussels":2,"Europe/Vienna":2,"Europe/Zurich":2,"Europe/Stockholm":2,"Europe/Oslo":2,"Europe/Copenhagen":2,"Europe/Warsaw":2,"Europe/Prague":2,"Europe/Athens":3,"Europe/Helsinki":3,"Europe/Kiev":3,"Europe/Kyiv":3,"Europe/Moscow":4,"Europe/Istanbul":4,"Asia/Dubai":5,"Asia/Kolkata":6,"Asia/Singapore":7,"Australia/Perth":7,"Asia/Tokyo":8,"Asia/Seoul":8,"Australia/Brisbane":9,"Australia/Sydney":10,"Australia/Melbourne":10,"Pacific/Auckland":11,"Pacific/Honolulu":12,"America/Anchorage":13,"America/Los_Angeles":14,"America/Denver":15,"America/Phoenix":16,"America/Chicago":17,"America/New_York":18,"America/Toronto":18,"America/Sao_Paulo":19,"Africa/Johannesburg":20};
var B=["show_runways","show_trails","alt_colors","sweep","show_ground","alert_led","status_led","flip","invert","clock_24h","night_mode","night_theme"];
var N=["range_idx","label_mode","theme","max_alt_kft","dist_units","alt_units","speed_units","alert_km","fetch_s","brightness","night_start_h","night_end_h","night_brightness"];
["night_start_h","night_end_h"].forEach(function(k){for(var h=0;h<24;h++){var o=document.createElement('option');o.value=h;o.textContent=(h<10?'0':'')+h+':00';$(k).appendChild(o)}});
$('night_brightness').oninput=function(){$('nbv').textContent=this.value+'%'};
$('brightness').oninput=function(){$('bv').textContent=this.value+'%'};
var sel=$('tzsel');TZ.forEach(function(t,i){var o=document.createElement('option');o.value=i;o.textContent=t[0];sel.appendChild(o)});
sel.onchange=function(){var t=TZ[sel.value];$('tz').style.display=t[1]?'none':'block';if(t[1])$('tz').value=t[1]};
function showHome(a){$('home').className=a?'pick on':'pick';$('home').innerHTML=a?'<b>'+esc(a.ident)+'</b>'+esc(a.name)+'<small>'+where(a)+'</small>':'No airport chosen yet'}
finder('hq','hres',function(a){api('POST','/api/location?ident='+encodeURIComponent(a.ident)).then(function(){showHome(a);var e=document.getElementById('hdst');if(e)e.textContent=e.textContent.replace(/\S+$/,a.ident);toast('Radar centred on '+a.ident)}).catch(function(e){toast(e.message)})});
function load(){api('GET','/api/state').then(function(s){var c=s.settings;showHome(s.home);
$('range_idx').innerHTML='';s.ranges.forEach(function(r,i){var o=document.createElement('option');o.value=i;o.textContent=r;$('range_idx').appendChild(o)});
B.forEach(function(k){$(k).checked=!!c[k]});N.forEach(function(k){$(k).value=c[k]});$('bv').textContent=c.brightness+'%';$('nbv').textContent=c.night_brightness+'%';
$('sim').value=s.status.sim;
var idx=-1;TZ.forEach(function(t,i){if(t[1]===c.tz)idx=i});
if(idx<0){var g=IANA[Intl.DateTimeFormat().resolvedOptions().timeZone];if(c.tz==='UTC0'&&g!==undefined)idx=g;else idx=TZ.length-1}
sel.value=idx;$('tz').value=c.tz;$('tz').style.display=TZ[idx][1]?'none':'block';if(TZ[idx][1])$('tz').value=TZ[idx][1];
var f=s.status;$('feed').innerHTML=(f.online?'<span class=ok>Online</span>':'<span class=bad>Offline</span>')+' &middot; '+f.ok+' updates, '+f.err+' errors &middot; '+f.count+' aircraft';
$('ver').textContent='Firmware '+s.version+' · '+(f.ip||'')})}
function save(){var c={tz:$('tz').value,sim:+$('sim').value};
B.forEach(function(k){c[k]=$(k).checked});N.forEach(function(k){c[k]=+$(k).value});
api('POST','/api/settings',c).then(function(){toast('Saved')}).catch(function(e){toast('Error: '+e.message)})}
load();
</script>)HTML";

// ------------------------------------------------------------- airports ----
static const char kAirports[] = R"HTML(
<div class="card"><h2>Find an airport</h2>
<input id="ident" placeholder="Name, city or code" autocomplete="off"><div id="sres"></div>
<div id="lkst" style="display:none"><div class="bar"><i id="lkbar"></i></div><small id="lkmsg"></small></div>
<div id="prev" style="display:none"><div class="row" style="align-items:center;margin:6px 0 12px"><div><b id="pid" style="font-size:1.3rem;color:#c8a8ff"></b><br><span id="pname"></span><br><small id="pinfo"></small></div>
<div style="flex:0 0 120px"><svg id="psvg" viewBox="0 0 140 140" width="120" height="120" style="background:#0a1426;border-radius:10px"></svg></div></div>
<div class="row"><button onclick="savePrev()">Add to radar</button><button class="G" onclick="savePrev(1)">Add &amp; make home</button></div>
<button class="G" style="margin-top:8px" onclick="hidePrev()">Cancel</button></div>
<p class="pr-hint" id="offline" style="display:none;margin:8px 0 0">Runways are fetched online, so this works once the radar is on Wi-Fi.</p></div>

<div class="card"><h2>Your airports <span class="pill" id="cnt"></span></h2><div id="list"></div>
<p class="pr-hint" style="margin:10px 0 0">About 1,200 major airports are drawn automatically. Airports you add here are drawn in violet.</p></div>

<details><summary>Private strip (manual)</summary>
<p class="pr-hint">For fields that are not in the airport database.</p>
<div class="row"><div><label>Ident</label><input id="mid" maxlength="7" placeholder="MYFLD" style="text-transform:uppercase"></div><div style="flex:2"><label>Name</label><input id="mname" maxlength="39" placeholder="My airfield"></div></div>
<div class="row"><div><label>Latitude</label><input id="mlat" inputmode="decimal"></div><div><label>Longitude</label><input id="mlon" inputmode="decimal"></div></div>
<label>Runways</label><select id="mmode" onchange="mrows()"><option value="c">Centre, heading and length</option><option value="e">Two runway ends</option></select>
<div id="mrw"></div>
<div class="row"><button class="G" onclick="addRow()">+ Runway</button><button onclick="saveManual()">Save strip</button></div></details>

<details><summary>Backup and restore</summary>
<textarea id="json" rows="4" style="font-family:monospace;font-size:.8rem" placeholder="Export, or paste a list to import"></textarea>
<div class="row"><button class="G" onclick="exp()">Export</button><button class="G" onclick="imp()">Import</button></div></details>
<script>
var R=[],preview=null,timer=null;
function list(){api('GET','/api/airports').then(function(j){$('cnt').textContent=j.airports.length+' / '+j.max;
$('offline').style.display=j.online?'none':'block';
var h='';j.airports.forEach(function(a){h+='<div class="ap"><b>'+esc(a.ident)+'</b><div class="n">'+esc(a.name||'Custom')+' &middot; '+a.runways.length+' rwy</div><button class="G sm" onclick="home(\''+a.ident+'\')">Home</button><button class="D sm" onclick="del(\''+a.ident+'\')">Delete</button></div>'});
$('list').innerHTML=h||'<p class="pr-hint" style="margin:0">None yet. Search above to add one.</p>';R=j.airports})}
function del(id){if(!confirm('Remove '+id+' from the radar?'))return;api('POST','/api/airports/delete?ident='+id).then(function(){toast(id+' removed');list()})}
function home(id){api('POST','/api/location?ident='+id).then(function(){toast('Radar centred on '+id)})}
finder('ident','sres',function(a){lookup(a.ident)},'Not in the list. Press Enter to look the code up online.');
$('ident')._enter=function(){lookup($('ident').value.trim().toUpperCase())};
function lookup(id){if(!id)return;hidePrev();$('sres').innerHTML='';
api('POST','/api/lookup?ident='+encodeURIComponent(id)).then(function(){$('lkst').style.display='block';poll()}).catch(function(e){toast(e.message)})}
function poll(){api('GET','/api/lookup').then(function(s){$('lkbar').style.width=s.progress+'%';$('lkmsg').textContent=s.message;
if(s.state==='running'){timer=setTimeout(poll,500);return}
if(s.state==='done'){$('lkst').style.display='none';showPrev(s.result)}else if(s.state==='error'){$('lkmsg').innerHTML='<span class=bad>'+esc(s.message)+'</span>'}})}
function draw(svg,a){var pts=[];a.runways.forEach(function(r){pts.push([r.le_lat,r.le_lon],[r.he_lat,r.he_lon])});
if(!pts.length){svg.innerHTML='<text x="70" y="74" fill="#8d9dba" font-size="11" text-anchor="middle">no runways</text>';return}
var cl=Math.cos(a.lat*Math.PI/180),xs=pts.map(function(p){return(p[1]-a.lon)*cl}),ys=pts.map(function(p){return p[0]-a.lat});
var mnx=Math.min.apply(0,xs),mxx=Math.max.apply(0,xs),mny=Math.min.apply(0,ys),mxy=Math.max.apply(0,ys),sp=Math.max(mxx-mnx,mxy-mny,1e-4),k=110/sp,cx=(mnx+mxx)/2,cy=(mny+mxy)/2,h='';
a.runways.forEach(function(r){var x0=70+((r.le_lon-a.lon)*cl-cx)*k,y0=70-((r.le_lat-a.lat)-cy)*k,x1=70+((r.he_lon-a.lon)*cl-cx)*k,y1=70-((r.he_lat-a.lat)-cy)*k;
h+='<line x1="'+x0+'" y1="'+y0+'" x2="'+x1+'" y2="'+y1+'" stroke="#c8a8ff" stroke-width="4" stroke-linecap="round"/>'});svg.innerHTML=h}
function showPrev(a){preview=a;$('prev').style.display='block';$('pid').textContent=a.ident;$('pname').textContent=a.name;
$('pinfo').textContent=a.runways.length?a.runways.map(function(r){return r.name}).join('  '):'No runway data';draw($('psvg'),a)}
function hidePrev(){$('prev').style.display='none';preview=null}
function savePrev(mk){var pid=preview.ident;api('POST','/api/lookup/save').then(function(){if(mk)return api('POST','/api/location?ident='+encodeURIComponent(pid))}).then(function(){toast(pid+(mk?' added, radar centred here':' added'));hidePrev();list()}).catch(function(e){toast(e.message)})}
var rows=1;function rowHtml(i){return $('mmode').value==='c'?
'<div class="row"><div><label>Runway</label><input id="rn'+i+'" placeholder="09/27"></div><div><label>Heading &deg;</label><input id="rh'+i+'" inputmode="decimal" placeholder="90"></div><div><label>Length m</label><input id="rl'+i+'" inputmode="decimal" placeholder="800"></div><div><label>Offset m</label><input id="ro'+i+'" inputmode="decimal" placeholder="0"></div></div>':
'<div class="row"><div><label>Runway</label><input id="rn'+i+'" placeholder="09/27"></div><div><label>End 1 lat</label><input id="a'+i+'" inputmode="decimal"></div><div><label>End 1 lon</label><input id="b'+i+'" inputmode="decimal"></div><div><label>End 2 lat</label><input id="c'+i+'" inputmode="decimal"></div><div><label>End 2 lon</label><input id="d'+i+'" inputmode="decimal"></div></div>'}
function mrows(){var h='';for(var i=0;i<rows;i++)h+=rowHtml(i);$('mrw').innerHTML=h}
function addRow(){if(rows<8){rows++;mrows()}}
function off(lat,lon,e,n){return[lat+n/111320,lon+e/(111320*Math.cos(lat*Math.PI/180))]}
function saveManual(){var lat=parseFloat($('mlat').value),lon=parseFloat($('mlon').value),rw=[];
for(var i=0;i<rows;i++){var nm=$('rn'+i).value;if($('mmode').value==='c'){var h=parseFloat($('rh'+i).value),l=parseFloat($('rl'+i).value),o=parseFloat($('ro'+i).value)||0;if(isNaN(h)||isNaN(l))continue;
var t=h*Math.PI/180,ex=Math.sin(t),ny=Math.cos(t),px=Math.cos(t),py=-Math.sin(t),a=off(lat,lon,px*o-ex*l/2,py*o-ny*l/2),b=off(lat,lon,px*o+ex*l/2,py*o+ny*l/2);
rw.push({name:nm,le_lat:a[0],le_lon:a[1],he_lat:b[0],he_lon:b[1]})}else{var v=['a','b','c','d'].map(function(k){return parseFloat($(k+i).value)});if(v.some(isNaN))continue;rw.push({name:nm,le_lat:v[0],le_lon:v[1],he_lat:v[2],he_lon:v[3]})}}
var ap={ident:$('mid').value,name:$('mname').value,runways:rw};if(!isNaN(lat)&&!isNaN(lon)){ap.lat=lat;ap.lon=lon}
api('POST','/api/airports',ap).then(function(){toast('Saved '+ap.ident.toUpperCase());list()}).catch(function(e){toast('Error: '+e.message)})}
function exp(){$('json').value=JSON.stringify(R)}
function imp(){var a;try{a=JSON.parse($('json').value)}catch(e){toast('Invalid JSON');return}if(!Array.isArray(a))a=a.airports||[a];
var p=Promise.resolve(),ok=0;a.forEach(function(x){p=p.then(function(){return api('POST','/api/airports',x).then(function(){ok++})}).catch(function(){})});p.then(function(){toast('Imported '+ok);list()})}
mrows();list();
api('GET','/api/lookup').then(function(s){if(s.state==='running'){$('lkst').style.display='block';poll()}else if(s.state==='done'){showPrev(s.result)}});
</script>)HTML";

// ----------------------------------------------------------------- live ----
static const char kLive[] = R"HTML(
<div class="card"><h2>Live traffic <span class="pill" id="sum"></span></h2><canvas id="cv" width="520" height="520"></canvas></div>
<div class="card" style="padding-top:8px"><table><thead><tr><th>Callsign</th><th>Type</th><th class="r">Alt</th><th class="r">Speed</th><th class="r">Dist</th></tr></thead><tbody id="tb"></tbody></table></div>
<script>
var cv=$('cv'),g=cv.getContext('2d');
function rad(d){return d*Math.PI/180}
function render(j){var W=cv.width,C=W/2,R=W*0.44,k=R/j.outer_km;g.fillStyle='#050d1a';g.fillRect(0,0,W,W);
g.strokeStyle='#14532a';g.lineWidth=2;for(var i=1;i<=4;i++){g.beginPath();g.arc(C,C,R*i/4,0,7);g.stroke()}
g.beginPath();g.moveTo(C-R,C);g.lineTo(C+R,C);g.moveTo(C,C-R);g.lineTo(C,C+R);g.stroke();
g.fillStyle='#e9eef7';g.font='bold 18px system-ui';g.textAlign='center';g.fillText('N',C,24);g.fillText('S',C,W-8);g.fillText('E',W-12,C+6);g.fillText('W',12,C+6);
g.font='13px system-ui';g.textAlign='right';g.fillText(j.range,C+R-6,C-6);
j.aircraft.forEach(function(a){var r=a.dist*k;if(r>R)return;var x=C+Math.sin(rad(a.brg))*r,y=C-Math.cos(rad(a.brg))*r,h=rad(a.trk);
g.strokeStyle='#ff00ff';g.beginPath();g.moveTo(x,y);g.lineTo(x+Math.sin(h)*a.gs/12,y-Math.cos(h)*a.gs/12);g.stroke();
g.save();g.translate(x,y);g.rotate(h);g.fillStyle=a.emerg?'#ffcc00':'#ff2a2a';g.beginPath();g.moveTo(0,-11);g.lineTo(7,6);g.lineTo(-7,6);g.fill();g.restore();
g.fillStyle='#fff';g.textAlign=x<C?'left':'right';g.font='bold 13px system-ui';g.fillText(a.call,x+(x<C?12:-12),y-2);g.fillStyle='#5ac8ff';g.font='12px system-ui';g.fillText(a.alt,x+(x<C?12:-12),y+12)});
g.fillStyle='#fff';g.beginPath();g.arc(C,C,3,0,7);g.fill()}
function tick(){api('GET','/api/aircraft').then(function(j){render(j);var h='';j.aircraft.forEach(function(a){h+='<tr><td><b>'+esc(a.call)+'</b>'+(a.emerg?' <span class=bad>'+esc(a.sqk)+'</span>':'')+'</td><td>'+esc(a.type)+'</td><td class="r">'+a.alt+'</td><td class="r">'+a.spd+'</td><td class="r">'+a.dists+'</td></tr>'});
$('tb').innerHTML=h||'<tr><td colspan=5><small>No aircraft in range</small></td></tr>';$('sum').textContent=j.aircraft.length+' aircraft · '+j.range+(j.sim?' · demo':'')}).catch(function(){}).then(function(){setTimeout(tick,2500)})}
tick();
</script>)HTML";

}  // namespace web::pages
