'use strict';
// MeshPlatform 管理页 —— 订阅 SSE 拓扑推送并渲染手写 SVG 拓扑图(零第三方依赖)
// 数据形态: {ts, nodes:[{hostNum,hostName}],
//            edges:[{a,b,channels:[{ch,rtt,up,down,buffered,netPath,iceState,candLocal,candRemote}]}]}
// 数据精确到 channel:服务端不做跨通道汇总(边级的 RTT/速率/路径都没有),页面逐通道展示。
// 裸边(客户端"仅连接数量"模式)只有 a/b,无 channels,拓扑灰显、不进质量表格。

const svg = document.getElementById('topo');
const detailBody = document.getElementById('detail-body');
const tableBody = document.querySelector('#edge-table tbody');
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
    // 一条线只能有一个颜色:取代表通道的 RTT 上色(主通道优先,否则通道号最小的那条)。
    // 这只是页面为"一条线"选的渲染代表,数据本身仍是分通道的,服务端不参与这个取舍。
    // (2026-09-17 复查"跨通道汇总是否删净"时确认:拓扑图保持"一条边一条线"是刻意保留的渲染选择,
    //  分通道数据在质量表格与节点详情里逐条可查 —— 别当漏删改成多条线。)
    const rep = repChannel(edge);
    const rtt = rep ? rep.rtt : null;
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
    const outbound = e.a === selected; // 选中端是否为 a:决定 ↑/↓ 朝向
    const peerNum = outbound ? e.b : e.a;
    const peer = topo.nodes.find(n => n.hostNum === peerNum);
    const card = el2('div', { class: 'edge-card' });
    card.appendChild(kv('对端', (peer ? peer.hostName : '未知') + ' (#' + peerNum + ')'));
    // 服务端只给精确到通道的数据,不再有"整条边"的汇总项 —— 逐通道展示
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
      card.appendChild(kv('RTT', c.rtt != null ? c.rtt + ' ms' : '—'));
      card.appendChild(kv('发送 → 对端', fmtRate(outbound ? c.up : c.down)));
      card.appendChild(kv('接收 ← 对端', fmtRate(outbound ? c.down : c.up)));
      card.appendChild(kv('发送积压', c.buffered != null ? fmtBytes(c.buffered) : '—'));
      card.appendChild(kv('ICE 状态', c.iceState || '—'));
      card.appendChild(kv('路径', netPathName(c.netPath)));
      // 选中候选对(诊断):路径判定依据是"对端地址是否与本机同网段",这里直接给出实际选中的候选
      if (c.candLocal || c.candRemote)
        card.appendChild(kv('候选对', (c.candLocal || '—') + ' → ' + (c.candRemote || '—')));
    }
    detailBody.appendChild(card);
  }
}

// ---------- 质量表格 ----------
function renderTable() {
  tableBody.textContent = '';
  // 一行 = 一条边的某一个通道:服务端只给精确到通道的数据,表格不再有"整条边"的汇总行
  const rows = [];
  for (const e of topo.edges)
    for (const c of e.channels || [])
      rows.push({ e, c });
  emptyHint.style.display = rows.length ? 'none' : 'block';
  for (const { e, c } of rows) {
    const na = topo.nodes.find(n => n.hostNum === e.a);
    const nb = topo.nodes.find(n => n.hostNum === e.b);
    const tr = document.createElement('tr');
    tr.append(
      td((na ? na.hostName : '未知') + ' #' + e.a),
      td((nb ? nb.hostName : '未知') + ' #' + e.b),
      td(CH_NAMES[c.ch] || ('通道 ' + c.ch)),
      td(c.rtt != null ? c.rtt + ' ms' : '—', rttColor(c.rtt)),
      td(fmtRate(c.up)),
      td(fmtRate(c.down)),
      td(c.buffered != null ? fmtBytes(c.buffered) : '—'),
      td(c.iceState || '—'),
      td(netPathName(c.netPath))
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
  const body = new URLSearchParams({
    enabled: cfgEnabled.checked,
    interval: cfgInterval.value,
    fields: readChoices(cfgFieldsBox).join(','),
  });
  try {
    const r = await fetch('/stats/config', { method: 'POST', headers: authHeader(), body });
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
// 为"一条边"选一个展示代表通道(仅拓扑线上色与标注用):主通道优先,否则通道号最小的。
// 一张图上每条边只画一条线,故必须有这么个"代表";它不改变任何数据,只是线上的颜色与数字。
function repChannel(edge) {
  const chs = edge.channels || [];
  return chs.length ? (chs.find(c => c.ch === 0) || chs[0]) : null;
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
