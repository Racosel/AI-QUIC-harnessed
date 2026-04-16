# 2026-04-16 Transfer/Transport 审计报告

## 执行摘要

本报告只基于 `2026-04-16 11:55:53` 之后的真实 interop 样本、当前仓库代码和仓库内 docs；不修改代码，不引入新实验。

- 已证实：`transfer` 双向共 20 次，`ai-quic -> xquic` 通过 `4/10`，`xquic -> ai-quic` 通过 `9/10`；`handshake` 双向 `2/2` 全通过。问题主要发生在应用数据阶段，而不是握手主链。
- 已证实：`ai-quic -> xquic` 的 6 次失败全部集中在第三个 `5 MiB` 文件；xquic 客户端在 30 秒时结束任务，runner 报文件大小不匹配。代表样本见 `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner-output.txt:69-100`。
- 已证实：同一失败样本的 ai-quic server 日志显示，握手确认后已经把 `10 MiB` 应用数据全部送入发送路径，随后进入 `retransmit_only=1` 循环，`conn_send=10485760/25690176`，说明它不是卡在“还没开始发”或“被对端 MAX_DATA 卡死”，而是卡在尾部恢复/完成判定闭环。见 `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner/ai-quic_xquic/transfer/server/server.log:27202-27218`。
- 已证实：`xquic -> ai-quic` 的唯一失败是 ai-quic 接收侧本地报错 `recv_limit exceeded`，触发点出现在第三个大文件传输中，错误只超出当前 `recv_limit` `892` 字节。见 `artifacts/interop/2026-04-16T12-03-29-xquic-ai-quic-transfer/runner/xquic_ai-quic/transfer/client/client.log:31813-31818`。
- 推断：`xquic -> ai-quic` 的失败更像“消费驱动的 MAX_DATA / MAX_STREAM_DATA 更新策略过紧”，在大文件、乱序或在途突发下信用更新略慢于到达数据；当前实现已经加入了 interop 定向补偿，但补偿仍不充分。证据见 `ai-quic/include/ai_quic/stream.h:15-29`、`ai-quic/src/transport/stream.c:683-701`、`ai-quic/src/transport/conn_io.c:575-605`。
- 已证实：当前重传模型是“按 `lost_ranges` / stream offset 重新编码 STREAM frame”，不是复用旧 packet buffer；这点与 lsquic 文档建议方向一致，因此“重传直接复用旧包缓冲”不是本次主因。见 `ai-quic/src/transport/loss.c:311-337`、`ai-quic/src/transport/stream.c:286-375`、`ai-quic/src/transport/conn_io.c:1029-1099`。
- 已证实：仓库里已有最小 RTT/loss/PTO 实现，也有 qlog，但 `congestion_control` 模块仍是占位符，qlog 只记录包头/长度/帧数等粗粒度信息，缺少 goodput、窗口演进、队列占用、重传挑选结果等传输层关键观测。见 `ai-quic/src/transport/loss.c:121-150`、`ai-quic/src/congestion_control/README.md:1`、`ai-quic/src/common/qlog.c:116-150`、`ai-quic/src/transport/conn_io.c:238-270`。

## 样本边界与基线

### 有效样本边界

- 有效样本窗口：`2026-04-16 11:55:53` 到 `12:11:35`。
- 无效样本：`2026-04-16 11:33:06/07/08` 这 3 个目录只是 Docker 权限失败的无效尝试，不应计入统计；代表证据见 `artifacts/interop/2026-04-16T11-33-06-ai-quic-xquic-transfer/runner-output.txt:1-11`。

### 结果总表

| 用例 | 方向 | 通过/总数 | 通过率 | 时长范围 | 平均时长 |
| --- | --- | --- | --- | --- | --- |
| transfer | ai-quic server -> xquic client | 4/10 | 40% | 42s-48s | 45.6s |
| transfer | xquic server -> ai-quic client | 9/10 | 90% | 39s-47s | 45.2s |
| handshake | ai-quic server -> xquic client | 1/1 | 100% | 17s | 17s |
| handshake | xquic server -> ai-quic client | 1/1 | 100% | 16s | 16s |

原始汇总：`/tmp/interop-suite-20260416T115553/summary.tsv`。

### 失败类型分布

