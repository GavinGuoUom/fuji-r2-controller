"""Local Fuji R2 serial console. Run with Python 3.10+ and pyserial."""

import argparse
import copy
import json
import re
import socket
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = list_ports = None

ROOT = Path(__file__).resolve().parent
DEFAULT_TUNING = [300, 200, 60000, 200, 3000, 0, 0, 0]
CONFIG = re.compile(r"@CFG ([123]) (\d+(?: +\d+)*)")
CONFIG_FIELDS = {"1": 5, "2": 7, "3": 8}
STATUS = re.compile(r"R2 totalN=(\d) porN=(\d) busN=(\d) sink=(\d) lat=(\d) en=(\d) F=(\d+) mask=(\d+)")
TRACK = re.compile(r"R([123]) state=(\d+) dir=([LR]) pwm=(\d+) goal=(\d+) min=(\d+) time=(\d+)ms")
ADC = re.compile(r"R([123]) ADC speed=(\d+) time=(\d+) min=(\d+) Iraw=(\d+)(?: dwell=(\d+)ms)?")
BRAKE = re.compile(r"R([123]) brake=(\d+)ms(?: pots=(\d+),(\d+),(\d+))?")
INPUTS = re.compile(r"R([123]) keyN=(\d) L=(\d) R=(\d) hits\(key,L,R\)=(\d+),(\d+),(\d+)")


def fields(names, values):
    return dict(zip(names.split(), map(int, values)))


class FrameReader:
    """Only publish complete status groups. Discard interrupted groups on resync."""

    pending = None

    def feed(self, line):
        if match := STATUS.fullmatch(line):
            self.pending = {
                "system": fields("total_n por_n bus_n sink latch enabled fault mask", match.groups()),
                "tracks": [{}, {}, {}],
            }
            return
        if self.pending is None:
            return
        if match := TRACK.fullmatch(line):
            track, state, direction, *values = match.groups()
            self.pending["tracks"][int(track) - 1].update(
                state=int(state), direction=direction, **fields("pwm goal minimum ramp", values)
            )
        elif match := ADC.fullmatch(line):
            track, speed, timer, lighting, current, dwell = match.groups()
            self.pending["tracks"][int(track) - 1].update(
                adc=list(map(int, (speed, timer, lighting))),
                current=int(current), dwell=int(dwell) if dwell is not None else None,
            )
        elif match := BRAKE.fullmatch(line):
            track, duration, *rotaries = match.groups()
            row = self.pending["tracks"][int(track) - 1]
            row["brake"] = int(duration)
            if rotaries[0] is not None:
                row["rotaries"] = list(map(int, rotaries))
        elif match := INPUTS.fullmatch(line):
            track, key, left, right, *counts = map(int, match.groups())
            self.pending["tracks"][track - 1].update(levels=[1-key, left, right], hits=counts)
            if track == 3:
                frame, self.pending = self.pending, None
                if all({"state", "adc", "hits"} <= row.keys() for row in frame["tracks"]):
                    return frame


def open_serial(device):
    port = serial.Serial(baudrate=115200, timeout=0, write_timeout=0.1)
    port.port = device
    port.dtr = port.rts = False
    try:
        port.open()
    except serial.SerialException as original:
        # The bench CH340 driver returns error 31 even after setting the DCB.
        # Fallback is valid only if Windows confirms an exact 115200/8N1 readback.
        if getattr(getattr(original, "__context__", None), "winerror", None) != 31 and "31)" not in str(original):
            raise
        from ctypes import byref, sizeof
        from serial import win32

        set_state = win32.SetCommState

        def confirm_state(handle, request):
            if set_state(handle, request):
                return 1
            current = win32.DCB()
            current.DCBlength = sizeof(current)
            if win32.GetCommState(handle, byref(current)):
                actual = current.BaudRate, current.ByteSize, current.Parity, current.StopBits
                return int(actual == (115200, 8, 0, 0))
            return 0

        win32.SetCommState = confirm_state
        try:
            port.open()
        finally:
            win32.SetCommState = set_state
    return port


