# 步骤02 逻辑设计：流控 + 多流传输 + 基础发送调度闭环

更新于 `2026-04-16`

## 1. 范围与完成标准

本文档定义步骤02的实现规格。目标不是描述某次工作树状态，而是给后续实现者一份以行为正确性为主的设计蓝图；同时它吸收了 `2026-04-16` transfer 审计暴露出的真实风险。

### 必做能力

- 在单个 QUIC 连接上完成多条双向流的最小 HTTP/0.9 并发下载。
- 建立可工作的 `stream manager`。
- 独立建模连接级与流级 flow control。
- 在 AppData 空间完成最小 ACK / loss / PTO / retransmission 配合。
- 提供基础 send scheduler，使 ACK / 控制帧 / 重传 / 新数据具备清晰优先级。
- 提供最小可用的 `/www`、`/downloads`、`REQUESTS`、`SSLKEYLOGFILE`、`QLOGDIR` runner 兼容。
- 提供足够的日志与 qlog 入口，使 transfer 失败可以定位到流控、乱序重组、尾部恢复或完成判定。

### 不做能力

- HTTP/3 / QPACK
- DATAGRAM
- 0-RTT / Resumption / Key Update
- Retry / Token / 地址验证新能力
- 连接迁移
- 复杂拥塞控制算法与成熟 pacing
- 自适应 flow control autotune 的最终形态

### 本地完成判定

步骤02只有在以下两项都通过时，才算完成：

- 步骤01的 `handshake` 路径仍可验证通过。
- 步骤02的 `transfer` 路径可在“单连接、多流并发、小窗口增长”场景下验证通过。

## 2. 总体架构

步骤02沿用步骤01的连接骨架，只在应用数据期补齐以下模块：

- `stream manager`
  - 负责流对象创建、查找、方向检查、Stream ID 分配、并发流集合维护。
- `per-stream state`
  - 负责发送偏移、接收重组、最终大小、应用消费、落盘进度、流级 flow control。
- `connection flow control`
  - 负责连接级累计接收字节与连接级 `MAX_DATA` / `DATA_BLOCKED`。
- `send scheduler`
  - 负责决定当前一轮 flush 应先发 ACK、控制帧、重传、还是新数据。
- `download path`
  - 负责客户端将连续响应体写入 `/downloads`，并把“写盘成功”反馈为真实消费进度。

设计原则：

- 步骤02不新建第二套连接状态机；仍复用步骤01的 `endpoint / connection / TLS / packet number spaces`。
- `stream` 是应用数据单元，`packet` 是发送调度单元，两者不能混用。
- 连接级与流级 flow control 必须分离建模。
- 接收缓存必须按 offset 组织，不能假设顺序到达。
- 窗口更新以“应用已消费”为主驱动，但实现必须留出在途 margin，不能被动等 `*_BLOCKED`。
- tail recovery 必须可观测，且重传必须以新 packet number 重新编码。

### 2.1 当前仓库建议对照锚点

为了让步骤02设计能直接落到当前代码结构，建议优先对照以下文件：

- `ai-quic/include/ai_quic/stream.h`
  - stream state、flow controller 字段与默认窗口常量
- `ai-quic/src/transport/stream.c`
  - 接收重组、`recv_contiguous_end`、`app_consumed`、`lost_ranges`
- `ai-quic/src/transport/conn_io.c`
  - `MAX_*` / `*_BLOCKED`、send scheduler、下载落盘、ACK/loss/PTO 协调
- `ai-quic/src/transport/loss.c`
  - RTT、loss detection、PTO、sent packet 跟踪
- `ai-quic/src/transport/transport_params.c`
  - `initial_max_data`、`initial_max_stream_data_*`、`initial_max_streams_bidi`
- `ai-quic/tests/unittest/test_unit.c`
  - flow control、重组、lost ranges、PTO 相关最小单测
- `ai-quic/tests/integration/test_handshake.c`
  - 当前多流下载的本地集成基础

## 3. 参考实现抽取的设计结论

步骤02建议吸收三份参考实现的共同结论，而不是抄其中某一份的全部复杂度。

### 3.1 xquic 给出的启发

- 发送最小原子是 `packet_out`，不是 `stream`。
- 连接 tick 时，发送顺序应明确区分：
  - ACK / 控制帧
  - retransmit / PTO probe
  - new data
- 发送成功后才进入 unacked / recovery 追踪。

这意味着新实现即使不完整复刻 `packet_out` 对象，也要保留“调度先决定发什么，再编码 packet”的分层。

