# Fuji R2 富士山沙盘控制器

简体中文 | [English](README.en.md)

三路列车自动往返控制器。日常运行使用控制面板。Windows 上位机用于查看状态、测试输入和修改参数。

## 下载

[下载完整 ZIP](https://github.com/GavinGuoUom/fuji-r2-controller/archive/refs/heads/main.zip)，解压后使用对应目录。

| 文件 | 用途 |
| --- | --- |
| 控制器使用手册 | [简体中文](fuji-r2-user-manual.html) · [English](fuji-r2-user-manual-en.html)。解压后用浏览器打开，可打印为 A4 |
| [Windows 上位机](software/) | 调试台程序、启动脚本和依赖清单 |
| [标准 R2 固件](firmware/) | 可直接用 PlatformIO 构建的固件源码 |
| [标准 R2 固件 HEX](fuji-r2-mega2560.hex) | 供维护人员使用的预编译镜像 |

## Windows 上位机

1. 打开解压后的 `software` 文件夹，安装 Python 3.10 或更高版本。如果电脑上已经安装 PlatformIO，也可用它自带的 Python。
2. 用 PowerShell 打开 `software` 文件夹，执行下面的命令安装串口依赖。安装一次即可。

   ```powershell
   py -m pip install -r requirements.txt
   ```

   若用 PlatformIO 自带的 Python，运行以下命令，让依赖装到启动脚本使用的环境。

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -r requirements.txt
   ```

3. 双击 `launch.cmd`，浏览器会打开本机调试台。启动窗口保持开启。已有一个调试台在运行时，直接打开 [本机调试台](http://127.0.0.1:8765/)。
   在页面右上角选择「简体中文」或「English」，软件会记住本次选择。
4. 面板轨道总控拨到 **O**，关闭控制器总电源，确保 USB 未接。**先拔下 R2 扩展板上的 JP3201 跳线帽，再用普通 USB 数据线连接 Mega2560。** 在线调试需要控制器供电时，再打开控制器总电源，轨道总控保持 **O**。在上位机选择对应 COM 端口并点击「连接」。
5. 调试结束后先点「断开」，关闭控制器总电源并拔掉 USB，再装回 JP3201。样机使用的 USB 断 5 V 专用线不随设备交付。

### 软件界面

- **运行监控**显示每路方向、轨道输出、端点事件和故障。PWM 百分比表示固件输出设定。
- **引导测试**逐路查看旋钮的原始 ADC 和生效位置、按钮状态及传感器触发次数。可选「离线演示」熟悉操作。
- **在线调参**可改长点灯上限、停站与满量程加速时间范围、端点减速时间上限、传感器余距以及旋钮方向。先「重新读取」，改好后点「立即应用」。总控保持 **O**，再点「保存到控制器」，等待校验通过。传感器余距的单位为 mm，填六个端点中最短值。停车位置以实车试跑为准。

连接不到时，先关闭占用 COM 端口的串口监视器，检查 USB 线是否支持数据传输，并在 Windows 设备管理器核对端口号。能看运行状态但读不到参数时，退出旧版，再从解压后的 `software` 文件夹启动新版。

## 固件升级

适用于富士山沙盘交付机的 **Arduino Mega2560 + Fuji R2 扩展板**。已有控制器正常运行时，可直接使用面板。

1. 按上述步骤连接 USB，确认 **JP3201 跳线帽已拔下**。在上位机读取、记录当前参数。把面板总控拨到 **O**，点击「断开」，关闭占用串口的其他软件。
2. 用 VS Code 打开解压后的 `firmware` 文件夹，安装 [PlatformIO IDE](https://docs.platformio.org/en/latest/integration/ide/vscode.html)。使用 PlatformIO Core CLI 也可执行同样的上传命令。
3. 在 PlatformIO 终端中依次执行。最后一项中的 `COM3` 换为设备实际端口号。

   ```powershell
   pio device list
   pio run -e fuji_mega_three_r2 -t upload --upload-port COM3
   ```

4. 等待上传和校验完成。重连上位机并确认总控仍为 **O**、三路 PWM 均为 0，核对八项设置。关闭控制器总电源，拔掉 USB 后装回 JP3201。恢复运行后先低速试一条线路，观察停站位置与换向，再依次试其余线路。

标准 R2 的端点减速从当前 PWM 降到长点灯占空比。停稳后保持该占空比，停站中点换向。上位机支持 `@CFG 3`，可显示和配置旋钮方向。
