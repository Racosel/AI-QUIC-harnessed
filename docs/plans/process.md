# AI-QUIC 阶段过程跟踪

更新于 `2026-04-09 10:17:21 +0800`

## 当前基线

- 本文件根据 `docs/plans/plan-quic.md` 与 `docs/plans/plan-general.md` 初始化。
- 当前执行职责已明确：在 `docs/` 约束下，参照 `xquic/` 的代码与结构，在 `ai-quic/` 下完成新的设计与实现；`xquic/` 默认仅作参考，不作为新设计落点。
- `ai-quic/` 目录骨架已创建，并补齐最小构建入口与模块占位文件；当前尚未落地握手协议逻辑。
- 当前仓库中，`xquic/interop/run_endpoint.sh` 已暴露一批 interop testcase 入口，但仓库内暂无可直接复用的近期成功验证记录，因此本文件默认采用保守状态，不把“代码存在”写成“已完成”。
- `docs/plans/plan-ai.md` 目前不在当前工作树中，故本文件初始化不将其作为现行依据。

## 状态说明

- `禁用占位`：计划中保留，但当前不作为主线发布阻塞项。
- `待核验`：已看到计划或代码/脚本入口，但缺少近期验证证据。
- `待对齐`：计划存在，但当前 interop 入口、实现映射或验证路径仍不完整。
- `进行中`：当前轮次已明确选为主攻项，并正在补实现或补验证。
- `已本地验证`：已完成本地或窄范围验证，但未完成高保真验证。
- `已高保真验证`：已完成 interop 或等价高保真验证。
- `已完成`：达到该阶段要求的目标验证层级，且无当前已知阻塞。

## 总体判断

- 当前优先主线仍应从 `步骤01` 开始，先建立最小握手闭环与可重复验证基线，再逐步推进到 `步骤05` 之后的恢复、迁移与 H3 阶段。
- `步骤03`、`步骤10`、`步骤11`、`步骤13` 在现有 `xquic/interop/run_endpoint.sh` 中未看到同名 testcase 入口，需要先补齐 interop 映射或确认替代执行路径。
- `步骤12` 的 HTTP/3 / QPACK 代码目录与 interop 入口都存在，但同样缺少本仓库内的近期成功记录，当前只能记为 `待核验`。

## 阶段跟踪

### 步骤00：Version Negotiation（禁用占位）

- 当前状态：`禁用占位`
- 目标测试例：`versionnegotiation`
- 实现焦点：版本协商报文编码与未知版本处理路径。
- 完成判定：明确记录“当前禁用（#20）”状态，保留代码路径，但不作为当前发布阻塞项。
- 已具备证据：
  - `docs/plans/plan-quic.md` 已将该步骤定义为禁用占位。
  - `docs/quic-interop-runner/quic-test-cases.md` 明确写明当前因 `#20` 处于禁用状态。
  - `xquic/interop/run_endpoint.sh` 已包含 `versionnegotiation` testcase 入口。
- 仍缺：
  - 当前仓库内暂无对应验证记录。
- 下一步：
  - 保持占位，不抢占主线实现优先级。
  - 若后续恢复该 testcase，再补独立验证与状态切换。

### 步骤01：握手最小闭环

- 当前状态：`进行中`
- 目标测试例：`handshake`
- 实现焦点：Initial/Handshake 基本收发、握手状态推进与连接建立。
- 完成判定：单连接完成握手并成功下载小文件。
- 已具备证据：
  - `docs/plans/plan-quic.md` 已将本步骤定义为当前主线起点。
  - `docs/plans/step01-handshake-doc-index.md` 已整理步骤01的阅读顺序、命令模板、结果判定与排障顺序。
  - `docs/plans/repo-file-hierarchy.md` 已区分当前仓库现状与建议中的 `ai-quic/` 目录，并给出参考 `xquic` 的模块划分。
  - `ai-quic/` 已按规划创建 `include/ai_quic`、`src/{common,transport,tls,congestion_control,http3}`、`demo/`、`interop/`、`tests/unittest/` 骨架。
  - `ai-quic/CMakeLists.txt` 与 `ai-quic/src/CMakeLists.txt` 已建立最小构建入口，并可构建 `ai_quic_core` 静态库骨架。
  - `docs/quic-interop-runner/quic-test-cases.md` 明确要求：单连接、握手成功、小文件下载、服务端本步不发送 Retry。
  - `docs/quic-interop-runner/how-to-run.md` 明确了 `/www`、`/downloads`、`REQUESTS`、`TESTCASE`、`/certs`、日志目录与 qlog 约束。
  - `docs/quic-interop-runner/implement-requirements.md` 明确了退出码 `0/1/127` 契约。
  - `docs/ietf/notes/9000.md`、`9001.md`、`9002.md` 已提供步骤1直接需要的握手、TLS 集成、Initial 大小、ACK、PTO 与反放大约束。
  - `xquic/interop/run_endpoint.sh` 已包含 `handshake` testcase 入口。