| 方向 | 失败次数 | 共同现象 | 代表证据 |
| --- | --- | --- | --- |
| ai-quic -> xquic | 6 | 第三个 `5 MiB` 文件未收齐，客户端 30 秒结束，runner 报 size mismatch | `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner-output.txt:69-100` |
| xquic -> ai-quic | 1 | ai-quic client 本地 `stream recv_limit exceeded` | `artifacts/interop/2026-04-16T12-03-29-xquic-ai-quic-transfer/runner/xquic_ai-quic/transfer/client/client.log:31813-31818` |

`ai-quic -> xquic` 六次失败的第三个文件实际落盘大小分别是：`97200`、`5194800`、`247200`、`229200`、`218400`、`226800` 字节。证据分别在：

- `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner-output.txt:99`
- `artifacts/interop/2026-04-16T11-57-23-ai-quic-xquic-transfer/runner-output.txt:100`
- `artifacts/interop/2026-04-16T11-59-36-ai-quic-xquic-transfer/runner-output.txt:100`
- `artifacts/interop/2026-04-16T12-00-24-ai-quic-xquic-transfer/runner-output.txt:100`
- `artifacts/interop/2026-04-16T12-01-54-ai-quic-xquic-transfer/runner-output.txt:100`
- `artifacts/interop/2026-04-16T12-02-41-ai-quic-xquic-transfer/runner-output.txt:100`

## 现象分型

### 1. `ai-quic -> xquic`：尾部恢复/完成判定问题

失败样本 `11:55:53`：

- 前两个文件完成，第三个 `5 MiB` 文件只收到 `97200` 字节。
- xquic client 在 30 秒时结束任务，`req_fin_cnt` 已经是 `3`，但最终文件校验失败。
- 见 `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner-output.txt:60-100`。

成功样本 `11:56:40`：

- 第三个 `5 MiB` 文件完整收齐，`recv_body_size:5242880`。
- 见 `artifacts/interop/2026-04-16T11-56-40-ai-quic-xquic-transfer/runner-output.txt:60-74`。

这说明同方向并非“完全不通”，而是大文件尾部的恢复/收口不稳定。

### 2. `xquic -> ai-quic`：本地接收信用耗尽

失败样本 `12:03:29`：

- 在第三个大文件的接收过程中，ai-quic client 先记录：
  `stream=8 recv progress ... frame_end=3191583 contiguous=3109053 consumed=3109053 recv_limit=3191870`
- 紧接着下一帧触发：
  `stream=8 recv_limit exceeded frame_end=3192762 recv_limit=3191870`
- 见 `artifacts/interop/2026-04-16T12-03-29-xquic-ai-quic-transfer/runner/xquic_ai-quic/transfer/client/client.log:31813-31818`。

成功样本 `12:04:08`：

- 日志能看到健康的 `DATA_BLOCKED -> schedule MAX_DATA/MAX_STREAM_DATA -> send MAX_DATA/MAX_STREAM_DATA` 闭环。
- 见 `artifacts/interop/2026-04-16T12-04-08-xquic-ai-quic-transfer/runner/xquic_ai-quic/transfer/client/client.log:100-115` 与 `:543-547`。

这说明 ai-quic client 的流控闭环“多数时候有效”，但在少数大文件样本里仍会失手。

## 传输链路审计

### 关键代码路径

