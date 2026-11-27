# Fuji R2 · Mount Fuji Model Railway Controller

[简体中文](README.md) | English

Fuji R2 runs three independent shuttle tracks. Use the control panel for daily operation. Use the Windows serial console to monitor the controller, test inputs and tune settings.

## Downloads

[Download the complete ZIP](https://github.com/GavinGuoUom/fuji-r2-controller/archive/refs/heads/main.zip) and extract it.

| File | Use |
| --- | --- |
| Controller user guide | [English](fuji-r2-user-manual-en.html) · [简体中文](fuji-r2-user-manual.html). Open the extracted HTML file in a browser to read or print it on A4. |
| [Windows serial console](software/README.en.md) | Setup, guided tests and live tuning |
| [Standard R2 firmware source](firmware/) | PlatformIO project |
| [Standard R2 firmware HEX](fuji-r2-mega2560.hex) | Prebuilt image for servicing |

## Windows console

1. Open the extracted `software` folder and install Python 3.10 or later. If PlatformIO is already installed, you can use the Python it provides.
2. In PowerShell, run the following command from the `software` folder once to install the serial library.

   ```powershell
   py -m pip install -r requirements.txt
   ```

   If you use PlatformIO's Python, install the library with the same interpreter the launcher will use.

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install -r requirements.txt
   ```

3. Double-click `launch.cmd` and leave its window open while using the console. If it is already running, open [the local console](http://127.0.0.1:8765/). Use the language selector at the top right to switch between English and Simplified Chinese.
4. Set the master track switch to **O** and switch off controller power. With USB disconnected, **remove the JP3201 jumper cap from the R2 shield**. Leave it off while using a standard USB data cable. If tuning requires controller power, switch it on and keep the master track switch at O. Select the correct COM port and click **Connect**.
5. When finished, click **Disconnect**, switch off controller power and unplug USB. Refit the JP3201 cap before powering up again. The special USB cable used during prototype testing is excluded from the delivery.

Use **Monitor** to check status, **Guided Test** to check inputs and **Live Tuning** to edit settings. Use **Read Again** first, then **Apply Now**. Set the master track switch to O before clicking **Save to Controller**. For the sensor-to-track-end distance, enter the shortest clearance measured across the six end sensors. Validate stop positions with a running test.

If the console cannot connect, close other serial monitors, confirm the USB cable supports data and check the COM number in Windows Device Manager.

## Firmware update

Use this firmware with the Arduino Mega2560 and Fuji R2 shield supplied with the Mount Fuji layout.

1. Connect USB as described above with the **JP3201 cap removed**. Read and record the current settings. Keep the master track switch at **O**, click **Disconnect** and close any other program using the port.
2. Open the extracted `firmware` folder in VS Code with [PlatformIO IDE](https://docs.platformio.org/en/latest/integration/ide/vscode.html), or use PlatformIO Core.
3. Run the commands below. Replace `COM3` with the actual port.

   ```powershell
   pio device list
   pio run -e fuji_mega_three_r2 -t upload --upload-port COM3
   ```

4. Wait for the upload and verification to finish. Reconnect the console with the master track switch still at O. Confirm that all three PWM outputs are zero and check the saved settings. Switch off the controller, unplug USB and refit the JP3201 cap. Restore power and test one track at low speed before testing the others.

Standard R2 firmware brakes to the standing-light duty cycle, keeps that output during the stop and reverses halfway through the dwell. The console supports the `@CFG 3` settings protocol for knob inversion.
