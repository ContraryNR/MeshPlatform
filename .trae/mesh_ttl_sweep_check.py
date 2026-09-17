# TTL 自检回归:连接保持、只是不再上报 —— 仓储应自行摘净节点与边,并把这次变化推给页面
# (配合 mesh_ws_test2.py 一起跑:那个覆盖合并/裸边/断连清理,这个覆盖"没有上报也有心跳")
import base64, json, os, socket, struct, time, urllib.request

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

def latest():
    return json.loads(urllib.request.urlopen(f"http://{HOST}:{PORT}/stats/latest", timeout=5).read().decode())

sa, ca = connect(); send_text(sa, json.dumps({"type": "hostname", "hostname": "ttl-probe"}))
ra, ca = recv_text(sa, ca); na = json.loads(ra)["hostNum"]
print(f"ttl-probe = {na}", flush=True)

# 对端 200 从不连接:这条边只有本端一支观测,且下面不会有任何断连(removeNode 不掺和),
# 于是"清理"只可能来自仓储自己的 tick 自检
send_text(sa, json.dumps({"type": "stats", "target": 1, "source": na,
                          "edges": [{"peer": 200, "ch": 0, "rtt": 33, "up": 10, "down": 20}]}))
time.sleep(2)
snap = latest()
print("上报后:", json.dumps(snap, ensure_ascii=False), flush=True)
assert snap["nodes"] and snap["edges"], snap

time.sleep(18)  # 连接保持不动,只是不再上报(默认 TTL = 5s×3 = 15s)
snap = latest()
print("停报 20s 后:", json.dumps(snap, ensure_ascii=False), flush=True)
assert snap["nodes"] == [] and snap["edges"] == [], snap
print("TTL 自检通过:无上报、无断连,节点与边都被自行摘净", flush=True)
