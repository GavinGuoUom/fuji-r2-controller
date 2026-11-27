# Fuji R2 Serial Console

[简体中文](README.md) | English

On Windows, double-click `launch.cmd` to open the console in your browser. The launcher prefers the PlatformIO Python environment when present. You can also use Python 3.10 or later. From the repository root, run `python -m pip install -r software/requirements.txt`, then `python software/console.py`. Open [the local console](http://127.0.0.1:8765/) if the browser does not open. Close the launcher window or press Ctrl+C to exit.

Use the language selector at the top right to switch between Simplified Chinese and English. The browser remembers your choice.

## Connect the controller

1. Set the master track switch to **O**, switch off controller power and confirm that USB is disconnected.
2. **Remove the JP3201 jumper cap** from the R2 shield before connecting a standard USB data cable. Leave the cap off while USB is connected. If tuning requires controller power, switch it on and leave the master track switch at **O**.
3. Close other apps using the serial port. Select the Mega's port and click **Connect**. The link uses 115200 baud, 8N1. Choose **Offline Demo** to explore the console without a controller.
4. When finished, click **Disconnect**, switch off controller power and unplug USB. Then refit the JP3201 cap.

## Use the console

- **Monitor** shows track output permission, faults, directions, PWM, standing-light duty, configured braking time, raw ADC and current readings, and end-sensor counts for all three tracks. The panel refreshes approximately every 250 ms. Direction and PWM show the controller's commands.
- **Guided Test** walks through the master switch, knobs, Start/Pause button, end sensors, track output and fault light. It shows live knob readings, button levels and counts, and sensor information. The console detects supported steps automatically. Confirm visual checks manually.
- **Live Tuning** sets the standing-light limit, dwell and acceleration ranges, sensor clearance and maximum braking time. Enter the shortest measured clearance across the six end sensors in mm, or 0 if unmeasured. The timing knob maps braking time from 0 to the maximum setting. The controller uses the value in effect when it detects the end sensor. Verify stopping clearance with the actual train at its highest operating speed. **Apply Now** takes effect immediately. Set the master track switch to **O** before using **Save to Controller**.

Use the physical panel to start and pause tracks. Simulated inputs in Offline Demo affect only the demo.

If replacement knobs read backwards, open **Live Tuning → Knob direction** and check those knobs, or click **Invert all** if all nine are reversed. Keep the master track switch at **O**, click **Apply Now**, then **Save to Controller**. In Guided Test, verify 0% fully counterclockwise and 100% fully clockwise. Inversion uses the existing calibration endpoints.

## Firmware compatibility

The console reads the existing R2 text status protocol. End-sensor braking and clearance require `@CFG 2` or newer. Knob inversion requires `@CFG 3`. Use PlatformIO environment `fuji_mega_three_r2` and disconnect the serial port in the app before uploading.