- 仍缺：
  - `ai-quic/` 目前仍是框架层，尚未实现 Initial/Handshake/CRYPTO 的真实协议路径。
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 按下方“步骤01 专项规划”先在 `ai-quic/src/transport` 与 `ai-quic/src/tls` 落地最小握手路径。
  - 建立最小握手闭环的本地基线。
  - 在 `quic-interop-runner` 中执行 `handshake` 并保存日志入口。

#### 步骤01 专项规划（基于 `docs/` 阅读）

##### 1. 文档依据

- 阶段与验收：`docs/plans/plan-quic.md`
- 仓库层级与建议落点：`docs/plans/repo-file-hierarchy.md`
- 测试语义：`docs/quic-interop-runner/quic-test-cases.md`
- 运行契约与日志：`docs/quic-interop-runner/how-to-run.md`
- 退出码约束：`docs/quic-interop-runner/implement-requirements.md`
- 传输层关键约束：`docs/ietf/notes/9000.md`
- TLS 集成关键约束：`docs/ietf/notes/9001.md`
- 握手阶段恢复/PTO：`docs/ietf/notes/9002.md`

##### 2. 本步目标

- 以 `TESTCASE="handshake"` 为唯一主线，建立“单连接 + 无 Retry + 小文件下载”的最小闭环。
- 以 HTTP/0.9 下载成功作为应用层最小验收，不在本步提前引入多流、H3、0-RTT、会话恢复、迁移等复杂能力。
- 先拿到“真实 interop 可运行”的最小链路，再在后续步骤叠加能力，而不是在步骤01一次性做完后续功能。

##### 3. 本步必须满足的协议与运行约束

- 客户端承载 Initial 的 UDP 数据报必须扩展到至少 1200 字节；服务器对过小 Initial 必须丢弃。
- Initial、Handshake、ApplicationData 必须按独立包号空间处理 ACK 与丢包恢复，不能混用确认。
- Initial / Handshake 包必须立即确认；握手确认前，Initial/Handshake 的 PTO 计算中 `max_ack_delay` 视为 0。
- TLS 握手消息必须走 QUIC `CRYPTO` 帧；`CRYPTO` 数据必须可靠重传，且重传时仍使用其原始加密级别。
- 客户端首次发送 Handshake 包后必须丢弃 Initial 密钥；服务器首次成功处理 Handshake 包后必须丢弃 Initial 密钥。
- 服务端在握手完成时必须立即发送 `HANDSHAKE_DONE`。
- 服务端在地址验证完成前必须遵守 3 倍反放大限制；客户端需要继续发送包来解除服务器阻塞。
- 本 testcase 明确要求服务端本步不发送 Retry，因此步骤01不应把 Retry 作为成功路径的一部分。
- interop 运行时必须满足 `/www`、`/downloads`、`REQUESTS`、`TESTCASE`、`/certs`、`SSLKEYLOGFILE`、`QLOGDIR` 等契约；客户端/服务端成功结束时退出码必须为 `0`。

##### 4. 本步范围内的实现切片

1. 包与帧最小收发链路
   - 能正确解析/组装 Long Header Initial 与 Handshake 包。
   - 能在对应空间内收发 `CRYPTO`、`ACK`、`PING`、`PADDING`、`CONNECTION_CLOSE`。
   - 首个未确认前使用完整包号语义，不提前依赖截断包号优化。
2. TLS 与 QUIC 的驱动对接
   - 将 TLS 握手字节通过 `CRYPTO` 帧收发。
   - 当 TLS 提供 Handshake/1-RTT 密钥时，及时安装到 QUIC 包保护路径。
   - 将 TLS 告警映射为 QUIC 连接错误，而不是走 TLS record 语义。
3. 握手状态推进
   - ClientHello -> Server Initial/Handshake -> client Handshake 完成 -> 1-RTT 可用。
   - 在正确时机丢弃 Initial 密钥并清理对应恢复状态。
   - 服务端在握手完成时发送 `HANDSHAKE_DONE`。
4. 最小应用数据闭环
   - 握手完成后建立单连接文件下载路径。
   - 对 interop runner 暴露的 `/www` 文件完成最小 HTTP/0.9 下载，并写入 `/downloads`。
5. 最小观测能力
   - 失败时至少能从 `output.txt`、`server/`、`client/`、`sim/`、`QLOGDIR` 进入排查。
   - 若当前日志不足以区分“未发包 / 未解密 / 未推进状态 / 未下载完成”，应先补高信号观测点，再继续改协议逻辑。

