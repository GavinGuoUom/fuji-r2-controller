"use strict";

const $ = id => document.getElementById(id);
const stateNames = ["总关", "建立偏置", "点灯待机", "自动行驶", "端点停稳", "点灯停站", "换向", "反向偏置", "端点减速"];
const faultNames = ["正常", "硬件故障锁存", "驱动器故障", "逻辑电源异常", "行程超时", "看门狗复位", "启动超时"];
const pageCopy = {
  monitor: ["运行监控", "查看端点事件、PWM 和控制器保护状态。"],
  guide: ["引导测试", "选择一个组件，按步骤操作并核对实时读数。"],
  tuning: ["在线调参", "调整长点灯、停站和加减速时间，设置旋钮方向。"],
};
const names = ["速度", "时间", "长点灯"];
const ports = {1: ["J401", "J402"], 2: ["J421", "J422"], 3: ["J441", "J442"]};
const tuningFields = ["light", "dwell-min", "dwell-max", "ramp-min", "ramp-max", "runout", "brake-max"].map($);
const tuningScales = [10, 1000, 1000, 1000, 1000, 1, 1000];
const reverseBoxes = [];
const ROTARY_MAX = 512;
const testPosition = (r, i) => r.rotaries ? r.rotaries[i] * 1023 / ROTARY_MAX : r.adc[i];
const TESTS = {
  master: {name: "总控开关", shared: true, steps: [
    {title: "拨到 OFF", text: "关总控，确认三路已停止输出。", auto: s => s.system.total_n && !s.system.enabled && s.tracks.every(r => !r.pwm), demo: ["master", 0]},
    {title: "拨到 ON", text: "保持轨道馈线断开，打开总控，等待输出许可。", auto: s => !s.system.total_n && s.system.enabled, demo: ["master", 1]},
    {title: "拨回 OFF", text: "关总控，确认输出许可撤销。", auto: s => s.system.total_n && !s.system.enabled && s.tracks.every(r => !r.pwm), demo: ["master", 0]},
  ]},
  ...Object.fromEntries(names.map((name, index) => [`pot${index}`, {
    name: `${name}旋钮`, channel: index,
    steps: [
      {title: "逆时针到底", text: "保持总控 OFF，将此旋钮逆时针转到底。生效位置应接近最小。", auto: (s,r) => s.system.total_n && testPosition(r,index)<=80, demo: ["pot", 0]},
      {title: "顺时针到底", text: "将同一旋钮顺时针转到底。生效位置应升到最大。", auto: (s,r) => s.system.total_n && testPosition(r,index)>=943, demo: ["pot", 1023]},
      {title: "回到逆时针端", text: "转回逆时针端，确认生效位置回到最小。", auto: (s,r) => s.system.total_n && testPosition(r,index)<=80, demo: ["pot", 0]},
    ],
  }])),
  button: {name: "启动／停止按钮", channel: 0, steps: [
    {title: "启动本路", text: "保持馈线断开，速度与长点灯归零。开总控，按一次本路按钮，查看运行灯。", auto: (s,r,b) => s.system.enabled && r.state>=3 && newHit(r,b,0), demo: ["hit", 0]},
    {title: "停止本路", text: "释放后再按一次。确认自动运行取消，运行灯熄灭。", auto: (s,r,b) => s.system.enabled && r.state<3 && newHit(r,b,0), demo: ["hit", 0]},
  ]},
  ...Object.fromEntries(["L", "R"].map((end,index) => [end, {
    name: `${end} 端传感器`, channel:index+1,
    steps: [{title: "经过所选端点", text: "只给测试线路供电。保持该路待机，调少量长点灯，用无动力车辆手推过所选传感器并完全离开。",
      auto: (s,r,b) => s.system.enabled && newHit(r,b,index+1), demo: ["hit",0]}],
  }])),
  output: {name: "轨道输出", steps: [
    {title: "观察待机 PWM", text: "保持馈线断开。开总控，调高本路长点灯，观察 PWM 与长点灯设定一致。", auto: (s,r) => s.system.enabled && r.state===2 && r.pwm>0 && r.pwm===r.minimum, demo: ["output",1]},
    {title: "验证总控关断", text: "关总控，确认本路 PWM 归零。", auto: (s,r) => s.system.total_n && !s.system.enabled && !r.pwm, demo: ["master",0]},
  ]},
  indicator: {name: "独立故障灯", shared: true, steps: [
    {title: "核对故障指示", text: "已有硬件故障锁存时，比对面板独立红灯与保护状态。记录目视结果后手动确认。", auto: () => false, demo: null},
  ]},
};

