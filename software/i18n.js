"use strict";

const english = new Map(`
Fuji R2 · 线路调试台|Fuji R2 · Serial Console
工具|Tools
语言|Language
运行监控|Monitor
查看端点事件、PWM 和控制器保护状态。|View end-sensor events, PWM and controller protection.
引导测试|Guided Test
选择一个组件，按步骤操作并核对实时读数。|Choose a component, follow the steps and check its live readings.
在线调参|Live Tuning
调整长点灯、停站和加减速时间，设置旋钮方向。|Adjust standing lights, dwell, acceleration and braking. Set knob directions.
FUJI R2 / 三路往返控制器|FUJI R2 / THREE-TRACK SHUTTLE CONTROLLER
串口|Serial port
刷新串口|Refresh serial ports
连接|Connect
断开|Disconnect
总控开关|Master track switch
总控|Master switch
输出许可|Track output
保护状态|Protection
最近采样|Last sample
线路状态|Track status
事件记录|Event log
清空|Clear
测试项目|Components
线路|Track
线路 1|Track 1
线路 2|Track 2
线路 3|Track 3
线路 {0}|Track {0}
重新开始|Restart
等待实时数据|Waiting for live data
确认操作完成|Confirm step
模拟此操作|Simulate step
实时输入|Live inputs
三路共用参数|Shared track settings
控制器|Controller
长点灯|Standing light
最高占空比|Maximum duty cycle
停站时间|Dwell time
最短|Shortest
最长|Longest
满量程加速时间|Full-range acceleration time
最快|Fastest
最慢|Slowest
端点减速|End-sensor braking
传感器到区间末端|Sensor-to-track-end distance
减速时间上限|Maximum braking time
旋钮方向|Knob direction
勾选需要反向的旋钮。总控 OFF 时应用并保存。|Check the knobs to invert. Apply and save with the master switch OFF.
全部反向|Invert all
取消全部反向|Clear inversions
立即应用|Apply Now
保存到控制器|Save to Controller
重新读取|Read Again
时间旋钮|Timing knob
最小端|Minimum
最大端|Maximum
逆时针端|Counterclockwise end
顺时针端|Clockwise end
停站短、升速快、到端直接进入停稳|Short dwell, fast acceleration and immediate stop command at the end sensor
停站长、升速慢、端点渐降至长点灯占空比|Long dwell, slow acceleration and gradual braking to the standing-light duty cycle
加速时间指 PWM 走完全量程所需的时间。实际升速时间随速度旋钮的目标变化。|Acceleration time is the time for PWM to traverse its full range. Actual ramp time depends on the speed knob setting.
停稳阶段另计 0.5 秒。正在进行的停站沿用进入停站时的计时。|The controller allows another 0.5 seconds for the train to settle. A stop already in progress keeps its original timing.
余距填六个端点中的最短值，0 表示未测。减速上限填 0 则关闭渐降。用实际车辆核对停车位置，停车距离随车速和惯性变化。|Enter the shortest clearance across the six end sensors. Enter 0 if unmeasured. Set maximum braking time to 0 to disable gradual braking. Verify stop positions with the actual train, since speed and inertia affect stopping distance.
应用后立即生效。保存前先把总控拨到 OFF，重启后才会沿用保存值。|Changes take effect when applied. Set the master switch to OFF before saving to keep the settings after a restart.
本机串口调试|Local serial console
总关|Output off
建立偏置|Setting bias
点灯待机|Standing-light standby
自动行驶|Shuttling
端点停稳|Settling at end
点灯停站|Dwell with lights
换向|Reversing
反向偏置|Setting reverse bias
正常|Normal
硬件故障锁存|Hardware fault latched
驱动器故障|Driver fault
逻辑电源异常|Logic power fault
行程超时|Travel timeout
看门狗复位|Watchdog reset
启动超时|Startup timeout
速度|Speed
时间|Timing
速度旋钮|Speed knob
长点灯旋钮|Standing-light knob
{0}旋钮|{0} knob
{0} 端传感器|End sensor {0}
拨到 OFF|Switch to OFF
关总控，确认三路已停止输出。|Switch the master track switch OFF and confirm all three outputs are off.
拨到 ON|Switch to ON
保持轨道馈线断开，打开总控，等待输出许可。|Leave the track feeders disconnected. Turn the master track switch ON and wait for output to be enabled.
拨回 OFF|Switch back to OFF
关总控，确认输出许可撤销。|Turn the master track switch OFF and confirm output is disabled.
逆时针到底|Turn fully counterclockwise
保持总控 OFF，将此旋钮逆时针转到底。生效位置应接近最小。|Keep the master switch OFF and turn this knob fully counterclockwise. Its effective position should be near minimum.
顺时针到底|Turn fully clockwise
将同一旋钮顺时针转到底。生效位置应升到最大。|Turn the same knob fully clockwise. Its effective position should reach maximum.
回到逆时针端|Return to the counterclockwise end
转回逆时针端，确认生效位置回到最小。|Turn back fully counterclockwise and confirm the effective position returns to minimum.
启动／停止按钮|Start/Pause button
启停按钮|Start/Pause button
启动本路|Start this track
保持馈线断开，速度与长点灯归零。开总控，按一次本路按钮，查看运行灯。|Leave the feeder disconnected and turn speed and standing light to zero. Turn the master switch ON. Press this track’s button once and check the indicator ring.
停止本路|Pause this track
释放后再按一次。确认自动运行取消，运行灯熄灭。|Release the button and press it again. Check that shuttling stops and the indicator ring goes out.
经过所选端点|Pass the selected end sensor
只给测试线路供电。保持该路待机，调少量长点灯，用无动力车辆手推过所选传感器并完全离开。|Power only the test track. Leave it in standby and turn the standing-light setting up slightly. Push an unpowered car past the selected sensor and clear it completely.
轨道输出|Track output
观察待机 PWM|Check standby PWM
保持馈线断开。开总控，调高本路长点灯，观察 PWM 与长点灯设定一致。|Leave the feeder disconnected. Turn the master switch ON and turn up this track’s standing-light setting. Check that PWM matches the setting.
验证总控关断|Verify master switch shutoff
关总控，确认本路 PWM 归零。|Turn the master switch OFF and confirm PWM drops to zero.
独立故障灯|Fault light
核对故障指示|Check the fault indication
已有硬件故障锁存时，比对面板独立红灯与保护状态。记录目视结果后手动确认。|If a hardware fault is already latched, compare the panel’s red light with the protection status. Record your observation and confirm the step manually.
离线演示|Offline Demo
未安装 pyserial，可先使用离线演示。|Install pyserial for serial connections. Offline Demo is available now.
目标占空比|Target duty
点灯占空比|Light duty
停站设定|Dwell setting
减速设定|Braking setting
电流原码|Raw current
离线|Offline
轨道 PWM|Track PWM
ADC · 速度 / 时间 / 点灯|ADC · Speed / Timing / Light
离线演示已启动|Offline demo started
已收到控制器数据|Controller data received
故障 F={0}|Fault F={0}
总控 OFF|Master switch OFF
总控 ON|Master switch ON
按键|Button press
左端 L|Left end L
右端 R|Right end R
切向 {0}|Direction changed to {0}
状态 {0}|State {0}
L 端|End L
R 端|End R
触发中|Active
按下|Pressed
检测到脉冲|Pulse detected
检测到按键|Press detected
待触发|Ready
松开|Released
计数 {0}|Count {0}
测试完成|Test complete
可在左侧选择其他组件，或重新开始此项。|Choose another component on the left or restart this test.
锁存 {0}|Latch {0}
事件计数 {0}|Event count {0}
设定余距 {0} mm|Configured clearance {0} mm
线路 {0} {1}反向|Invert {1} on Track {0}
当前固件支持前五项参数。升级后可设置端点减速。|This firmware supports the first five settings. Update it to configure end-sensor braking.
升级固件后可设置旋钮方向。|Update the firmware to configure knob direction.
请把总控拨回 OFF，再应用旋钮方向。|Set the master switch to OFF before applying knob directions.
等待支持调参协议的 R2 固件|Waiting for compatible R2 firmware
等待控制器响应…|Waiting for the controller…
● 离线演示|● Offline Demo
● 正在连接|● Connecting
● {0} 在线|● {0} online
● {0} 无数据|● {0} no data
● 离线|● Offline
已允许|Enabled
已关闭|Disabled
未知|Unknown
模拟数据|Simulated data
{0} s 前|{0} s ago
自动识别|Detected automatically
人工确认|Confirmed manually
无法读取本机控制台 · {0}|Could not read the local console · {0}
需要 {0} 个整数参数|Expected {0} integer settings
参数超出范围，或最小值大于最大值|A setting is out of range or a minimum exceeds its maximum.
余距或减速时间超出范围|Clearance or braking time is out of range.
旋钮方向设置超出范围|A knob direction setting is out of range.
找不到选择的串口|The selected serial port was not found.
请先安装 pyserial|Install pyserial first.
串口正在关闭，请稍候|The serial port is closing. Wait a moment.
控制器未确认命令，请重新读取参数|The controller did not acknowledge the command. Read the settings again.
控制器已应用参数|Controller settings applied
已读取控制器当前参数|Current controller settings read
已保存且读回校验通过|Saved and verified by readback
参数验证失败|Settings validation failed
请把总控拨回 OFF 再保存|Set the master switch to OFF before saving.
控制器正在保存|The controller is saving.
EEPROM 校验失败|EEPROM verification failed
未知参数操作|Unknown settings operation
请把总控拨回 OFF 再修改旋钮方向|Set the master switch to OFF before changing knob directions.
演示参数已更新|Demo settings updated
演示模式|Demo mode
串口未就绪或正在等待控制器|The serial port is unavailable or waiting for the controller.
当前固件尚未响应调参协议|The current firmware has not responded to the tuning protocol.
演示动作仅用于演示模式|Simulated actions are only available in demo mode.
无效线路或输入|Invalid track or input
控制台仅接受本机地址|The console only accepts local requests.
未找到|Not found
请从本机控制台操作|Use the local console for this action.
无效请求长度|Invalid request length
无效请求|Invalid request
`.trim().split("\n").map(row => row.split("|")));