##### 5. 明确不纳入步骤01的内容

- `retry`
- `chacha20`
- `v2`
- `transfer` 的大文件流控增长与多流并发
- `resumption`
- `zerortt`
- `keyupdate`
- `rebind-port` / `rebind-addr`
- `http3`
- `connectionmigration`

##### 6. 推荐执行顺序

1. 先打通“客户端 Initial 发出 -> 服务端 Initial/Handshake 响应 -> 客户端 Handshake 回应”的最小密码学路径。
2. 再补 `ACK`、重传、PTO、反放大与密钥丢弃时序，避免出现握手假成功或死锁。
3. 然后接上最小文件下载，确保 handshake testcase 不是“只建连不传输”。
4. 最后在 `quic-interop-runner` 中执行 `handshake`，并以日志、抓包、下载结果和退出码作为验收证据。

##### 7. 验证计划

- 窄验证：
  - 本地 client/server 最小闭环验证，重点确认握手状态推进、密钥安装、Initial/Handshake ACK、`HANDSHAKE_DONE`。
- 高保真验证：
  - 在 `quic-interop-runner` 中执行 `TESTCASE="handshake"`。
  - 必看证据：`logs/<server>_<client>/handshake/output.txt`、`server/`、`client/`、`sim/`、qlog。
- 成功条件：
  - 单连接完成握手。
  - 成功下载小文件且内容匹配。
  - 服务端未走 Retry 路径。
  - 客户端/服务端退出码为 `0`。

##### 8. 当前主要风险

- Initial 未填充到 1200 字节，导致服务端直接丢弃首包。
- Initial / Handshake / 1-RTT 的包号空间、ACK 或丢包恢复混用，造成假重传或假确认。
- TLS 与 QUIC 的密钥安装和 `CRYPTO` 缓冲/重传时序不一致，导致握手卡死。
- Initial 密钥丢弃过早或过晚，导致旧空间包处理异常。
- 只看到“代码路径存在”，却没有 interop 下载成功证据，容易误判为完成。

### 步骤02：密码套件约束能力

- 当前状态：`待核验`
- 目标测试例：`chacha20`
- 实现焦点：TLS 密码套件选择与策略控制。
- 完成判定：双方仅使用 ChaCha20 套件完成握手与下载。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `chacha20` testcase 入口，并向客户端传入 `TLS_CHACHA20_POLY1305_SHA256`。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在步骤01有稳定基线后执行 `chacha20` 验证。

### 步骤03：QUIC v2 协商与兼容

- 当前状态：`待对齐`
- 目标测试例：`v2`
- 实现焦点：版本协商后的 v2 路径兼容、传输参数协同与基础数据收发。
- 完成判定：从 v1 起始连接可升级/协商到 v2 并完成小文件下载。
- 已具备证据：
  - `docs/plans/plan-quic.md` 与 `docs/quic-interop-runner/quic-test-cases.md` 已定义 `v2` testcase。
  - `xquic` 代码中可见 version negotiation 相关路径。
- 仍缺：
  - 当前 `xquic/interop/run_endpoint.sh` 未看到 `v2` testcase 入口。
  - 暂无近期验证记录。
- 下一步：
  - 先确认 `v2` 在当前 xquic interop 包装层的映射方式。
  - 若映射缺失，补齐入口后再做验证。

### 步骤04：流控与多流传输基础

- 当前状态：`待核验`
- 目标测试例：`transfer`
- 实现焦点：连接级/流级流控、ACK 与窗口更新、多流并发数据传输。
- 完成判定：约 1MB 传输期间窗口可正确增长，传输完成且无死锁。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `transfer` testcase 入口。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在握手基线稳定后，补 `transfer` 的本地与 interop 验证。

### 步骤05：Token 与地址验证链路

- 当前状态：`待核验`
- 目标测试例：`retry`
- 实现焦点：Retry token 生成/校验、Initial 重发路径与 CID 替换一致性。
- 完成判定：服务端触发 Retry 后，客户端可携带 token 完成后续握手。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `retry` testcase 入口。
  - `xquic/interop/run_endpoint.sh` 在服务端路径下会为 `retry` 增加 `-r` 参数。
  - `xquic/demo/demo_server.c` 含 Retry 相关配置与回调路径。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在 `handshake` / `transfer` 稳定后，执行 `retry` 并重点核对 token 复用与重发路径。

### 步骤06：握手丢包恢复能力

- 当前状态：`待核验`
- 目标测试例：`multiconnect`
- 实现焦点：高丢包场景下的握手重传、超时与恢复策略。
- 完成判定：多连接在丢包环境下仍可稳定完成握手并下载文件。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `multiconnect` testcase 入口。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在 `retry` 前后收敛出最小可靠重传观测点，再做 `multiconnect` 验证。