### 3.2 lsquic 给出的启发

- 重传不应复用旧 packet buffer，而应根据未确认的 frame/range 重新编码。
- 读取/消费推进后，再由 flow control 模块决定何时扩大窗口并发送 `MAX_STREAM_DATA / MAX_DATA`。

这意味着：

- loss recovery 的核心对象应是“stream range / frame descriptor”，而不是“旧 UDP 包字节串”。
- `recv progress` 与 `app consumed progress` 必须是不同状态。

### 3.3 mvfst 给出的启发

- 连接状态变化与发包决策应通过 `pending events + scheduler` 解耦。
- 流控专项需要专门回归：
  - stream-level violation
  - connection-level violation
  - `MAX_*` 生成
  - BLOCKED frame
  - 读取触发 window update

这意味着步骤02不仅要把逻辑做对，还要把专项测试入口提前留好。

## 4. Stream Manager 与流模型

### 4.1 基本职责

`stream manager` 至少负责：

- 通过 Stream ID 查找流对象
- 为本端新请求分配本地双向流
- 在收到远端双向流数据时 lazy materialization
- 检查流方向是否合法
- 检查远端开启的流数量是否超限
- 维护“还有新数据可发 / 还有丢失数据待重传 / 已完成下载”的流集合语义

### 4.2 每条流至少维护的状态

建议每条流至少显式维护：

- 发送侧
  - `send_offset`
  - `send_fin_requested`
  - `send_fin_sent`
  - `send_fin_acked`
  - `send_data`
  - `acked_ranges`
  - `lost_ranges`
- 接收侧
  - `recv_data`
  - `recv_map` 或等价 offset 区间结构
  - `recv_contiguous_end`
  - `app_consumed`
  - `file_written_offset`
  - `final_size`
  - `final_size_known`
  - `recv_fin`
- 流级 flow control
  - `initial_window`
  - `recv_limit`
  - `recv_consumed`
  - `highest_received`
  - `send_limit`
  - `update_pending`
  - `blocked_pending`

### 4.3 流状态推进约束

- 本端发起请求流后，允许在握手完成后的 1-RTT 上发送最小请求。
- 远端数据到达时，必须支持乱序重组、重复数据、重复 FIN、最终大小锁定。
- 一旦 `final_size` 锁定，任何与之矛盾的数据都必须视为协议错误。
- `RESET_STREAM` 与 `STOP_SENDING` 即使不在步骤02主成功路径中，也必须预留清晰的状态边界，不能后续再推翻 flow control / final size 语义。

## 5. 流量控制设计

### 5.1 连接级与流级的职责边界

流级 flow control：

- 限制单个流的最大绝对接收偏移。
- 对端通过 `MAX_STREAM_DATA` 获得新的单流额度。
- 对端被单流额度卡住时，可发 `STREAM_DATA_BLOCKED`。

连接级 flow control：

- 限制所有流累计接收字节总量。
- 对端通过 `MAX_DATA` 获得新的连接级额度。
- 对端被连接级额度卡住时，可发 `DATA_BLOCKED`。

两者都必须满足：

- 新额度只能单调增加，收到变小或不变的 `MAX_*` 必须忽略。
- receiver 不得等待 `*_BLOCKED` 再更新额度。

### 5.2 receiver 侧窗口更新规则

步骤02采用“消费驱动为主”的窗口更新策略，但必须满足三个约束：

1. `MAX_STREAM_DATA` 可依据本流已消费偏移推进。
2. `MAX_DATA` 可依据所有流累计已消费字节推进。
3. 必须留出 margin / reserve，防止大文件、乱序或在途突发导致本地先触发 `recv_limit exceeded`。

因此，推荐策略不是“收到字节立即放大窗口”，而是：

- 以 `app_consumed` 为主驱动；
- 结合固定 reserve 或未来可扩展的 RTT/BDP 推测；
- 优先把 `MAX_*` 捎带在已有 ACK / 数据包上发送；
- 但绝不能等 `*_BLOCKED` 才发。

### 5.3 final size 与流控关系

- final size 是该流对连接级 credit 的最终扣除值。
- 收到带 FIN 的 STREAM 或 `RESET_STREAM` 后，必须锁定 final size。
- 锁定后：
  - 不再无限制扩大该流窗口；
  - 仍需继续收齐缺失区间；
  - 若后续包暗示不同 final size，必须触发 `FINAL_SIZE_ERROR` 或等价连接错误。

### 5.4 审计反推的设计防线

结合 `2026-04-16` 的失败现象，步骤02必须额外守住以下边界：