let fujiLanguage = localStorage.getItem("fuji-language") === "en" ? "en" : "zh-CN";

function t(key, ...values) {
  if (fujiLanguage === "en" && /^需要 \d+ 个整数参数$/.test(key)) {
    return t("需要 {0} 个整数参数", key.match(/\d+/)[0]);
  }
  const copy = (fujiLanguage === "en" ? english.get(key) : undefined) ?? key;
  return copy.replace(/\{(\d+)\}/g, (_, i) => String(values[i]));
}

const staticText = [];
const textWalker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
while (textWalker.nextNode()) {
  const node = textWalker.currentNode;
  if (node.parentElement.closest("#language")) continue;
  const text = node.textContent.trim();
  if (english.has(text)) {
    const original = node.textContent;
    staticText.push(() => { node.textContent = original.replace(text, t(text)); });
  }
}
for (const el of document.querySelectorAll("[aria-label], [title]")) {
  for (const attr of ["aria-label","title"]) {
    const label = el.getAttribute(attr);
    if (english.has(label)) staticText.push(() => el.setAttribute(attr,t(label)));
  }
}
const initialTitle = document.title;

function applyFujiLocale(language) {
  fujiLanguage = language === "en" ? "en" : "zh-CN";
  localStorage.setItem("fuji-language", fujiLanguage);
  document.documentElement.lang = fujiLanguage;
  document.title = t(initialTitle);
  staticText.forEach(update => update());
}
