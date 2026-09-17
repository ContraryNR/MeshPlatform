# 缺省语义回归:未上报的字段组不应以 0 出现在拓扑里,真 0 必须保留为 0
# (配合 mesh_ws_test2.py 一起跑:那个覆盖合并/裸边,这个覆盖缺省与真 0 的区分)
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

sa, ca = connect(); send_text(sa, json.dumps({"type": "hostname", "hostname": "sim-x"}))
ra, ca = recv_text(sa, ca); na = json.loads(ra)["hostNum"]
sb, cb = connect(); send_text(sb, json.dumps({"type": "hostname", "hostname": "sim-y"}))
rb, cb = recv_text(sb, cb); nb = json.loads(rb)["hostNum"]
print(f"sim-x={na} sim-y={nb}")
kx = "a" if na < nb else "b"   # sim-x 的观测落在哪个键下(sim-y 全程不报送,故另一端应缺席)

# 情形1:只配了 rtt(不报流量) —— 该端对象里应当只有 rtt,且未上报的那端整个不出现
send_text(sa, json.dumps({"type": "stats", "target": 1, "source": na,
                          "edges": [{"peer": nb, "ch": 0, "rtt": 40}]}))
time.sleep(1)
e = latest()["edges"][0]
assert set(e.keys()) == {"a", "b", "channels"}, e  # 边级不得出现任何汇总字段
assert e["channels"] == [{"ch": 0, kx: {"rtt": 40}}], e
print("情形1(仅 rtt):", json.dumps(e, ensure_ascii=False))

# 情形2:换成只配 traffic+buffered(不报 rtt) —— 覆盖同一(边,端,通道),rtt 必须消失
send_text(sa, json.dumps({"type": "stats", "target": 1, "source": na,
                          "edges": [{"peer": nb, "ch": 0, "up": 100, "down": 200, "buffered": 5}]}))
time.sleep(1)
e = latest()["edges"][0]
c0 = e["channels"][0]
assert set(c0.keys()) == {"ch", kx}, e             # 未上报的那端不补空对象
s = c0[kx]
assert "rtt" not in s, e
assert s["up"] == 100 and s["down"] == 200, e      # 该端自己的发出/收到
assert s["buffered"] == 5, e
assert "netPath" not in s and "iceState" not in s, e
print("情形2(仅 traffic+buffered):", json.dumps(e, ensure_ascii=False))

# 情形3:真 0 必须保留为 0(不能与缺省混为一谈)
send_text(sa, json.dumps({"type": "stats", "target": 1, "source": na,
                          "edges": [{"peer": nb, "ch": 0, "rtt": 30, "up": 0, "down": 0, "buffered": 0}]}))
time.sleep(1)
e = latest()["edges"][0]
s = e["channels"][0][kx]
assert s["up"] == 0 and s["down"] == 0 and s["buffered"] == 0, e
assert s["rtt"] == 30, e
assert "netPath" not in s, e
print("情形3(真 0 与缺省并存):", json.dumps(e, ensure_ascii=False))
print("缺省语义验证全部通过")

sa.close(); sb.close()
