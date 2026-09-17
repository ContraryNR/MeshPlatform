'use strict';
// MeshPlatform 管理页 —— 订阅 SSE 拓扑推送并渲染手写 SVG 拓扑图(零第三方依赖)
// 数据形态: {ts, nodes:[{hostNum,hostName}],
//   edges:[{a,b,channels:[{ch,
//      a:{rtt,up,down,buffered,netPath,iceState,candLocal,candRemote},
//      b:{同左}}]}]}
// 服务端只归档与转发,不合并两端观测:同一通道两端各自的读数原样并列(up/down 是该端自己的方向读数,
// rtt 是该端自己测的,buffered 是该端自己的发送队列)。因此页面按"选中节点"的视角展示:
// 选中谁就只看它作为上报方的那一份(节点详情与质量表格都跟随);某端对象不存在 = 该端没报该通道。
// 唯一不跟随选中的是拓扑那条线(一条边只有一条线,固定取 hostNum 较小端,图例里已写明)。
// 裸边(客户端"仅连接数量"模式)只有 a/b,无 channels,拓扑灰显、不进质量表格。

const svg = document.getElementById('topo');
const detailBody = document.getElementById('detail-body');
const tableBody = document.querySelector('#edge-table tbody');
const tableCaptionEl = document.getElementById('table-caption');
const emptyHint = document.getElementById('empty-hint');
const nodeCountEl = document.getElementById('node-count');
const updateTimeEl = document.getElementById('update-time');
const connStateEl = document.getElementById('conn-state');

const SVG_NS = 'http://www.w3.org/2000/svg';
const CENTER = { x: 400, y: 300 };
const CH_NAMES = { 0: '主通道', 1: '文件传输', 2: '音频', 3: '视频' };
const NET_PATH_NAMES = { lan: '局域网', wan: '公网', relay: '中继' };
let topo = null;      // 最近一次快照
let selected = null;  // 当前选中的 hostNum

// ---------- SSE 订阅 ----------
const source = new EventSource('/stats/stream');
source.addEventListener('topology', e => {
  topo = JSON.parse(e.data);
  render();
});
source.onopen = () => connStateEl.classList.replace('offline', 'online');
source.onerror = () => connStateEl.classList.replace('online', 'offline'); // EventSource 会自动重连

// ---------- 渲染主流程 ----------
function render() {
  if (!topo) return;
  nodeCountEl.textContent = '在线节点 ' + topo.nodes.length;
  updateTimeEl.textContent = '更新于 ' + new Date(topo.ts).toLocaleTimeString('zh-CN');
  renderTopo();
  renderDetail();
  renderTable();
}

// ---------- 拓扑图 ----------
function renderTopo() {
  svg.textContent = '';
  if (!topo.nodes.length) {
    const text = el('text', { x: CENTER.x, y: CENTER.y, class: 'empty-text', 'text-anchor': 'middle' });
    text.textContent = '暂无在线节点';
    svg.appendChild(text);
    return;
  }
  const pos = layout(topo.nodes);

  // 先画边(在下层)
  for (const edge of topo.edges) {
    const pa = pos[edge.a], pb = pos[edge.b];
    if (!pa || !pb) continue;
    // 一条线只能有一个颜色:固定取 hostNum 较小端(a)视角的代表通道 RTT(a 端此边没数据时退到 b 端)。
    // 它只是线上那个数字与颜色,不改变任何数据 —— 分端分通道的数据在表格与详情里逐条可查。
    const v = lineView(edge);
    const rtt = v ? v.side.rtt : null;
    const color = rttColor(rtt);
    svg.appendChild(el('line', {
      x1: pa.x, y1: pa.y, x2: pb.x, y2: pb.y,
      stroke: color, 'stroke-width': 2.5, 'stroke-opacity': 0.85
    }));
    if (rtt != null) { // RTT 标在边中点,带深色描边做衬底
      const label = el('text', {
        x: (pa.x + pb.x) / 2, y: (pa.y + pb.y) / 2 - 6,
        class: 'edge-label', fill: color, 'text-anchor': 'middle'
      });
      label.textContent = rtt + ' ms';
      svg.appendChild(label);
    }
  }

  // 再画节点(在上层)
  for (const node of topo.nodes) {
    const p = pos[node.hostNum];
    const g = el('g', { class: 'node' + (node.hostNum === selected ? ' selected' : '') });
    g.appendChild(el('circle', { cx: p.x, cy: p.y, r: 26 }));
    const num = el('text', { x: p.x, y: p.y + 5, class: 'node-num', 'text-anchor': 'middle' });
    num.textContent = node.hostNum;
    const name = el('text', { x: p.x, y: p.y + 44, class: 'node-name', 'text-anchor': 'middle' });
    name.textContent = node.hostName;
    g.appendChild(num);
    g.appendChild(name);
    g.addEventListener('click', () => { selected = (selected === node.hostNum ? null : node.hostNum); render(); });
    svg.appendChild(g);
  }
}

