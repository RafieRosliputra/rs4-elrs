// rs4-elrs remote — settings page served at http://192.168.4.1
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="ms"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RS4 Remote</title>
<style>
:root{--bg:#111418;--card:#1b2027;--fg:#e8ecf1;--dim:#8a95a3;--acc:#2fb4ff;--ok:#36d07a;--warn:#ffcf3a;--bad:#ff5a5a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
header{padding:14px 16px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #262d36}
h1{font-size:18px;margin:0}h2{font-size:14px;margin:0 0 10px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em}
main{padding:12px 16px 40px;max-width:560px;margin:auto}
.card{background:var(--card);border-radius:12px;padding:14px;margin:12px 0}
.row{display:flex;align-items:center;gap:10px;margin:8px 0}.row label{flex:0 0 120px;color:var(--dim)}
.row input[type=range]{flex:1}.row output{flex:0 0 46px;text-align:right;font-variant-numeric:tabular-nums}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.grid input{width:100%;padding:8px;border-radius:8px;border:1px solid #333b46;background:#0f1216;color:var(--fg);text-align:center}
select{flex:1;padding:8px;border-radius:8px;background:#0f1216;color:var(--fg);border:1px solid #333b46}
button{padding:10px 12px;border-radius:9px;border:0;background:#2a323d;color:var(--fg);font-weight:600}
button.p{background:var(--acc);color:#002033}
.btns{display:flex;flex-wrap:wrap;gap:8px}
.bar{height:10px;background:#0f1216;border-radius:6px;position:relative;overflow:hidden;flex:1}
.bar i{position:absolute;top:0;bottom:0;background:var(--acc);left:50%;width:0}
.mono{font-variant-numeric:tabular-nums;font-family:ui-monospace,monospace;color:var(--dim)}
.pill{padding:3px 9px;border-radius:999px;font-size:13px;background:#2a323d}
.ok{color:var(--ok)}.bad{color:var(--bad)}
small{color:var(--dim)}
</style></head><body>
<header><h1>RS4 Remote</h1><span id="link" class="pill">...</span></header>
<main>

<div class="card"><h2>Live</h2>
 <div class="row"><label>Pan</label><div class="bar"><i id="b0"></i></div><output id="o0"></output></div>
 <div class="row"><label>Tilt</label><div class="bar"><i id="b1"></i></div><output id="o1"></output></div>
 <div class="row"><label>Zoom</label><div class="bar"><i id="b2"></i></div><output id="o2"></output></div>
 <div class="mono" id="raw"></div>
 <div class="mono" id="rx"></div>
</div>

<div class="card"><h2>Joystick</h2>
 <div class="row"><label>Deadzone</label><input type="range" id="deadzone" min="0" max="0.3" step="0.01"><output></output></div>
 <div class="row"><label>Expo</label><input type="range" id="expo" min="0" max="1" step="0.05"><output></output></div>
 <div class="row"><label>Ramp (/s)</label><input type="range" id="ramp" min="0" max="20" step="0.5"><output></output></div>
 <small>Ramp 0 = berhenti serta-merta. Nilai kecil = mula dan berhenti lebih lembut.</small>
 <div class="row"><label>Invert pan</label><input type="checkbox" id="inv0"></div>
 <div class="row"><label>Invert tilt</label><input type="checkbox" id="inv1"></div>
 <div class="row"><label>Invert zoom</label><input type="checkbox" id="inv2"></div>
</div>

<div class="card"><h2>Movement speed 1-4 (% maksimum)</h2>
 <div class="grid"><input id="m1" type="number" min="1" max="100"><input id="m2" type="number" min="1" max="100"><input id="m3" type="number" min="1" max="100"><input id="m4" type="number" min="1" max="100"></div>
 <h2 style="margin-top:14px">Zoom speed 1-4 (% maksimum)</h2>
 <div class="grid"><input id="z1" type="number" min="1" max="100"><input id="z2" type="number" min="1" max="100"><input id="z3" type="number" min="1" max="100"><input id="z4" type="number" min="1" max="100"></div>
 <div class="row" style="margin-top:14px"><label>Recall speed</label><select id="recall"></select></div>
 <small>Kelajuan gimbal bergerak ke preset (gerakan smooth ease in/out).</small>
</div>

<div class="btns"><button class="p" onclick="save()">Simpan tetapan</button><span id="msg" class="mono"></span></div>

<div class="card"><h2>Kalibrasi joystick</h2>
 <ol style="margin:0 0 10px;padding-left:18px;color:var(--dim)">
  <li>Lepaskan joystick, tekan <b>Set centre</b>.</li>
  <li>Tekan <b>Mula range</b>, gerak X, Y dan twist sampai habis ke semua arah.</li>
  <li>Tekan <b>Tamat range</b>.</li></ol>
 <div class="btns"><button onclick="cal('centre')">Set centre</button><button onclick="cal('range_start')">Mula range</button><button onclick="cal('range_stop')">Tamat range</button><button onclick="cal('reset')">Reset</button></div>
 <div class="mono" id="cal" style="margin-top:10px"></div>
</div>

<div class="card"><h2>Firmware</h2>
 <a href="/update" style="color:var(--acc)">Update firmware (.bin)</a><br>
 <small>Arduino IDE: Sketch &gt; Export Compiled Binary, kemudian upload fail remote.ino.bin di sini.</small>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
let S={};
function bindRange(id){const e=$(id),o=e.nextElementSibling;const f=()=>o.textContent=e.value;e.oninput=f;f()}
async function load(){
 S=await (await fetch('/api/settings')).json();
 ['deadzone','expo','ramp'].forEach(k=>{$(k).value=S[k];bindRange(k)});
 S.inv.forEach((v,i)=>$('inv'+i).checked=!!v);
 S.movePct.forEach((v,i)=>$('m'+(i+1)).value=v);
 S.zoomPct.forEach((v,i)=>$('z'+(i+1)).value=v);
 $('recall').innerHTML=S.recallDps.map((d,i)=>`<option value="${i}">${d} °/s</option>`).join('');
 $('recall').value=S.recallIdx; showCal();
}
function showCal(){$('cal').textContent=['X','Y','Z'].map((a,i)=>`${a}: min ${S.cal[i][0]}  centre ${S.cal[i][1]}  max ${S.cal[i][2]}`).join('\n');$('cal').style.whiteSpace='pre'}
async function save(){
 const p=new URLSearchParams();
 ['deadzone','expo','ramp'].forEach(k=>p.append(k,$(k).value));
 [0,1,2].forEach(i=>p.append('inv'+i,$('inv'+i).checked?1:0));
 [1,2,3,4].forEach(i=>{p.append('m'+i,$('m'+i).value);p.append('z'+i,$('z'+i).value)});
 p.append('recall',$('recall').value);
 S=await (await fetch('/api/settings',{method:'POST',body:p})).json();
 $('msg').textContent='Disimpan ✓';setTimeout(()=>$('msg').textContent='',2000);
}
async function cal(a){S=await (await fetch('/api/cal?a='+a)).json();showCal()}
function bar(i,v){const b=$('b'+i);const w=Math.abs(v)*50;b.style.width=w+'%';b.style.left=(v<0?50-w:50)+'%';$('o'+i).textContent=v.toFixed(2)}
async function live(){
 try{const L=await (await fetch('/api/live')).json();
  L.out.forEach((v,i)=>bar(i,v));
  $('raw').textContent=`raw X ${L.raw[0]}  Y ${L.raw[1]}  Z ${L.raw[2]}   MOVE ${L.move}  ZOOM ${L.zoom}${L.calRange?'   (merekod range...)':''}`;
  $('rx').textContent='Receiver: '+(L.rx||'—');
  $('link').textContent=L.link?`LQ ${L.lq}  ${L.rssi} dBm`:'NO LINK';$('link').className='pill '+(L.link?'ok':'bad');
 }catch(e){}
 setTimeout(live,200);
}
load();live();
</script></body></html>)rawliteral";
