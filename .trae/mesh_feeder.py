# 页面视觉验证用的数据泵:两个模拟节点持续上报多通道 stats,验证完即删
import base64, json, os, socket, struct, time

HOST, PORT = "127.0.0.1", 8080

def connect():
    s = socket.create_connection((HOST, PORT), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (f"GET /ws HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nUpgrade: websocket\r\n"
           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
    s.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        resp += s.recv(4096)
    assert "101" in resp.split(b"\r\n")[0].decode()
    return s, resp.split(b"\r\n\r\n", 1)[1]

def send_text(s, text):
    payload = text.encode()
    mask = os.urandom(4)
    header = bytes([0x81])
    n = len(payload)
    if n < 126:
        header += bytes([0x80 | n])
    else:
        header += bytes([0x80 | 126]) + struct.pack(">H", n)
    s.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

def recv_text(s, carry=b""):
    buf = carry
    while True:
        while len(buf) < 2:
            buf += s.recv(4096)
        opcode = buf[0] & 0x0F
        length = buf[1] & 0x7F
        off = 2
        if length == 126:
            while len(buf) < 4: buf += s.recv(4096)
            length = struct.unpack(">H", buf[2:4])[0]; off = 4
        while len(buf) < off + length:
            buf += s.recv(4096)
        payload, buf = buf[off:off+length], buf[off+length:]
        if opcode == 0x1:
            return payload.decode(), buf
        if opcode == 0x8:
            return None, buf

sa, ca = connect(); send_text(sa, json.dumps({"type": "hostname", "hostname": "desktop-a"}))
ra, ca = recv_text(sa, ca); numa = json.loads(ra)["hostNum"]
sb, cb = connect(); send_text(sb, json.dumps({"type": "hostname", "hostname": "laptop-b"}))
rb, cb = recv_text(sb, cb); numb = json.loads(rb)["hostNum"]
print(f"feeder up: desktop-a={numa} laptop-b={numb}", flush=True)

for i in range(30):  # 30 轮 x 5s = 150s
    send_text(sa, json.dumps({"type": "stats", "target": 1, "source": numa, "edges": [
        {"peer": numb, "ch": 0, "rtt": 38 + i % 5, "up": 1200 + i * 10, "down": 800, "buffered": 0,
         "state": "connected", "iceState": "completed", "netPath": "lan"},
        {"peer": numb, "ch": 1, "rtt": 42, "up": 400000 + i * 5000, "down": 1000, "buffered": 1024 * (i % 3),
         "state": "connected", "iceState": "completed", "netPath": "lan"}]}))
    send_text(sb, json.dumps({"type": "stats", "target": 1, "source": numb, "edges": [
        {"peer": numa, "ch": 0, "rtt": 41 + i % 4, "up": 800, "down": 1200, "buffered": 0,
         "state": "connected", "iceState": "completed", "netPath": "lan"}]}))
    time.sleep(5)
print("feeder done", flush=True)