// 圆周布局:1 个节点居中,n 个节点等角分布在圆周上
function layout(nodes) {
  const pos = {};
  if (nodes.length === 1) {
    pos[nodes[0].hostNum] = { x: CENTER.x, y: CENTER.y };
    return pos;
  }
  const r = Math.min(250, 80 + nodes.length * 26);
  nodes.forEach((node, i) => {
    const angle = (i / nodes.length) * Math.PI * 2 - Math.PI / 2;
    pos[node.hostNum] = { x: CENTER.x + r * Math.cos(angle), y: CENTER.y + r * Math.sin(angle) };
  });
  return pos;
}

// ---------- 节点详情面板 ----------
function renderDetail() {
  detailBody.textContent = '';
  if (selected == null) {
    detailBody.appendChild(hint('点击拓扑中的节点查看详情'));
    return;
  }
  const node = topo.nodes.find(n => n.hostNum === selected);
  if (!node) { // 选中节点已下线
    selected = null;
    detailBody.appendChild(hint('节点已下线'));
    return;
  }
  const nameRow = el2('div', {});
  nameRow.appendChild(kv('主机', node.hostName + ' (#' + node.hostNum + ')'));
  detailBody.appendChild(nameRow);

  const related = topo.edges.filter(e => e.a === selected || e.b === selected);
  if (!related.length) {
    detailBody.appendChild(hint('尚无 P2P 连接(孤立节点)'));
    return;
  }
  for (const e of related) {
    const mySide = e.a === selected ? 'a' : 'b';// 选中端在这条边上的那一份观测
    const peerNum = e.a === selected ? e.b : e.a;
    const peer = topo.nodes.find(n => n.hostNum === peerNum);
    const card = el2('div', { class: 'edge-card' });
    card.appendChild(kv('对端', (peer ? peer.hostName : '未知') + ' (#' + peerNum + ')'));
    const chs = e.channels || [];
    if (!chs.length) { // 裸边:客户端处于"仅连接数量"模式,没有质量字段
      card.appendChild(hint('仅连接数量模式:未上报质量字段'));
      detailBody.appendChild(card);
      continue;
    }
    for (const c of chs) {
      card.appendChild(el2('div', { class: 'divider' }));
      const head = el2('div', { class: 'ch-line' });
      head.appendChild(span('ch-name', CH_NAMES[c.ch] || ('通道 ' + c.ch)));
      card.appendChild(head);
      // 只看选中端自己报的那一份:up/down 就是该端的发出/收到,不镜像、不兜底、不取平均
      const s = c[mySide];
      if (!s) {
        card.appendChild(hint('该端未上报此通道(对端有数据)'));
        continue;
      }
      card.appendChild(kv('RTT', s.rtt != null ? s.rtt + ' ms' : '—'));
      card.appendChild(kv('发送 → 对端', fmtRate(s.up)));
      card.appendChild(kv('接收 ← 对端', fmtRate(s.down)));
      card.appendChild(kv('发送积压', s.buffered != null ? fmtBytes(s.buffered) : '—'));
      card.appendChild(kv('ICE 状态', s.iceState || '—'));
      card.appendChild(kv('路径', netPathName(s.netPath)));
      // 选中候选对(诊断):路径判定依据是"对端地址是否与本机同网段",这里直接给出该端实际选中的候选
      if (s.candLocal || s.candRemote)
        card.appendChild(kv('候选对', (s.candLocal || '—') + ' → ' + (s.candRemote || '—')));
    }
    detailBody.appendChild(card);
  }
}