| 环节 | 代码锚点 | 审计结论 |
| --- | --- | --- |
| transport params 默认值 | `ai-quic/src/transport/transport_params.c:118-125` | 默认 `initial_max_data = 128 KiB`，`initial_max_stream_data_bidi_* = 128 KiB`，`initial_max_streams_bidi = 32`。 |
| 连接流控初始化 | `ai-quic/src/transport/conn.c:23`、`ai-quic/src/transport/conn_io.c:1630-1633` | 连接级窗口先按本地 TP 初始化，再用对端 TP 设置发送上限。 |
| 流控结构 | `ai-quic/include/ai_quic/stream.h:42-87` | `recv_limit / recv_consumed / highest_received / send_limit / update_pending / blocked_pending` 都在同一结构里维护。 |
| 接收缓存与乱序重组 | `ai-quic/src/transport/stream.c:609-626` | 使用 `recv_data + recv_map`，按字节标记并线性推进 `recv_contiguous_end`。 |
| 应用消费与增窗 | `ai-quic/src/transport/stream.c:683-701`、`ai-quic/src/transport/conn_io.c:575-605` | `MAX_STREAM_DATA` 与 `MAX_DATA` 都严格依赖“应用已消费”的推进。 |
| 文件落盘与消费触发 | `ai-quic/src/transport/conn_io.c:694-720` | client 只有把连续数据写入文件后，才会调用 `ai_quic_conn_on_app_consumed()`。 |
| `MAX_*` / `*_BLOCKED` 发送 | `ai-quic/src/transport/conn_io.c:819-894` | ACK、`MAX_DATA`、`MAX_STREAM_DATA`、`DATA_BLOCKED`、`STREAM_DATA_BLOCKED` 都会在 app-data flush 时拼入包。 |
| 新数据发送与 blocked 检查 | `ai-quic/src/transport/conn_io.c:1139-1203` | 新数据发送前检查连接级与流级 credit，不足时置 `blocked_pending` 并打日志。 |
| app-data flush 调度 | `ai-quic/src/transport/conn_io.c:1248-1318`、`:1332-1372` | 已实现 ACK/control、PTO probe、retransmit、new data 的调度层次。 |
| 丢包检测与重传标记 | `ai-quic/src/transport/loss.c:289-337` | 丢包后按 packet 中记录的 stream frame 范围回写 `lost_ranges`。 |
| 重传取数与重编码 | `ai-quic/src/transport/stream.c:336-375`、`ai-quic/src/transport/conn_io.c:1029-1099`、`:922-935` | 从 `lost_ranges` 取 offset/chunk，再从 `send_data + offset` 复制到新 STREAM frame。 |
| RTT/PTO | `ai-quic/src/transport/loss.c:121-150`、`:480-603` | 已有最小 RFC 9002 风格 RTT/PTO 计算，但无完整拥塞控制模块。 |
| qlog | `ai-quic/src/common/qlog.c:116-150`、`ai-quic/src/transport/conn_io.c:238-270` | 有 qlog，但字段很粗，缺少 transport 诊断所需的高价值状态。 |

### 发送链路审计

- ai-quic 的 app-data flush 已经采用“先 control，再 probe/retransmit，再 new data”的顺序，和 xquic / mvfst 文档强调的 scheduler 顺序大体一致。见 `ai-quic/src/transport/conn_io.c:1332-1372`，对照 `docs/refs/xquic.md:1072-1088`、`docs/refs/mvfst.md:45-47` 与 `:912-916`。
- 新数据发送前，代码同时检查 `conn_credit` 和 `stream_credit`，并在不足时设置 `DATA_BLOCKED` / `STREAM_DATA_BLOCKED`。见 `ai-quic/src/transport/conn_io.c:1145-1176`。
- 代表失败样本里，ai-quic server 已经达到 `conn_send=10485760/25690176`，说明它不是被对端信用挡在“发送前”，而是发送后进入了恢复阶段。见 `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner/ai-quic_xquic/transfer/server/server.log:27206-27218`。

### 重传与恢复审计

- `loss.c` 在检测到 packet threshold 或 time threshold 触发时，把包内各个 STREAM frame 的 `[offset, end)` 范围回写到 `stream->lost_ranges`。见 `ai-quic/src/transport/loss.c:299-337`。
- 随后 `conn_io.c` 会从 `lost_ranges` 中取一个范围，重新构造新的 STREAM frame，并从 `stream->send_data + offset` 复制 payload。见 `ai-quic/src/transport/conn_io.c:1029-1099` 与 `:922-935`。
- 因此，当前实现已经是“按 stream range 重编码”的模型，和 `docs/refs/lsquic.md:577-581` 的建议一致。
- 推断：`ai-quic -> xquic` 的主问题不太像“错误地复用旧 packet buffer”；更像“尾部恢复闭环不够稳”，包括但不限于 lost-range 选择、完成判定、以及缺少足够强的恢复态观测。

### 接收缓存、消费与流控审计

- 流级接收时，若 `frame_end > stream->flow.recv_limit` 就直接报错。见 `ai-quic/src/transport/stream.c:591-602`。
- 连接级接收时，代码按“新增唯一字节数”累计到 `conn->conn_flow.highest_received`，并用它检查连接级 `recv_limit`。见 `ai-quic/src/transport/conn_io.c:1794-1821`。
- 这里有一个语义差异：`stream->flow.highest_received` 是“单流最大 offset”，而 `conn->conn_flow.highest_received` 实际上是“所有流累计唯一字节数”。后者符合 RFC 9000 对 `MAX_DATA` 的语义，但字段名与流级含义不一致，容易误导日志分析。对照 `docs/ietf/txt/rfc9000.txt:1064-1069`。
- client 只有把 `recv_contiguous_end - file_written_offset` 这段连续数据成功写入文件后，才推进 `recv_consumed` 并调度 `MAX_*`。见 `ai-quic/src/transport/conn_io.c:694-720`。
- 这意味着一旦出现 gap，`contiguous` 与 `app_consumed` 都会停住，`MAX_DATA / MAX_STREAM_DATA` 也会跟着停住；RFC 9000 明确指出，这会影响吞吐，并建议接收方提前或同一 RTT 内多次更新信用。见 `docs/ietf/txt/rfc9000.txt:1100-1147`。