let status = {};
let page = "monitor";
let test = "master";
let stage = 0;
let baseline;
let results = [];
let previous;
let eventEpoch;
let tuningLookup = "";
let dirty = false;
let lastReply = "";
const flash = Array.from({length: 3}, () => [0,0,0]);

function newHit(route, before, index) {
  if (!before) return false;
  const difference = (route.hits[index] - before.hits[index] + 65536) % 65536;
  return difference > 0 && difference < 32768;
}

function h(id, value, style="") {
  const el = $(id);
  el.textContent = value;
  el.classList.remove("good","bad","warm");
  if (style) el.classList.add(style);
}

function percentage(pwm) {
  return pwm === undefined ? "—" : (pwm*100/512).toFixed(1)+"%";
}

function seconds(ms) {
  return ms == null ? "—" : (ms/1000).toLocaleString("zh-CN",{maximumFractionDigits:3})+" s";
}

async function api(route, data) {
  const response = await fetch("/api/"+route, data === undefined ? {} : {
    method:"POST", headers:{"Content-Type":"application/json","X-Fuji-Console":"1"}, body:JSON.stringify(data),
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error);
  return result;
}

function notify(message) {
  $("notice").textContent = message;
  $("notice").hidden = !message;
}

async function loadPorts() {
  try {
    const {ports:found, serialInstalled} = await api("ports");
    const saved = $("ports").value || localStorage.getItem("fuji-port");
    $("ports").replaceChildren();
    for (const p of found) {
      $("ports").append(new Option(p.device + (p.description ? " · " + p.description : ""),p.device));
    }
    $("ports").append(new Option("离线演示","demo"));
    if (Array.from($("ports").options).some(option => option.value===saved)) $("ports").value = saved;
    if (!serialInstalled) notify("未安装 pyserial，可先使用离线演示。");
  } catch (error) {
    notify(error.message);
  }
}

function choosePage(next) {
  page = next;
  for (const tab of document.querySelectorAll(".tab")) {
    const active = tab.dataset.page===next;
    tab.classList.toggle("active",active);
    tab.setAttribute("aria-current",active?"page":"false");
  }
  for (const section of document.querySelectorAll(".page")) section.hidden = section.id!==next;
  [ $("page-title").textContent, $("page-description").textContent ] = pageCopy[next];
  if (page==="guide" && !baseline && stage===0) resetGuide();
  render();
}

function seedTrackCards() {
  const labels = ["目标占空比","点灯占空比","停站设定","减速设定","电流原码"];
  for (let i=1; i<=3; i++) {
    const card = document.createElement("article");
    card.className = "panel track-card";
    card.innerHTML = `
      <div class="section-head"><h3>线路 ${i}</h3><span class="pill" id="state-${i}">离线</span></div>
      <div class="rail"><span class="endpoint" id="left-${i}">L —</span><div class="rails"><b id="dir-${i}">—</b></div><span class="endpoint" id="right-${i}">R —</span></div>
      <div class="power-line"><span>轨道 PWM</span><strong id="pwm-${i}">—</strong></div>
      <div class="power-bar"><i id="bar-${i}" style="width:0%"></i></div>
      <div class="metrics">${labels.map((label,n)=>`<div><span>${label}</span><b id="metric-${i}-${n}">—</b></div>`).join("")}</div>
      <div class="raw"><span>ADC · 速度 / 时间 / 点灯</span><code id="adc-${i}">—</code></div>`;
    $("tracks").append(card);
  }
}

function addEvent(message) {
  const li = document.createElement("li");
  const clock = document.createElement("time");
  clock.textContent = new Date().toLocaleTimeString("zh-CN",{hour12:false});
  li.append(clock,document.createTextNode(message));
  $("event-list").prepend(li);
  while ($("event-list").children.length>80) $("event-list").lastChild.remove();
}

function recordEvents(frame) {
  if (!previous) {
    addEvent(status.mode==="demo" ? "离线演示已启动" : "已收到控制器数据");
    return;
  }
  if (frame.system.fault!==previous.system.fault) {
    addEvent(faultNames[frame.system.fault] || `故障 F=${frame.system.fault}`);
  }
  if (frame.system.total_n!==previous.system.total_n) addEvent(frame.system.total_n?"总控 OFF":"总控 ON");
  frame.tracks.forEach((r,i) => {
    const old = previous.tracks[i];
    r.hits.forEach((hits,k) => {
      if (!newHit(r,old,k)) return;
      flash[i][k] = Date.now()+900;
      const labels = ["按键","左端 L","右端 R"];
      addEvent(`线路 ${i+1} · ${labels[k]} +${(hits-old.hits[k]+65536)%65536}`);
    });
    if (r.direction!==old.direction) addEvent(`线路 ${i+1} · 切向 ${r.direction}`);
  });
}

function renderMonitor(frame) {
  for (let i=1;i<=3;i++) {
    const r = frame?.tracks[i-1];
    h(`state-${i}`,r ? stateNames[r.state]??`状态 ${r.state}` : "离线",r?.state>=3?"good":"");
    h(`dir-${i}`,r?.state ? (r.direction==="L"?"←":"→") : "—");
    h(`pwm-${i}`,percentage(r?.pwm));
    $(`bar-${i}`).style.width = percentage(r?.pwm??0);
    [percentage(r?.goal),percentage(r?.minimum),seconds(r?.dwell),seconds(r?.brake),r?.current??"—"]
      .forEach((x,n)=>h(`metric-${i}-${n}`,x));
    h(`adc-${i}`,r?.adc.join(" / ")??"—");
    ["left","right"].forEach((end,k)=>{
      const endpoint = $(`${end}-${i}`);
      const label = k ? "R" : "L";
      endpoint.textContent = `${label} ${r?.hits[k+1]??"—"}`;
      endpoint.classList.toggle("firing", Boolean(r && (r.levels[k+1] || flash[i-1][k+1]>Date.now())));
    });
  }
}

function guideRoute() { return Number($("guide-track").value); }

function seedInputPanel() {
  $("test-rotaries").innerHTML = names.map((name,i)=>`
    <div class="input-tile" data-input="pot${i}">
      <span>${name}</span><div class="dial" aria-hidden="true"><i></i></div>
      <strong id="test-pot${i}">—</strong><small id="test-raw${i}">ADC —</small>
    </div>`).join("");
  $("test-digital").innerHTML = [["master","总控"],["button","启停按钮"],["L","L 端"],["R","R 端"]].map(([key,label])=>`
    <div class="input-tile digital" data-input="${key}">
      <span><i class="input-light" aria-hidden="true"></i>${label}</span>
      <strong id="test-${key}">—</strong><small id="detail-${key}"></small>
    </div>`).join("");
}

function renderInputPanel(frame, index) {
  const r = frame?.tracks[index];
  const now = Date.now();
  h("panel-track",`线路 ${index+1}`);
  document.querySelector(".test-panel").classList.toggle("no-sample",!r);
  for (const el of document.querySelectorAll(".test-panel [data-input]")) {
    el.classList.toggle("selected",el.dataset.input===test);
  }
  names.forEach((_,i)=>{
    const raw = r?.adc[i];
    const value = r?.rotaries?.[i];
    const reading = $(`test-pot${i}`);
    reading.textContent = value==null ? "—" : `${(value*100/ROTARY_MAX).toFixed(1)}%`;
    h(`test-raw${i}`, `ADC ${raw??"—"}`);
    reading.previousElementSibling.style.setProperty("--angle",`${-135+(r ? testPosition(r,i) : 0)*270/1023}deg`);
  });
  const digital = (key, active, label, detail) => {
    const output = $(`test-${key}`);
    output.textContent = r ? label : "—";
    output.parentElement.classList.toggle("lit",Boolean(r && active));
    h(`detail-${key}`,detail);
  };
  const masterOn = frame?.system.total_n===0;
  digital("master",masterOn,masterOn?"ON":"OFF","");
  ["button","L","R"].forEach((key,i)=>{
    const held = r?.levels[i];
    const recent = flash[index][i]>now;
    const label = held ? (i ? "触发中" : "按下") : recent ? (i ? "检测到脉冲" : "检测到按键") : (i ? "待触发" : "松开");
    const port = i ? `${ports[index+1][i-1]} · ` : "";
    digital(key,held||recent,label,`${port}计数 ${r?.hits[i]??"—"}`);
  });
}

function resetGuide() {
  stage = 0;
  results = [];
  baseline = status.frame?.tracks[guideRoute()-1];
  renderGuide();
}

function finishStep(source) {
  const selected = TESTS[test];
  if (stage>=selected.steps.length) return;
  results[stage] = source;
  addEvent(`${selected.shared?"":`线路 ${guideRoute()} · `}${selected.name} · ${selected.steps[stage].title}（${source}）`);
  stage += 1;
  baseline = status.frame?.tracks[guideRoute()-1];
  renderGuide();
}

function renderGuide() {
  const selected = TESTS[test];
  const step = selected.steps[stage];
  const index = guideRoute()-1;
  const frame = (status.mode==="demo" || status.mode==="serial" && status.age<=2) ? status.frame : null;
  const row = frame?.tracks[index];
  const system = frame?.system;
  const complete = !step;
  const name = `${selected.shared?"总控":`线路 ${index+1}`} / ${selected.name}`;
  h("guide-type",name);
  h("guide-title",complete?"测试完成":step.title);
  const connector = (test==="L" || test==="R") ? ` · ${ports[index+1][test==="L"?0:1]}` : "";
  h("guide-text",complete ? "可在左侧选择其他组件，或重新开始此项。": step.text+connector);
  let display = "等待实时数据";
  if (row && system) {
    display = `总控 ${system.total_n?"OFF":"ON"} · PWM ${percentage(row.pwm)}`;
    if (test.startsWith("pot")) display += ` · ADC ${row.adc[selected.channel]} / 1023`;
    if (test==="indicator") display += ` · F=${system.fault} · 锁存 ${system.latch}`;
    if (test==="L" || test==="R" || test==="button") display += ` · 事件计数 ${row.hits[selected.channel]}`;
  }
  if ((test==="L" || test==="R") && status.tuning?.[5]) display += ` · 设定余距 ${status.tuning[5]} mm`;
  h("guide-live",display);
  renderInputPanel(frame,index);
  $("guide-steps").replaceChildren(...selected.steps.map((item,n)=>{
    const line = document.createElement("div");
    line.className = `step ${n===stage?"active":n<stage?"done":""}`;
    const mark = document.createElement("span");
    mark.className = "check";
    mark.textContent = n<stage?"✓":String(n+1).padStart(2,"0");
    line.append(mark,document.createTextNode(item.title+(results[n]?` · ${results[n]}`:"")));
    return line;
  }));
  $("confirm").disabled = complete;
  $("demo-action").hidden = status.mode!=="demo" || !step?.demo;
  for (const option of $("test-options").children) {
    option.classList.toggle("selected",option.dataset.test===test);
    option.setAttribute("aria-pressed",String(option.dataset.test===test));
  }
}

function rotaryMask() {
  return reverseBoxes.reduce((mask, box, i)=>box.checked ? mask | (1<<i) : mask,0);
}

function seedRotaryDirections() {
  const grid = $("rotary-grid");
  grid.append(document.createElement("span"));
  for (const name of names) {
    const header = document.createElement("span");
    header.textContent = name;
    grid.append(header);
  }
  for (let i=0; i<3; i++) {
    const track = document.createElement("span");
    track.textContent = `线路 ${i+1}`;
    grid.append(track);
    for (const name of names) {
      const box = document.createElement("input");
      box.type = "checkbox";
      box.setAttribute("aria-label", `线路 ${i+1} ${name}反向`);
      box.addEventListener("change",()=>{dirty=true;renderTuning();});
      reverseBoxes.push(box);
      grid.append(box);
    }
  }
  for (const [id, checked] of [["reverse-all",true],["clear-reverse",false]]) {
    $(id).addEventListener("click",()=>{
      if (reverseBoxes.every(box=>box.checked===checked)) return;
      reverseBoxes.forEach(box=>{box.checked=checked;});
      dirty=true;
      renderTuning();
    });
  }
}

function tuneValues() {
  const values = tuningFields.slice(0,status.tuning?.length??0).map((field,i)=>Math.round(Number(field.value)*tuningScales[i]));
  if (status.tuning?.length===8) values.push(rotaryMask());
  return values;
}

function renderTuning() {
  const received = status.tuning?.join(",")??"";
  const acknowledgment = status.reply==="控制器已应用参数" || status.reply==="演示参数已更新";
  if (status.tuning && (received!==tuningLookup && !dirty || acknowledgment && received===tuneValues().join(","))) {
    tuningFields.forEach((field,i)=>{ field.value = i<status.tuning.length ? status.tuning[i]/tuningScales[i] : ""; });
    reverseBoxes.forEach((box,i)=>{box.checked=Boolean(status.tuning[7] & (1<<i));});
    dirty = false;
    tuningLookup = received;
  } else if (!status.tuning && !dirty && tuningLookup) {
    tuningFields.forEach(field=>{field.value="";});
    tuningLookup = "";
  }
  const ready = status.mode==="serial" || status.mode==="demo";
  const fresh = ready && (status.mode==="demo" || status.age<=2);
  const reverseSupported = status.tuning?.length===8;
  const masterOn = status.frame?.system.total_n===0;
  const reverseBlocked = reverseSupported && masterOn && rotaryMask()!==status.tuning[7];
  tuningFields.forEach((field,i)=>{field.disabled = i>=(status.tuning?.length??0);});
  $("rotary-directions").disabled = !fresh || !reverseSupported || status.pending;
  $("read").disabled = !fresh || status.pending;
  $("apply").disabled = !fresh || !status.tuning || status.pending || !dirty || reverseBlocked;
  $("save").disabled = !fresh || !status.tuning || status.pending || dirty ||
    (status.mode==="serial" && (!status.frame?.system.total_n || status.age>2));
  const guidance = status.tuning?.length===5 ? "当前固件支持前五项参数。升级后可设置端点减速。" :
      status.tuning?.length===7 ? "升级固件后可设置旋钮方向。" :
      reverseBlocked ? "请把总控拨回 OFF，再应用旋钮方向。" :
      ready&&!status.tuning ? "等待支持调参协议的 R2 固件" : "";
  h("tuning-reply",status.pending?"等待控制器响应…":[status.reply,guidance].filter(Boolean).join(" · "));
}

function render() {
  const ready = status.mode==="serial" || status.mode==="demo";
  const active = ready || status.mode==="connecting";
  const frame = ready && (status.mode==="demo" || status.age<=2) ? status.frame : null;
  const system = frame?.system;
  h("link", status.mode==="demo" ? "● 离线演示" : status.mode==="connecting" ? "● 正在连接" :
      status.mode==="serial" ? (frame?`● ${status.device} 在线`:`● ${status.device} 无数据`) : "● 离线",
    status.mode==="demo"||status.mode==="connecting"?"warm":frame?"good":"");
  h("master",system?(system.total_n?"OFF":"ON"):"—",system&&!system.total_n?"warm":"");
  h("enabled",system?(system.enabled?"已允许":"已关闭"):"—",system?.enabled?"good":"");
  const fault = system?.fault ?? 0;
  const affected = system?.mask ? [1,2,3].filter(i=>system.mask & (1<<(i-1))).join("/") : "";
  h("fault",system ? (fault ? `F=${fault} · ${faultNames[fault]??"未知"}${affected?` · 线路 ${affected}`:""}` : "正常") : "—",system?(fault?"bad":"good"):"");
  h("age",status.mode==="demo"?"模拟数据":frame?`${status.age.toFixed(2)} s 前`:"—");
  $("connect").textContent = active?"断开":"连接";
  $("ports").disabled = active;
  $("refresh").disabled = active;
  renderMonitor(frame);
  if (page==="guide") renderGuide();
  renderTuning();
}

async function readState() {
  try {
    status = await api("state");
    if (status.error) notify(status.error);
    if (eventEpoch!==status.epoch) {
      eventEpoch = status.epoch;
      previous = baseline = undefined;
      flash.forEach(row=>row.fill(0));
      dirty = false;
      tuningLookup = "";
      tuningFields.forEach(field=>{field.value="";});
      resetGuide();
    }
    if (lastReply!==status.reply) {
      lastReply = status.reply;
      if (lastReply) addEvent(lastReply);
    }
    const fresh = status.frame && (status.mode==="demo" || status.mode==="serial" && status.age<=2);
    if (fresh && previous?.seq!==status.seq) {
      const frame = status.frame;
      recordEvents(frame);
      previous = {...frame,seq:status.seq};
    }
    if (fresh && page==="guide") {
      const frame = status.frame;
      const step = TESTS[test].steps[stage];
      const row = frame.tracks[guideRoute()-1];
      if (step && !frame.system.fault && step.auto(frame,row,baseline)) finishStep("自动识别");
      else if (!baseline) baseline = row;
    }
    render();
  } catch (error) {
    notify("无法读取本机控制台 · "+error.message);
  } finally {
    setTimeout(readState,250);
  }
}

async function send(route, values) {
  try { notify(""); await api(route,values); }
  catch (error) { notify(error.message); }
}

for (const [key,definition] of Object.entries(TESTS)) {
  const button = document.createElement("button");
  button.dataset.test = key;
  button.textContent = definition.name;
  button.addEventListener("click",()=>{test=key;resetGuide();});
  $("test-options").append(button);
}
for (const tab of document.querySelectorAll(".tab")) tab.addEventListener("click",()=>choosePage(tab.dataset.page));
$("refresh").addEventListener("click",loadPorts);
$("connect").addEventListener("click",()=>{
  if (status.mode==="connecting" || status.mode==="demo" || status.mode==="serial") send("disconnect",{});
  else {
    localStorage.setItem("fuji-port",$("ports").value);
    send("connect",{device:$("ports").value});
  }
});
$("clear-events").addEventListener("click",()=>$("event-list").replaceChildren());
$("guide-track").addEventListener("change",resetGuide);
$("restart").addEventListener("click",resetGuide);
$("confirm").addEventListener("click",()=>finishStep("人工确认"));
$("demo-action").addEventListener("click",async()=>{
  const selected = TESTS[test];
  const step = selected.steps[stage];
  if (!step?.demo) return;
  const [action,value] = step.demo;
  if (action==="master" && (status.frame.system.enabled!==value)) await send("demo",{action});
  else if (action==="hit") {
    if (!status.frame.system.enabled) await send("demo",{action:"master"});
    await send("demo",{action,track:guideRoute(),channel:selected.channel,value});
  } else if (action==="output") {
    if (!status.frame.system.enabled) await send("demo",{action:"master"});
    await send("demo",{action,track:guideRoute(),channel:0,value});
  } else if (action==="pot") {
    const track = guideRoute();
    const wiredReverse = status.tuning?.[7] & (1<<((track-1)*3+selected.channel));
    await send("demo",{action,track,channel:selected.channel,value:wiredReverse ? 1023-value : value});
  }
});
for (const field of tuningFields) field.addEventListener("input",()=>{ dirty=true;renderTuning(); });
$("parameters").addEventListener("submit",event=>{
  event.preventDefault();
  if (event.currentTarget.reportValidity()) send("tuning",{kind:"set",values:tuneValues()});
});
$("read").addEventListener("click",()=>{
  dirty = false;
  tuningLookup = "";
  send("tuning",{kind:"get"});
});
$("save").addEventListener("click",()=>send("tuning",{kind:"save"}));
seedTrackCards();
seedInputPanel();
seedRotaryDirections();
loadPorts();
readState();