### 步骤07：会话恢复路径

- 当前状态：`待核验`
- 目标测试例：`resumption`
- 实现焦点：会话票据存储与复用、二次连接恢复状态机。
- 完成判定：首次连接获取票据，二次连接可完成恢复并下载剩余文件。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `resumption` testcase 入口。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在前置握手与传输稳定后，验证 session ticket 的保存与恢复路径。

### 步骤08：0-RTT 与重放防护

- 当前状态：`待核验`
- 目标测试例：`zerortt`
- 实现焦点：早期数据发送、接收隔离与重放风险控制。
- 完成判定：二次连接可在 0-RTT 阶段请求剩余文件且行为符合协议预期。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `zerortt` testcase 入口。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在 `resumption` 有基线后，单独验证 0-RTT 接受/拒绝与重放边界。

### 步骤09：密钥轮换

- 当前状态：`待核验`
- 目标测试例：`keyupdate`
- 实现焦点：Key Phase 切换、密钥更新时序与新旧密钥并存窗口。
- 完成判定：连接早期触发密钥更新后，传输仍可继续并完成。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `keyupdate` testcase 入口。
  - `xquic/demo/demo_client.c` 与 `xquic/demo/demo_server.c` 中存在 keyupdate 相关参数。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在 `transfer` 稳定后补密钥更新的时序验证。

### 步骤10：端口重绑定路径验证

- 当前状态：`待对齐`
- 目标测试例：`rebind-port`
- 实现焦点：源端口变化检测、路径验证触发、迁移前后状态一致性。
- 完成判定：握手后端口变化不会导致连接断裂，路径验证可通过。
- 已具备证据：
  - `docs/quic-interop-runner/quic-test-cases.md` 已定义 `rebind-port`。
- 仍缺：
  - 当前 `xquic/interop/run_endpoint.sh` 未看到 `rebind-port` testcase 入口。
  - 暂无近期验证记录。
- 下一步：
  - 明确 xquic interop 封装是否已有替代入口；若无，则补齐 testcase 映射。

### 步骤11：地址重绑定路径验证

- 当前状态：`待对齐`
- 目标测试例：`rebind-addr`
- 实现焦点：IP 地址变化下的路径验证、连接状态延续与安全检查。
- 完成判定：地址变化后仍可维持连接并继续传输。
- 已具备证据：
  - `docs/quic-interop-runner/quic-test-cases.md` 已定义 `rebind-addr`。
- 仍缺：
  - 当前 `xquic/interop/run_endpoint.sh` 未看到 `rebind-addr` testcase 入口。
  - 暂无近期验证记录。
- 下一步：
  - 与步骤10一并确认迁移类 testcase 的包装层映射方式。

### 步骤12：HTTP/3 收敛验证

- 当前状态：`待核验`
- 目标测试例：`http3`
- 实现焦点：HTTP/3 控制流/请求流语义映射与 QPACK 协同。
- 完成判定：并行 HTTP/3 请求与文件传输正确完成。
- 已具备证据：
  - `xquic/interop/run_endpoint.sh` 已包含 `http3` testcase 入口。
  - `xquic/src/http3/` 与 `xquic/demo/demo_client.c`、`xquic/demo/demo_server.c` 中可见 HTTP/3 / QPACK 相关代码。
- 仍缺：
  - 暂无近期本地验证结果。
  - 暂无近期 interop 成功日志。
- 下一步：
  - 在传输主线稳定后，再做 H3 / QPACK 收敛验证，避免把协议层与传输层问题混在一起。

### 步骤13：完整连接迁移

- 当前状态：`待对齐`
- 目标测试例：`connectionmigration`
- 实现焦点：CID 池管理、首选地址迁移、迁移期间数据面连续性。
- 完成判定：客户端迁移到新路径后连接持续可用，应用层数据不中断。
- 已具备证据：
  - `docs/quic-interop-runner/quic-test-cases.md` 已定义 `connectionmigration`。
- 仍缺：
  - 当前 `xquic/interop/run_endpoint.sh` 未看到 `connectionmigration` testcase 入口。
  - 暂无近期验证记录。
- 下一步：
  - 在步骤10-12有可靠基线后，再补完整迁移路径的 interop 映射与验证。

## 当前轮次建议

1. 先把 `步骤01 handshake` 跑通并沉淀一条可复现命令与日志入口。
2. 依次补 `步骤04 transfer`、`步骤05 retry` 的验证证据。
3. 再判断 `步骤03 / 10 / 11 / 13` 的缺口主要在实现本身，还是仅在 interop 包装层映射。