### 测量与观测审计

- 当前已有 ACK、RTT、PTO、`bytes_in_flight`、`DATA_BLOCKED`、`MAX_*` 相关日志。见 `ai-quic/src/transport/conn_io.c:142-180`、`:1251-1265`。
- 当前也有 qlog writer，但 `packet_sent/packet_received` 事件只记录包头、包长、预览和 `frame_count`，没有每个包内 frame 明细、没有 credit/win/loss state、没有 retransmit pick 结果。见 `ai-quic/src/common/qlog.c:116-150` 与 `ai-quic/src/transport/conn_io.c:238-270`。
- `ai-quic/src/congestion_control/README.md:1` 仍是 `Stage 00-01 placeholder.`，说明完整的 congestion control / pacing 能力尚未落地。

## RFC 9000/9002 对照

### Flow Control

RFC 9000 的关键要求：

- 接收方同时维护 stream-level 和 connection-level flow control。见 `docs/ietf/txt/rfc9000.txt:1037-1055`。
- `MAX_STREAM_DATA` 是单流最大绝对 offset，`MAX_DATA` 是所有流累计字节上限。见 `docs/ietf/txt/rfc9000.txt:1057-1070`。
- 发送方不得超限；接收方若发现超限，必须按 `FLOW_CONTROL_ERROR` 处理。见 `docs/ietf/txt/rfc9000.txt:1076-1081`。
- 为避免发送方阻塞，接收方可以在一个 RTT 内多次提前发 `MAX_*`；而且接收方不得等 `*_BLOCKED` 再发 `MAX_*`。见 `docs/ietf/txt/rfc9000.txt:1100-1126`。
- 如果接收方不能保证对端始终持有高于 BDP 的信用，吞吐会被 flow control 限制；丢包造成的 gap 会阻碍消费，从而阻碍信用释放。见 `docs/ietf/txt/rfc9000.txt:1133-1147`。

本实现的符合点：

- 连接级与流级都维护了 `recv_limit / send_limit / blocked_pending / update_pending`，基础框架是对的。见 `ai-quic/include/ai_quic/stream.h:42-87`。
- `DATA_BLOCKED` / `STREAM_DATA_BLOCKED` 的收发路径存在。见 `ai-quic/src/transport/conn_io.c:819-894` 与 `:1967-1980`。

本实现的风险点：

- `MAX_*` 更新完全绑定到“应用消费”，不是绑定到“接收进度 + RTT + 在途数据预算”。这在 RFC 允许范围内，但更保守，且在大文件/gap 下更容易出现短暂信用耗尽。见 `ai-quic/src/transport/stream.c:683-701`、`ai-quic/src/transport/conn_io.c:575-605`。
- `ai-quic/include/ai_quic/stream.h:15-29` 的注释已经直接承认：为了 interop transfer，代码曾把流窗口对齐到连接窗口，并额外加了 `4 * chunk` reserve。这是一个很强的信号，说明当前实现已经知道“消费驱动增窗 + gap”会在真实互操作里出问题；本次 `9/10` 中那 `1` 次失败说明该补丁仍不完全。

### Loss / PTO / RTT

RFC 9002 的关键要求：

- PTO 是 tail loss / ack loss 恢复机制，PTO 触发本身不等于判定丢包。见 `docs/ietf/txt/rfc9002.txt:269-285` 与 `:604-613`。
- RTT 样本应从 newly acked 的最大 ack-eliciting 包生成，并维护 `min_rtt / smoothed_rtt / rttvar`。见 `docs/ietf/txt/rfc9002.txt:319-360`。

本实现的符合点：

- `loss.c` 已实现 `smoothed_rtt / rttvar / min_rtt` 与指数回退 PTO 计算。见 `ai-quic/src/transport/loss.c:121-187`。
- `conn_io.c` 在 ACK 收到后会写 RTT/PTO 相关日志，并在 ack progress 时重置 PTO 计数。见 `ai-quic/src/transport/conn_io.c:142-180`。

本实现的缺口：