// ---------- 质量表格 ----------
// 一行 = 选中节点视角下"某条边的一个通道":所有数值都取自选中节点自己上报的那一份。
// A→B / B→A 两列按物理方向落座(选中端是 b 时,它报的 down 就是 A→B),两列始终形容同一对方向,
// 换个节点选中只是换一组计数器来看同一对方向。未选中节点时表格为空。
function renderTable() {
  tableBody.textContent = '';
  const node = selected != null ? topo.nodes.find(n => n.hostNum === selected) : null;
  const rows = [];
  if (node) {
    for (const e of topo.edges) {
      if (e.a !== selected && e.b !== selected) continue;
      const mySide = e.a === selected ? 'a' : 'b';
      for (const c of e.channels || [])
        rows.push({ e, c, mySide });
    }
  }
  tableCaptionEl.textContent = node
      ? ('连接质量(视角:' + node.hostName + ' #' + node.hostNum + ')')
      : '连接质量';
  emptyHint.style.display = rows.length ? 'none' : 'block';
  emptyHint.textContent = node ? '该节点当前没有可展示的通道数据'
                               : '点击拓扑中的节点,查看该节点视角的连接质量';
  for (const { e, c, mySide } of rows) {
    const na = topo.nodes.find(n => n.hostNum === e.a);
    const nb = topo.nodes.find(n => n.hostNum === e.b);
    const s = c[mySide];//选中端自己的那份观测(该端没报此通道时为空)
    const ab = s ? (e.a === selected ? s.up : s.down) : null;//A → B 方向
    const ba = s ? (e.a === selected ? s.down : s.up) : null;//B → A 方向
    const tr = document.createElement('tr');
    tr.append(
      td((na ? na.hostName : '未知') + ' #' + e.a),
      td((nb ? nb.hostName : '未知') + ' #' + e.b),
      td(CH_NAMES[c.ch] || ('通道 ' + c.ch)),
      td(s && s.rtt != null ? s.rtt + ' ms' : '—', s ? rttColor(s.rtt) : null),
      td(fmtRate(ab)),
      td(fmtRate(ba)),
      td(s && s.buffered != null ? fmtBytes(s.buffered) : '—'),
      td(s && s.iceState ? s.iceState : '—'),
      td(netPathName(s ? s.netPath : null))
    );
    tableBody.appendChild(tr);
  }
}

// ---------- 上报配置(server 侧统一调控;改配置需要管理员身份) ----------
const cfgForm = document.getElementById('config-form');
const cfgMsg = document.getElementById('cfg-msg');
const cfgEnabled = document.getElementById('cfg-enabled');
const cfgInterval = document.getElementById('cfg-interval');
const cfgFieldsBox = document.getElementById('cfg-fields');
const FIELD_NAMES = { rtt: 'RTT', traffic: '流量速率', buffered: '发送积压',
                      state: '连接状态', ice: 'ICE 状态', path: '路径 + 候选对' };
let allowedFields = [];

// 生成字段复选框
function buildChoices(container, items, nameOf, checked) {
  container.textContent = '';
  for (const item of items) {
    const label = document.createElement('label');
    label.className = 'cfg-choice';
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.value = item;
    box.checked = checked.has(item);
    label.appendChild(box);
    label.appendChild(document.createTextNode(nameOf(item)));
    container.appendChild(label);
  }
}
function readChoices(container) {
  return [...container.querySelectorAll('input:checked')].map(b => b.value);
}

async function loadConfig() {
  try {
    const cfg = await (await fetch('/stats/config')).json();
    cfgEnabled.checked = !!cfg.enabled;
    cfgInterval.value = cfg.interval;
    allowedFields = cfg.allowedFields || Object.keys(FIELD_NAMES);
    buildChoices(cfgFieldsBox, allowedFields, v => FIELD_NAMES[v] || v, new Set(cfg.fields || []));
  } catch (e) {
    cfgMsg.textContent = '读取配置失败: ' + e.message;
  }
}

