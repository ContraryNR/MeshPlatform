# MeshPlatform stats 协议验证:两端观测并列(不做平均/镜像/取大) + netPath + 模式一裸边
# 仅标准库裸 socket WebSocket
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
    head = resp.split(b"\r\n\r\n")[0].decode()
    assert "101" in head.split("\r\n")[0], head
    return s, resp.split(b"\r\n\r\n", 1)[1]

def send_text(s, text):
    payload = text.encode()
    mask = os.urandom(4)
    header = bytes([0x81])
    n = len(payload)
    if n < 126:
        header += bytes([0x80 | n])
    elif n < 65536:
        header += bytes([0x80 | 126]) + struct.pack(">H", n)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    s.sendall(header + mask + masked)

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
        elif length == 127:
            while len(buf) < 10: buf += s.recv(4096)
            length = struct.unpack(">Q", buf[2:10])[0]; off = 10
        while len(buf) < off + length:
            buf += s.recv(4096)
        payload, buf = buf[off:off+length], buf[off+length:]
        if opcode == 0x1:
            return payload.decode(), buf
        if opcode == 0x8:
            return None, buf

def http_get(path):
    return urllib.request.urlopen(f"http://{HOST}:{PORT}{path}", timeout=5).read().decode()

def close(s):
    s.sendall(bytes([0x88, 0x80]) + os.urandom(4))
    s.close()

sa, ca = connect(); send_text(sa, json.dumps({"type": "hostname", "hostname": "sim-a"}))
ra, ca = recv_text(sa, ca); numa = json.loads(ra)["hostNum"]
sb, cb = connect(); send_text(sb, json.dumps({"type": "hostname", "hostname": "sim-b"}))
rb, cb = recv_text(sb, cb); numb = json.loads(rb)["hostNum"]
print(f"sim-a={numa} sim-b={numb}")

# sim-a 上报两条通道:ch0 主通道(lan,rtt=40)+ ch1 文件通道(wan,有速率)
stats_a = {"type": "stats", "target": 1, "source": numa, "edges": [
    {"peer": numb, "ch": 0, "rtt": 40, "up": 1000, "down": 2000, "buffered": 0,
     "state": "connected", "iceState": "completed", "netPath": "lan"},
    {"peer": numb, "ch": 1, "rtt": 60, "up": 50000, "down": 1000, "buffered": 2048,
     "state": "connected", "iceState": "completed", "netPath": "wan"}]}
# sim-b 反向只报 ch0(rtt=50 => 主通道平均应为 45)
stats_b = {"type": "stats", "target": 1, "source": numb, "edges": [
    {"peer": numa, "ch": 0, "rtt": 50, "up": 2000, "down": 1000, "buffered": 0,
     "state": "connected", "iceState": "completed", "netPath": "lan"}]}
send_text(sa, json.dumps(stats_a)); send_text(sb, json.dumps(stats_b))
time.sleep(1)
latest = json.loads(http_get("/stats/latest"))
print("双通道边:", json.dumps(latest["edges"], ensure_ascii=False))
e = latest["edges"][0]
assert set(e.keys()) == {"a", "b", "channels"}, e            # 边级不再有任何汇总字段
chs = {c["ch"]: c for c in e["channels"]}
assert sorted(chs) == [0, 1], e                              # 通道并集,升序
c0, c1 = chs[0], chs[1]
# 通道对象内两端各自观测并列:键就是两端 hostNum(sim-a / sim-b 谁小谁是 a)
ka = "a" if numa < numb else "b"                             # sim-a 的观测落在哪个键下
kb = "b" if numa < numb else "a"                             # sim-b
sa0, sb0 = c0[ka], c0[kb]
assert set(c0.keys()) == {"ch", "a", "b"}, e                 # ch0 两端都报过 -> 两份都在
assert sa0["rtt"] == 40 and sb0["rtt"] == 50, e              # 各报各的:不取平均(旧行为是 45)
assert sa0["up"] == 1000 and sa0["down"] == 2000, e          # up/down 是该端自己的发出/收到
assert sb0["up"] == 2000 and sb0["down"] == 1000, e          # 不跨端镜像兜底
assert sa0["buffered"] == 0 and sb0["buffered"] == 0, e      # 真 0 保留为 0
assert sa0["netPath"] == "lan" and sb0["netPath"] == "lan", e
assert sa0["iceState"] == "completed" and sb0["iceState"] == "completed", e
# 只有 sim-a 报过 ch1:只出现它那一端,另一端不补空对象
assert set(c1.keys()) == {"ch", ka}, e
sa1 = c1[ka]
assert sa1["rtt"] == 60 and sa1["buffered"] == 2048, e
assert sa1["up"] == 50000 and sa1["down"] == 1000, e
assert sa1["netPath"] == "wan", e
print("断言通过: 两端观测并列各自原样/通道并集/无跨通道汇总/无跨端兜底")

# 模式一裸边:sim-c 只报存在性
sc, cc = connect(); send_text(sc, json.dumps({"type": "hostname", "hostname": "sim-c"}))
rc, cc = recv_text(sc, cc); numc = json.loads(rc)["hostNum"]
send_text(sc, json.dumps({"type": "stats", "target": 1, "source": numc, "edges": [{"peer": numa}]}))
time.sleep(1)
latest = json.loads(http_get("/stats/latest"))
bare = [e for e in latest["edges"] if "channels" not in e]  # 裸边判据:无 channels
assert len(bare) == 1 and set(bare[0].keys()) == {"a", "b"}, bare
print("模式一裸边:", json.dumps(bare[0]))

close(sa); close(sb); close(sc)
time.sleep(1)
print("全部断连后节点数:", len(json.loads(http_get("/stats/latest"))["nodes"]))
