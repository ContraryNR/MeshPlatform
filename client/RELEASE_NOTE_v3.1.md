# Release v3.1.0

音视频模块重构 — 简化代码设计、消除重复逻辑、优化分层架构

> 基于 [v3.0](https://github.com/ContraryNR/Mesh/releases/tag/V3.0) 的架构基础，本次发布聚焦于音频通话和视频通话两大模块的深度重构。核心目标：**降低函数间的代码重复率、简化整体设计、优化调用链的分层架构**。

---

## 技术栈

Qt 6.11.1 (MinGW 64-bit) · C++17 · libdatachannel (WebRTC DataChannel) · libjuice (ICE/STUN/TURN) · wintun (Windows 虚拟网卡) · OpenCV (视频帧处理) · Opus (语音编解码)

---

## 核心变更

### 音视频通话模块重构

#### 1. 提取公共播放层 — audioplayer mixin
- 将 `AudioChatWindow` 和 `VideoChatWindow` 中共用的音频播放逻辑抽取为独立的 `audioplayer` 非 QObject mixin 类
- 两个窗口通过多重继承共用同一套 `initAudio` / `playFlood` / `shutdownAudio` 接口，消除代码重复
- `QAudioSink` 直接内建于对话窗口中，移除独立的 `trd[AP]` 播放线程，降低线程开销

#### 2. 设备格式协商去重
- 将原本分散在 `mainwindow` 中的音频设备格式协商逻辑提取为 `audiocapture::negotiateAudio()` 静态方法
- 音频采集端初始化时直接调用，视频通话窗口初始化音频时也复用同一方法
- 一处协商逻辑，两处复用

#### 3. `mainwindow_frontend` 拆分为子系统模块
原 1466 行的 `mainwindow_frontend.cpp` 按职责拆分为 4 个独立编译单元：

| 文件 | 职责 |
|---|---|
| `mainwindow_audio.cpp` | 音频通话全生命周期管理（发起/接听/挂断/资源清理） |
| `mainwindow_video.cpp` | 视频通话全生命周期管理（发起/接听/挂断/资源清理） |
| `mainwindow_peer.cpp` | 对端管理（上线/下线/名称解析/列表更新） |
| `mainwindow_request.cpp` | 通用协商请求处理（`onTransferRequest` / `onReturnResult`） |

拆分后每个模块职责单一、行数可控，维护成本显著降低。

#### 4. 统一通话生命周期接口
- 提取 `sendHangupMsg()`、`updateDcWorkerCallingState()`、`shut*Session()` 系列函数作为音视频通话的共享接口
- 对齐音频/视频通话生命周期：`onEnd*Chat` → `shut*Session` → 资源清理 + 管线检查
- 新增 `getAudioCallingPeerWorkers()` 与已有的 `getVideoCallingPeerWorkers()` 对称设计
- 两端对话窗口统一添加顶部静音按钮

### 线程布局精简

| 版本 | 线程布局 |
|---|---|
| v3.0 | `NET=0` `DC=1` `TIN=2` `TOUT=3` `JS=4` `EN=5` `DE=6` `AU=7` |
| **v3.1** | **`NET=0` `DC=1` `TIN=2` `TOUT=3` `JW=4` `VE=5` `AC=6` `AE=7`** |

- `JS` → `JW`（JSON Worker，语义更精确）
- `EN` → `VE`（Video Encoder，避免与通用"编码"混淆）
- 移除已废弃的 `DE`（解码器线程）和 `AU`（音频综合线程）
- `AU` 拆分为 `AC`（Audio Capture 采集）和 `AE`（Audio Encoder 编码）
- 播放端集成到窗口内部后，移除 `trd[AP]` 引用

### 会话协商对话框层级化

引入通用对话框继承体系，替代零散的弹窗逻辑：

```
TopBaseDialog
  └─ SessionBaseDialog
        ├─ InitiativeSessionRequestDialog（发起方）
        └─ PassiveSessionRequestDialog（接收方）
  └─ FileRequestDialog
```

- 通话协商参数完全通过 `request/mission` 机制传递，不再依赖 `mainwindow` 成员变量缓存
- `requestTime`（8 字节）贯穿 wire 协议，接收方透传至业务侧

### 音频状态管理简化
- **移除** `audioCalleeHostNum` 单例路由 — 本地音频电平改为广播至所有活跃窗口
- **移除** `audioChatSessions`（`QSet<int>`），其功能完全由 `audioChatWindows`（`QHash<int, AudioChatWindow*>`）替代
- **移除** 已废弃的 `audioDeCoder` 字段，解码器由 `dcworker` 按需创建

### 健壮性与修复

#### HangUp/Frame 数据竟态修复（关键）
**问题**：发送 HangUp 消息前，已有连续的视频帧在传输管道中 in-flight。HangUp 到达对端更新 `isVideoCalling=false` 后，之前已进入 decoder 队列的帧仍在解码并向上回溯（dcworker → dcmanager → MainWindow），`MainWindow::onDecodedFrame` 无条件的初始化新的 `VideoChatWindow` 和 `VideoEncoder`，导致"挂断后幽灵回拨"。

**修复方案**：在 **dcworker 层**（上游）做双层拦截：
- 入站拦截：`onDcMsg(TYPE_VIDEO)` 入口检查 `isVideoCalling`，通话结束后丢弃剩余帧
- 出站拦截：`videodecoder::sendDecodedFrame` 信号的 `transferDecodedFrame` 发射前再次检查 `isVideoCalling`
- 音频同理：`TYPE_AUDIO` 入站 + `sendDecodedAudio` / `sendDecodedAudioLevel` 出站均做 `isAudioCalling` 守卫

**对比**：该方案在 dcworker 层（最上游）解决，而非在 MainWindow 层新增 `QSet<int>` 维护每路通话状态。避免了额外容器引入导致的逻辑膨胀和潜在的状态同步开销。

#### 其他修复
- `QHash::operator[]` re-insert 守卫：`dcFinish`、`Logger` 初始化路径中的隐式插入修复
- 跨线程 `delete` 修复：严格遵循 `quit()` → `wait()` → `delete` 顺序
- `dcmanager::setWorkerCallingState` 中的取反条件修复
- `closeEvent` 统一处理：对话窗口关闭时自动触发 `hangUpClicked` 信号
- `audioplayer` 线程安全：使用 `QMetaObject::invokeMethod` 保护状态更新
- `Logger` 初始化竞态修复

### 代码清理
- 移除 `thirdparty/libdatachannel` 源码（改为外部 CMake 子目录依赖）
- 清理大量冗余注释和调试日志
- 统一代码格式化
- `dbg_report.h` 移出版本追踪
- 所有变量命名规范化：`enCoder` → `videoEnCoder`、`audioCap` → `audioCapture`

---

## 文件变更清单（关键）

| 新增文件 | 说明 |
|---|---|
| `audioplayer.h` | 音频播放 mixin 基类（非 QObject） |
| `topbasedialog.h` | 顶层对话框基类 |
| `sessionbasedialog.h` | 会话协商对话框基类 |
| `initiativesessionrequestdialog.h` | 发起方协商对话框 |
| `passivesessionrequestdialog.h` | 接收方协商对话框 |
| `filerequestdialog.h` | 文件传输请求对话框 |
| `mainwindow_audio.cpp` | 音频通话管理模块 |
| `mainwindow_video.cpp` | 视频通话管理模块 |
| `mainwindow_peer.cpp` | 对端管理模块 |
| `mainwindow_request.cpp` | 协商请求处理模块 |

| 删除文件 | 说明 |
|---|---|
| `mainwindow_frontend.cpp` | 拆分为 4 个子模块 |
| `thirdparty/libdatachannel/` | 改为外部依赖 |

| 重命名 | 说明 |
|---|---|
| `20260530(.pro/.cpp/... )` → `Mesh` | 项目名称统一 |

---

## 升级说明

- **配置兼容**：配置文件格式无变化，v3.0 配置可直接沿用
- **协议兼容**：wire 协议（TYPE_NEGOTIATE / TYPE_TUN / TYPE_FILE / TYPE_AUDIO / TYPE_VIDEO / TYPE_JSON）完全向后兼容
- **构建变更**：`thirdparty/libdatachannel` 已从仓库移除，需确保本地 `E:/Code/QtCode/Dependency/libdatachannel` 路径存在
- **线程索引更新**：原 `JS` / `EN` / `DE` / `AU` 宏不再可用，同步代码请改用 `JW` / `VE` / `AC` / `AE`