- 不得让“已收到但未形成连续区间”的数据无上限占满本地信用。
- 不得让“窗口更新过晚”成为大文件场景的常态。
- 连接级“累计接收字节数”和流级“最大接收偏移”必须语义清晰，避免同名异义。

## 6. 发送调度设计

### 6.1 基本优先级

步骤02的 send scheduler 最小优先级建议如下：

1. ACK
2. `MAX_DATA` / `MAX_STREAM_DATA` / `*_BLOCKED` 等控制帧
3. retransmission / PTO probe
4. new STREAM data

这条顺序的目标是：

- 先把恢复和流控闭环跑起来；
- 再在剩余预算中发新数据。

### 6.2 多流公平性

对 new STREAM data，步骤02至少应满足：

- 采用 round-robin 或等价简单公平策略。
- 单个流单次发送 chunk 大小受明确上限约束。
- 单个流不能长期独占发送预算。
- 当连接级额度耗尽时，能记录并发送 `DATA_BLOCKED`。
- 当流级额度耗尽时，能记录并发送 `STREAM_DATA_BLOCKED`。

### 6.3 重传策略

步骤02的重传遵循以下原则：

- 重传使用新的 packet number。
- 重传依据未确认的 stream range / frame descriptor 重新编码。
- 不复用旧 packet buffer 作为“原样重发包”。
- 恢复态若只剩 lost data，可进入明显的 retransmit-only 模式，但必须有前进证据和可观测性。

### 6.4 PTO 与 tail recovery

- PTO 是探测超时，不等于“包已判丢”。
- PTO 到期时应优先发送新数据；没有新数据时可发送重传或 PING probe。
- 尾部恢复必须保证：
  - scheduler 知道当前是否只剩 lost data；
  - 日志能显示是否还在前进；
  - 不允许“长时间只打 retransmit-only 日志，但无法判断最后缺口在哪里”。

## 7. 接收重组、消费与下载路径

### 7.1 接收重组

接收侧至少满足：

- 对 STREAM 数据按 offset 写入接收缓存。
- 对重复数据做一致性处理。
- 对乱序数据做区间缓存。
- 用 `recv_contiguous_end` 或等价状态表示“当前可连续交付到应用的上界”。

步骤02首版可以使用较简单的数据结构，但必须满足：

- 不能假设顺序到达；
- 不能因为一个小 gap 就破坏整个流的状态机；
- 能清楚地区分：
  - `highest_received`
  - `recv_contiguous_end`
  - `app_consumed`
  - `file_written_offset`

### 7.2 应用消费与文件落盘

客户端建议采用以下成功路径：

- 仅将 `recv_contiguous_end - file_written_offset` 这段连续数据写入目标文件。
- 只有当文件写入成功后，才把这些字节记为真实 `app_consumed`。
- `app_consumed` 推进后，才驱动 `MAX_STREAM_DATA / MAX_DATA` 更新。

这样做的好处是：

- `flow control credit` 与真实应用消费保持一致；
- 可以把写盘失败与协议状态推进清晰分开。

### 7.3 服务端响应路径

服务端在步骤02的成功路径至少满足：

- 解析请求流得到目标文件路径。
- 从 `/www` 读取目标文件。
- 将响应体绑定到对应请求流的发送侧缓冲。
- 在 send scheduler 驱动下按 chunk 发出，并支持重传。

## 8. ACK、Loss、PTO 与拥塞边界

### 8.1 本步最小要求

步骤02不要求完整成熟的 congestion control，但至少要求：

- AppData 空间有独立的 sent packet 跟踪。
- ACK 可驱动：
  - 新确认数据清理
  - RTT 更新
  - PTO 重置
  - loss detection
- loss 可驱动：
  - stream range 标记为 lost
  - 后续由 scheduler 重新编码重传

### 8.2 需要显式保证的行为

- ACK、loss、PTO 的状态必须按 packet number space 独立维护。
- 丢包检测不能把“同一流的应用偏移”与“packet number”混在一起。
- 重传后被确认，应能清除对应的 lost range。
- ACK 前进时，应能清楚地重置 PTO/backoff 状态。

### 8.3 与步骤06的边界

步骤02只要求“transfer 成功所需的最小恢复闭环”，不要求：

- 完整 persistent congestion 语义
- 高级 pacing
- 高丢包 / 长 RTT / blackhole 的鲁棒性

这些能力将在步骤06及后续阶段收敛。

## 9. Transport Parameters

步骤02至少要求正确处理以下 transport parameters：