def validate_tuning(values, count):
    if not isinstance(values, list) or len(values) != count or any(type(x) is not int for x in values):
        raise ValueError(f"需要 {count} 个整数参数")
    light, dwell_min, dwell_max, ramp_min, ramp_max = values[:5]
    if not (0 <= light <= 1000 and 0 <= dwell_min <= dwell_max <= 60000
            and 1 <= ramp_min <= ramp_max <= 60000):
        raise ValueError("参数超出范围，或最小值大于最大值")
    if count >= 7 and not (0 <= values[5] <= 9999 and 0 <= values[6] <= 60000):
        raise ValueError("余距或减速时间超出范围")
    if count == 8 and not 0 <= values[7] <= 511:
        raise ValueError("旋钮方向设置超出范围")
    return values


class ControllerLink:
    def __init__(self):
        self.lock = threading.RLock()
        self.connection_lock = threading.Lock()
        self.thread = None
        self.stop = threading.Event()
        self.pending = None
        self.epoch = 0
        self.seq = 0
        self.mode = "offline"
        self.device = ""
        self.error = ""
        self.reply = ""
        self.frame = None
        self.tuning = None
        self.sampled = 0.0

    def snapshot(self):
        with self.lock:
            return copy.deepcopy({
                "mode": self.mode, "device": self.device, "error": self.error,
                "reply": self.reply, "frame": self.frame, "tuning": self.tuning,
                "age": round(time.monotonic()-self.sampled, 2) if self.frame else None,
                "epoch": self.epoch, "seq": self.seq, "pending": self.pending is not None,
            })

    def connect(self, device):
        with self.connection_lock:
            available = {entry.device for entry in list_ports.comports()} if list_ports else set()
            if device != "demo" and device not in available:
                raise ValueError("找不到选择的串口" if list_ports else "请先安装 pyserial")
            self._disconnect()
            with self.lock:
                self.epoch += 1
                self.error, self.reply, self.device = "", "", device
                self.stop.clear()
                if device == "demo":
                    self.mode = "demo"
                    self.tuning = DEFAULT_TUNING.copy()
                    self.frame = {
                        "system": fields("total_n por_n bus_n sink latch enabled fault mask", [1, 1, 1, 0, 0, 0, 0, 0]),
                        "tracks": [dict(state=0, direction="R", pwm=0, goal=0, minimum=0, ramp=200,
                                        adc=[0, 0, 0], rotaries=[0, 0, 0], current=0, dwell=200, brake=0, levels=[0, 0, 0],
                                        hits=[0, 0, 0]) for _ in range(3)],
                    }
                    self.sampled = time.monotonic()
                    self.seq += 1
                else:
                    self.mode = "connecting"
                    self.thread = threading.Thread(target=self._serial_loop, args=(device,), daemon=True)
                    self.thread.start()

    def disconnect(self):
        with self.connection_lock:
            self._disconnect()

    def _disconnect(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=2)
            if self.thread.is_alive():
                raise ValueError("串口正在关闭，请稍候")
            self.thread = None
        with self.lock:
            self.mode = "offline"
            self.device = self.error = self.reply = ""
            self.pending = self.frame = self.tuning = None

    def _serial_loop(self, device):
        reader = FrameReader()
        try:
            with open_serial(device) as port:
                if self.stop.wait(1.5):
                    return
                port.reset_input_buffer()
                port.write(b"q\n@get\n")
                buffer = bytearray()
                next_status = 0.0
                active = None
                deadline = 0.0
                while not self.stop.is_set():
                    now = time.monotonic()
                    with self.lock:
                        if active and now >= deadline:
                            self.reply = "控制器未确认命令，请重新读取参数"
                            self.pending = None
                            active = None
                        pending = self.pending if not active else None
                    if pending:
                        port.write(pending)
                        active, deadline = pending, now+2.0
                    if now >= next_status:
                        port.write(b"s\n")
                        next_status = now+0.25
                    waiting = port.in_waiting
                    if waiting:
                        buffer.extend(port.read(waiting))
                    while b"\n" in buffer:
                        raw, _, remainder = buffer.partition(b"\n")
                        buffer = bytearray(remainder)
                        line = raw.decode("ascii", errors="replace").strip()
                        with self.lock:
                            if config := CONFIG.fullmatch(line):
                                version, values = config.groups()
                                self.tuning = validate_tuning(list(map(int, values.split())), CONFIG_FIELDS[version])
                                self.reply = "控制器已应用参数" if active and active.startswith(b"@set ") else "已读取控制器当前参数"
                                self.pending = active = None
                            elif line == "@SAVED" or line.startswith("@ERR "):
                                messages = {"@SAVED": "已保存且读回校验通过", "@ERR range": "参数验证失败",
                                            "@ERR master-on": "请把总控拨回 OFF 再保存",
                                            "@ERR busy": "控制器正在保存", "@ERR EEPROM": "EEPROM 校验失败"}
                                self.reply = messages.get(line, line)
                                self.pending = active = None
                            elif frame := reader.feed(line):
                                self.frame, self.sampled = frame, now
                                self.mode = "serial"
                                self.seq += 1
                    if len(buffer)>256:
                        buffer.clear()
                    self.stop.wait(0.015)
        except (OSError, ValueError, serial.SerialException) as exc:
            with self.lock:
                self.error = str(exc)
        finally:
            with self.lock:
                self.mode = "offline"
                self.pending = None

    def command(self, kind, values=None):
        if kind not in ("get", "set", "save"):
            raise ValueError("未知参数操作")
        with self.lock:
            if kind == "set" and self.tuning is not None:
                validate_tuning(values, len(self.tuning))
                if (len(values) == 8 and values[7] != self.tuning[7] and
                        self.frame and not self.frame["system"]["total_n"]):
                    raise ValueError("请把总控拨回 OFF 再修改旋钮方向")
            if self.mode == "demo":
                if kind=="set":
                    self.tuning = values.copy()
                    for index in range(3):
                        self.demo_rotaries(index)
                self.reply = "演示参数已更新" if kind=="set" else "演示模式"
                return
            if self.mode!="serial" or self.pending is not None or time.monotonic()-self.sampled>2:
                raise ValueError("串口未就绪或正在等待控制器")
            if self.tuning is None and kind!="get":
                raise ValueError("当前固件尚未响应调参协议")
            if kind=="save" and not self.frame["system"]["total_n"]:
                raise ValueError("请把总控拨回 OFF 再保存")
            args = " "+" ".join(map(str,values)) if kind=="set" else ""
            self.pending = f"@{kind}{args}\n".encode("ascii")
            self.reply = ""

    def demo_rotaries(self, track):
        row = self.frame["tracks"][track]
        _, dwell_min, dwell_max, ramp_min, ramp_max, _, brake_max, mask = self.tuning
        for index, adc in enumerate(row["adc"]):
            mapped = min(512, max(0, (adc - 8) * 512 // 1007))
            row["rotaries"][index] = 512 - mapped if mask & (1 << (track * 3 + index)) else mapped
        time_setting = row["rotaries"][1]
        row["dwell"] = dwell_min + time_setting * (dwell_max - dwell_min) // 512
        row["ramp"] = ramp_min + time_setting * (ramp_max - ramp_min) // 512
        row["brake"] = time_setting * brake_max // 512

    def demo_action(self, action, track, channel, value):
        with self.lock:
            if self.mode!="demo":
                raise ValueError("演示动作仅用于演示模式")
            system = self.frame["system"]
            if action=="master":
                system["total_n"] ^= 1
                system["enabled"] = 1-system["total_n"]
                for row in self.frame["tracks"]:
                    row["state"] = 2 if system["enabled"] else 0
                    row["pwm"] = 0
            else:
                if track not in (1, 2, 3) or channel not in (0, 1, 2):
                    raise ValueError("无效线路或输入")
                row = self.frame["tracks"][track-1]
                if action=="pot" and type(value) is int and 0<=value<=1023:
                    row["adc"][channel] = value
                    self.demo_rotaries(track - 1)
                elif action=="hit" and system["enabled"]:
                    row["hits"][channel] += 1
                    if channel==0:
                        row["state"] = 2 if row["state"]>=3 else 3
                        row["pwm"] = 160 if row["state"]==3 else 0
                elif action=="output" and system["enabled"]:
                    row["pwm"] = row["minimum"] = 75
            self.sampled = time.monotonic()
            self.seq += 1


class RequestHandler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send(self, status, content, kind="application/json; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(content)

    def json(self, content, status=200):
        self.send(status, json.dumps(content, ensure_ascii=False).encode("utf-8"))

    def local_host(self):
        host = self.headers.get("Host")
        port = self.server.server_port
        return host in (f"127.0.0.1:{port}",f"localhost:{port}")

    def do_GET(self):
        if not self.local_host():
            self.json({"error": "控制台仅接受本机地址"},403)
            return
        if self.path=="/api/state":
            self.json(self.server.link.snapshot())
        elif self.path=="/api/ports":
            ports = [{"device": p.device, "description": p.description} for p in list_ports.comports()] if list_ports else []
            self.json({"ports": ports, "serialInstalled": serial is not None})
        elif self.path in ("/", "/style.css", "/app.js"):
            name, kind = {"/": ("index.html", "text/html"), "/style.css": ("style.css", "text/css"),
                          "/app.js": ("app.js", "application/javascript")}[self.path]
            self.send(200, (ROOT/name).read_bytes(), kind+"; charset=utf-8")
        else:
            self.json({"error": "未找到"}, 404)

    def do_POST(self):
        origin = self.headers.get("Origin")
        if (not self.local_host() or self.headers.get("X-Fuji-Console")!="1" or
                origin not in (None, f"http://{self.headers.get('Host')}")):
            self.json({"error": "请从本机控制台操作"}, 403)
            return
        try:
            size = int(self.headers.get("Content-Length","0"))
            if not 0<size<=1024:
                raise ValueError("无效请求长度")
            body = json.loads(self.rfile.read(size))
            if not isinstance(body,dict):
                raise ValueError("无效请求")
            link = self.server.link
            if self.path=="/api/connect":
                link.connect(body.get("device"))
            elif self.path=="/api/disconnect":
                link.disconnect()
            elif self.path=="/api/tuning":
                link.command(body.get("kind"),body.get("values"))
            elif self.path=="/api/demo":
                link.demo_action(body.get("action"),body.get("track"),body.get("channel"),body.get("value"))
            else:
                self.json({"error": "未找到"},404)
                return
            self.json({"ok": True})
        except (ValueError, TypeError, OSError) as exc:
            self.json({"error": str(exc)},400)


class ConsoleServer(ThreadingHTTPServer):
    # Windows SO_REUSEADDR can let several consoles listen on the same port.
    allow_reuse_address = not hasattr(socket, "SO_EXCLUSIVEADDRUSE")

    def server_bind(self):
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()
    with ConsoleServer(("127.0.0.1",args.port), RequestHandler) as server:
        server.link = ControllerLink()
        url = f"http://127.0.0.1:{server.server_port}"
        print(f"Fuji R2 控制台 · {url}",flush=True)
        if not args.no_open:
            threading.Thread(target=webbrowser.open,args=(url,),daemon=True).start()
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            server.link.disconnect()


if __name__=="__main__":
    main()