- 当前没有完整的拥塞控制模块与 pacing；PTO/log 具备，但 throughput 调优能力不完整。见 `ai-quic/src/congestion_control/README.md:1`。
- 在代表性 `ai-quic -> xquic` 失败样本中，没有看到明显 `pto fire` 日志，而是看到了长期 `retransmit_only`。这说明问题更像恢复态闭环或完成收口不足，而不是 PTO 完全缺失。

## 与 xquic / lsquic / mvfst 的对照

### 与 xquic 的对照

- `docs/refs/xquic.md:1072-1088` 强调：发送最小原子是 `packet_out`，连接 tick 时先 ACK，再重传/PTO，再新数据。
- ai-quic 在 `ai_quic_conn_flush_app_data()` 里已经实现了非常相似的顺序。见 `ai-quic/src/transport/conn_io.c:1332-1372`。
- 差异主要不在调度顺序，而在“观测颗粒度”：xquic 风格文档强调 send queue / unacked list / recovery 的 packet 级对象；ai-quic 的 qlog 和日志还不足以把“某个尾部范围为何迟迟未收敛”讲清楚。

### 与 lsquic 的对照

- `docs/refs/lsquic.md:577-581` 认为重传不应复用旧 packet buffer，而应按未确认 frame descriptor 重新编码。
- ai-quic 目前也不是复用旧包，而是 `lost_ranges -> pop lost range -> 重新构造 STREAM frame -> memcpy(send_data + offset)`。见 `ai-quic/src/transport/stream.c:336-375` 与 `ai-quic/src/transport/conn_io.c:1029-1099`。
- `docs/refs/lsquic.md:852-855` 强调“应用读取后推进 read_offset，再由 flow control 模块决定是否发 `MAX_STREAM_DATA / MAX_DATA`”。ai-quic 也采用“读取/写盘后推进 consume，再发 MAX”的基本模式。见 `ai-quic/src/transport/conn_io.c:694-720`。
- 差异在于 lsquic 文档更像成熟设计建议，而 ai-quic 现在的阈值仍是固定的 `half-window + one chunk + reserve`，还没做到更成熟的 autotune。

### 与 mvfst 的对照

- `docs/refs/mvfst.md:45-47` 与 `:912-916` 强调“pending events + scheduler”模型，由 scheduler 决定 ACK/STREAM/CRYPTO/MAX_* 等帧的编排。
- ai-quic 已经具备部分相同思想：很多事件只是先置位 `ack_needed / update_pending / blocked_pending`，随后在 flush 阶段出包。见 `ai-quic/src/transport/conn_io.c:819-894`。
- `docs/refs/mvfst.md:1128-1132` 还强调要专门覆盖 stream/connection flow control violation、`MAX_*` 生成、BLOCKED、读取触发 window update 等测试。
- ai-quic 已有基础单元测试和本地集成测试，但 interop 规模、长文件、重复跑、复杂网络条件下的 transport 专项回归仍偏弱。见 `ai-quic/tests/unittest/test_unit.c:662-669` 与 `ai-quic/tests/integration/test_handshake.c:86-149`。

## 关键问题分级

### P0：当前 transfer 失败的根因候选

1. `ai-quic -> xquic` 的尾部恢复/完成闭环不稳定

- 已证实：失败样本里新数据早已发完，随后长时间停在 `retransmit_only=1`；不是握手问题，也不是明显的发送前 flow-control 阻塞。见 `artifacts/interop/2026-04-16T11-55-53-ai-quic-xquic-transfer/runner/ai-quic_xquic/transfer/server/server.log:27202-27218`。
- 推断：需要优先审计 lost-range 生命周期、恢复态选择策略、FIN/完成判定和与 xquic 客户端 30 秒 task timeout 的交互。

2. `xquic -> ai-quic` 的接收信用更新在大文件场景下不够稳

- 已证实：唯一失败样本是本地 `recv_limit exceeded`，而成功样本又显示 credit 闭环多数时候有效。
- 已证实：当前信用更新严格依赖消费推进；client 只有写入连续数据后才会发 `MAX_*`。见 `ai-quic/src/transport/conn_io.c:694-720`。
- 推断：固定阈值与 `4 * chunk` reserve 仍不足以覆盖这类 interop burst / gap 场景。

### P1：协议正确性 / 稳定性缺口

1. 连接级 `highest_received` 命名与语义不一致