- `initial_max_data`
- `initial_max_stream_data_bidi_local`
- `initial_max_stream_data_bidi_remote`
- `initial_max_streams_bidi`

这些参数必须满足：

- 本地接收窗口来自本端 transport parameters。
- 本端发送上限来自对端 transport parameters。
- 对远端新开流的数量检查使用本端 `initial_max_streams_bidi`。
- 一旦握手完成，connection 与 stream 的初始 flow control 状态应由这些参数统一初始化，而不是在多个位置临时拼装。

## 10. 可观测性与日志

### 10.1 必须提供的观测入口

步骤02必须继续支持：

- `SSLKEYLOGFILE`
- `QLOGDIR`

并保证失败时能按以下顺序定位：

- runner `output.txt`
- client/server 端点日志
- `sim` 抓包
- qlog / keylog

### 10.2 transfer 期最小日志集合

至少建议记录以下事件：

- 流创建与请求排队
- `recv progress`
- `app consumed progress`
- `client download progress`
- `schedule/send MAX_DATA`
- `schedule/send MAX_STREAM_DATA`
- `recv DATA_BLOCKED`
- `recv STREAM_DATA_BLOCKED`
- `conn flow blocked`
- `stream flow blocked`
- `ack progress reset PTO`
- `pto fire`
- `retransmit` 选择结果
- “当前只剩 lost data / 当前同时有 new+lost data”的调度状态

### 10.3 qlog 组织建议

步骤02的 qlog 最少应保留：

- `packet_sent`
- `packet_received`
- `connection_state_updated`
- `key_updated`

并建议逐步补齐 transfer 诊断更需要的字段：

- 当前 packet 内 frame 类型摘要
- last credit update
- blocked 状态
- retransmit pick
- download/stream progress

## 11. 测试与回归设计

### 11.1 本地测试最小集合

步骤02至少应具备：

- 单元测试
  - `MAX_DATA / MAX_STREAM_DATA / *_BLOCKED` 编解码
  - stream out-of-order reassembly
  - final size 与重复/重叠数据
  - consumed 触发窗口更新
  - lost ranges 被重传 ACK 清理
  - PTO backoff 被新 ACK 重置
- 集成测试
  - 单连接多流下载
  - `/www -> /downloads` 文件一致性
  - qlog 产出

### 11.2 interop 测试最小集合

步骤02至少应放通：

- `make interop-transfer-server`
- `make interop-transfer-client`
- `make test-interop-transfer`

并保证：

- 双向都可运行
- 单次失败能保留完整日志目录
- 重复运行时能快速分型“流控失效 / 恢复卡死 / 文件校验失败”

### 11.3 审计派生的补充回归

结合真实失败模式，步骤02完成前建议额外确认：

- 大文件尾部缺口不会长期卡在恢复态
- 接收侧不会因为 margin 过小而本地触发 `recv_limit exceeded`
- 多流并发时一个流的 gap 不会让连接级信用彻底停摆

## 12. 用户入口与 Makefile 约束

步骤02继续统一要求：

- 仓库根目录维护 `Makefile`
- 高频命令优先走 `make`
- transfer 主入口必须可直接从根目录运行

至少预留这些目标：

- `make build`
- `make test-unit`
- `make test-integration`
- `make test-interop-transfer`
- `make interop-transfer-server`
- `make interop-transfer-client`
- `make interop-logs CASE=transfer`

## 13. 非目标与删除项

本文档刻意不再承载以下内容：

- 某次具体 interop run 的逐目录复盘
- 基于当前工作树的临时结论式审计记录
- 只对某一个对端实现生效的经验性 patch 描述
- 对未来步骤06/08/12 的完整展开

若需要记录具体失败样本、真实通过率或一次性排障轨迹，应放在独立审计或实验记录中，而不是污染步骤02的行为规格。

## 14. 实现完成判定

### 14.1 步骤02完成条件

以下条件同时满足，才算步骤02完成：

- 单连接、多流 HTTP/0.9 下载路径可运行。
- 流级与连接级 flow control 可正确增长且无死锁。
- 多流并发时无明显饿死。
- final size、乱序接收、重复数据与重传闭环正确。
- transfer 成功路径可在 runner 契约下完成文件一致性验证。

### 14.2 与步骤01的联动条件

以下条件同时满足，才算步骤02真正完成：

- 步骤01的 `handshake` 仍保持通过。
- 步骤02的 `transfer` 可验证通过。

换言之，**step2 完成必须意味着 step1 仍然健康，且 step2 自身的多流传输与流控闭环已经成立。**