// 管理员凭据只留在本次会话(关闭标签页即失效),避免明文落盘
function authHeader() {
  const u = sessionStorage.getItem('meshAdminUser'), p = sessionStorage.getItem('meshAdminPass');
  return u ? { Authorization: 'Basic ' + btoa(u + ':' + p) } : {};
}

cfgForm.addEventListener('submit', async ev => {
  ev.preventDefault();
  const user = document.getElementById('cfg-user').value.trim();
  const pass = document.getElementById('cfg-pass').value;
  if (user) sessionStorage.setItem('meshAdminUser', user);
  if (pass) sessionStorage.setItem('meshAdminPass', pass);
  //请求体用 JSON:fields 是真正的字符串数组,与服务端的 Set<String> 一一对应(不再拼逗号串)
  const body = JSON.stringify({
    enabled: cfgEnabled.checked,
    interval: Number(cfgInterval.value),
    fields: readChoices(cfgFieldsBox),
  });
  try {
    const r = await fetch('/stats/config', {
      method: 'POST',
      headers: { ...authHeader(), 'Content-Type': 'application/json' },
      body,
    });
    if (r.status === 401) { cfgMsg.textContent = '需要管理员身份(或账号密码不正确)'; return; }
    if (!r.ok) { cfgMsg.textContent = '下发失败: HTTP ' + r.status; return; }
    const cfg = await r.json();
    cfgMsg.textContent = '已下发(周期 ' + cfg.interval + 'ms)';
    await loadConfig();
  } catch (e) {
    cfgMsg.textContent = '下发失败: ' + e.message;
  }
});

loadConfig();

// ---------- 工具函数 ----------
function el(tag, attrs) { // SVG 命名空间
  const node = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) node.setAttribute(k, attrs[k]);
  return node;
}
function el2(tag, attrs) { // HTML 命名空间
  const node = document.createElement(tag);
  for (const k in attrs) node.className = attrs[k];
  return node;
}
function span(cls, text) {
  const s = document.createElement('span');
  s.className = cls;
  s.textContent = text;
  return s;
}
function td(text, color) {
  const cell = document.createElement('td');
  if (color) cell.style.color = color;
  cell.textContent = text;
  return cell;
}
function kv(k, v) {
  const div = document.createElement('div');
  div.className = 'kv';
  const key = document.createElement('span');
  key.className = 'kv-k';
  key.textContent = k;
  const val = document.createElement('span');
  val.className = 'kv-v';
  val.textContent = v;
  div.append(key, val);
  return div;
}
function hint(text) {
  const div = document.createElement('div');
  div.className = 'empty-hint';
  div.textContent = text;
  return div;
}
// 拓扑线取谁的数字:固定用 hostNum 较小端(a)视角 —— 该端在此边上的代表通道(主通道优先,否则通道号最小);
// a 端整条边都没数据时退到 b 端,免得线假灰。它只决定线的颜色与那个数字,不改动任何数据。
function lineView(edge) {
  return pickSide(edge, 'a') || pickSide(edge, 'b');
}
function pickSide(edge, key) {
  const chs = (edge.channels || []).filter(c => c[key]);
  if (!chs.length) return null;
  const c = chs.find(x => x.ch === 0) || chs[0];
  return { ch: c.ch, side: c[key] };
}
function rttColor(rtt) {
  if (rtt == null) return '#6b7280';
  if (rtt < 100) return '#22c55e';
  if (rtt < 300) return '#f59e0b';
  return '#ef4444';
}
function netPathName(p) {
  return p ? (NET_PATH_NAMES[p] || p) : '—';
}
function fmtRate(bps) {
  if (bps == null) return '—'; // 裸边无质量数据
  if (!bps) return '0 B/s';
  if (bps < 1024) return bps + ' B/s';
  if (bps < 1024 * 1024) return (bps / 1024).toFixed(1) + ' KB/s';
  return (bps / 1024 / 1024).toFixed(2) + ' MB/s';
}
function fmtBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1024 / 1024).toFixed(2) + ' MB';
}