- 已证实：stream 侧表示“最大 offset”，conn 侧表示“累计唯一字节数”。见 `ai-quic/src/transport/stream.c:605-606` 与 `ai-quic/src/transport/conn_io.c:1794-1821`。
- 这不一定是当前失败主因，但会提高误判概率，并增加排障成本。

2. 接收缓存与乱序重组模型较粗

- 已证实：`recv_map` 按字节标记，`recv_contiguous_end` 线性扫描推进。见 `ai-quic/src/transport/stream.c:609-626`。
- 推断：对 `5 MiB` 大文件和更复杂乱序模式来说，这种实现的效率与观测粒度都偏弱，容易掩盖真正的 gap 行为。

3. 恢复态观测不足

- 已证实：qlog 与普通日志里缺少“每次重传挑了哪个 range、剩余 lost_ranges 数量、最后一个未完成洞的位置、最近一次有效 MAX_* 发送/接收”的持续轨迹。
- 这不一定制造 bug，但会显著放大定位成本。

### P2：观测 / 测试 / 长期能力欠账

1. 拥塞控制 / pacing 尚未完整实现

- 已证实：`ai-quic/src/congestion_control/README.md:1` 仍是占位符。

2. qlog 粒度不足

- 已证实：当前 qlog 只适合“看有没有包”，不适合“看 transport 为何失稳”。

3. transport 场景回归矩阵不足

- 已证实：仓库内 interop 用例文档只列出 `handshake / transfer / retry / resumption / zerortt / http3 / multiconnect / v2 / rebind-* / connectionmigration` 等基础场景，未见 long-rtt、goodput、crosstraffic、ECN 这类专门性能/鲁棒性场景。见 `docs/quic-interop-runner/quic-test-cases.md:9-59`。
- 已证实：现有测试更偏编码 round-trip、本地 fake-link 集成、多流小到中等文件传输。见 `ai-quic/tests/unittest/test_unit.c:280-318`、`:561-584`、`:662-669` 与 `ai-quic/tests/integration/test_handshake.c:86-149`。

## 未完成 / 未实现功能清单

以下项不一定是本次失败的单一主因，但会影响 transport 完整度：

- 完整 congestion control / pacing 模块未落地。见 `ai-quic/src/congestion_control/README.md:1`。
- 流控策略仍是固定阈值 + 小 reserve，没有更成熟的 RTT/BDP/autotune 策略。见 `ai-quic/src/transport/stream.c:683-701`、`ai-quic/src/transport/conn_io.c:575-605`。
- qlog 缺少 transport 深度诊断字段。见 `ai-quic/src/common/qlog.c:116-150`、`ai-quic/src/transport/conn_io.c:238-270`。
- interop 规模化 transport 回归矩阵不够丰富，缺少更强的长期稳定性验证。

## 优先级整改地图

### P0：先回答“为什么现在会失败”

- 先围绕 `ai-quic -> xquic` 失败样本做尾部恢复专项时间线，重点对齐：最后一个完整 ACK、首次进入 `retransmit_only`、lost-range 何时清空/未清空、最后一个未完成 offset 区间。
- 再围绕 `xquic -> ai-quic` 失败样本做接收信用专项时间线，重点对齐：`contiguous`、`app_consumed`、`recv_limit`、最近一次 `MAX_STREAM_DATA/MAX_DATA` 发送、失败前最后一个 gap 的大小与持续时间。

### P1：再补 transport 正确性与稳定性

- 明确区分连接级“累计接收字节数”和流级“最大 offset”的命名/日志语义。
- 为恢复态和流控态补充稳定可读的观测：lost-ranges、last MAX* sent/recv、blocked age、last gap、final-size 进度。
- 评估固定阈值信用策略是否需要更早更新或自适应调整。

### P2：最后补长期能力

- 扩展 interop / integration 回归矩阵，至少加入更大文件、更多重复次数、长 RTT、损失、跨流竞争等场景。
- 提升 qlog 与性能指标，使 transport 问题能直接从轨迹中定位，而不是主要靠肉眼翻普通日志。

## 最终结论

- 握手链路已经基本健康；当前主要矛盾集中在应用数据期。
- `ai-quic -> xquic` 更像尾部恢复/完成闭环问题。
- `xquic -> ai-quic` 更像接收侧信用更新策略过紧。
- 当前实现在“基础 transport 逻辑”上已经具备可工作的骨架，也已经做过 interop 定向补偿；但从 RFC 9000/9002 与 xquic / lsquic / mvfst 的对照看，仍存在明显的稳定性、观测性和完整度欠账。
