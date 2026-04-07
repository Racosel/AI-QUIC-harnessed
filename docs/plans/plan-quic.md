### QUIC Interop 单例梯度计划（易 -> 难）

排序依据：仅使用 `docs/quic-interop-runner/quic-test-cases.md` 中定义的测试例，按实现复杂度与依赖关系从易到难排列。

#### 模块映射规则

- `IO/包头/TLS/加解密` -> 步骤01-03
- `流控与传输` -> 步骤04
- `Retry/反放大` -> 步骤05
- `丢包检测与PTO` -> 步骤06
- `恢复/0-RTT/密钥更新` -> 步骤07-09
- `CID/路径验证` -> 步骤10-12
- `H3/QPACK` -> 步骤13

### 步骤00：Version Negotiation（禁用占位）

- **目标测试例(TESTCASE)**：`TESTCASE="versionnegotiation"`
- **实现焦点**：版本协商报文编码与未知版本处理路径；仅维护实现入口，不纳入当前可执行验收。
- **验收标准**：能明确记录“当前禁用（#20）”状态，代码路径可保留但默认不作为发布阻塞项。
- **前置依赖**：无（占位步骤）。

### 步骤01：握手最小闭环

- **目标测试例(TESTCASE)**：`TESTCASE="handshake"`
- **实现焦点**：最小可用握手链路，包括 Initial/Handshake 基本收发、握手状态推进与连接建立。
- **验收标准**：单连接完成握手并成功下载小文件。
- **前置依赖**：步骤00。

### 步骤02：密码套件约束能力

- **目标测试例(TESTCASE)**：`TESTCASE="chacha20"`
- **实现焦点**：TLS 密码套件选择与策略控制，确保在受限套件下仍能握手成功。
- **验收标准**：双方仅使用 ChaCha20 套件完成握手与下载。
- **前置依赖**：步骤01。

### 步骤03：QUIC v2 协商与兼容

- **目标测试例(TESTCASE)**：`TESTCASE="v2"`
- **实现焦点**：版本协商后的 v2 路径兼容、传输参数协同与基础数据收发。
- **验收标准**：从 v1 起始连接可升级/协商到 v2 并完成小文件下载。
- **前置依赖**：步骤02。

### 步骤04：流控与多流传输基础

- **目标测试例(TESTCASE)**：`TESTCASE="transfer"`
- **实现焦点**：连接级/流级流控、ACK 与窗口更新、多流并发数据传输。
- **验收标准**：约 1MB 传输期间窗口可正确增长，传输完成且无死锁。
- **前置依赖**：步骤03。

### 步骤05：Token 与地址验证链路

- **目标测试例(TESTCASE)**：`TESTCASE="retry"`
- **实现焦点**：Retry token 生成/校验、Initial 重发路径与 CID 替换一致性。
- **验收标准**：在服务端触发 Retry 的情况下，客户端可携带 token 完成后续握手。
- **前置依赖**：步骤04。

### 步骤06：握手丢包恢复能力

- **目标测试例(TESTCASE)**：`TESTCASE="multiconnect"`
- **实现焦点**：高丢包场景下的握手重传、超时与恢复策略，避免连接建立不稳定。
- **验收标准**：多连接（顺序或并行）在丢包环境下仍可稳定完成握手并下载文件。
- **前置依赖**：步骤05。

### 步骤07：会话恢复路径

- **目标测试例(TESTCASE)**：`TESTCASE="resumption"`
- **实现焦点**：会话票据存储与复用、二次连接恢复状态机（不含 0-RTT 数据）。
- **验收标准**：首次连接获取票据，二次连接可完成恢复并下载剩余文件。
- **前置依赖**：步骤06。

### 步骤08：0-RTT 与重放防护

- **目标测试例(TESTCASE)**：`TESTCASE="zerortt"`
- **实现焦点**：早期数据发送、接收隔离与重放风险控制。
- **验收标准**：二次连接可在 0-RTT 阶段请求剩余文件且行为符合协议预期。
- **前置依赖**：步骤07。

### 步骤09：密钥轮换

- **目标测试例(TESTCASE)**：`TESTCASE="keyupdate"`
- **实现焦点**：Key Phase 切换、密钥更新时序与新旧密钥并存容错窗口。
- **验收标准**：连接早期触发密钥更新后，传输仍可继续并完成。
- **前置依赖**：步骤08。

### 步骤10：端口重绑定路径验证

- **目标测试例(TESTCASE)**：`TESTCASE="rebind-port"`
- **实现焦点**：源端口变化检测、路径验证触发、迁移前后状态一致性。
- **验收标准**：握手后端口变化不会导致连接断裂，路径验证可通过。
- **前置依赖**：步骤09。

### 步骤11：地址重绑定路径验证

- **目标测试例(TESTCASE)**：`TESTCASE="rebind-addr"`
- **实现焦点**：IP 地址变化下的路径验证、连接状态延续与安全检查。
- **验收标准**：地址变化后仍可维持连接并继续传输。
- **前置依赖**：步骤10。

### 步骤12：完整连接迁移

- **目标测试例(TESTCASE)**：`TESTCASE="connectionmigration"`
- **实现焦点**：CID 池管理、首选地址迁移、迁移期间数据面连续性。
- **验收标准**：客户端迁移到新路径后连接持续可用，应用层数据不中断。
- **前置依赖**：步骤11。

### 步骤13：HTTP/3 收敛验证

- **目标测试例(TESTCASE)**：`TESTCASE="http3"`
- **实现焦点**：HTTP/3 控制流/请求流语义映射与 QPACK 协同。
- **验收标准**：并行 HTTP/3 请求与文件传输正确完成。
- **前置依赖**：步骤12。

### 扩展验证（待补资料）

以下条目不在 `docs/quic-interop-runner/quic-test-cases.md` 当前测试清单内，已从主梯度移出：

- **历史计划条目（需额外映射或资料）**：`ipv6`、`multiplexing`、`handshakeloss`、`transferloss`、`longrtt`、`blackhole`、`amplificationlimit`、`handshakecorruption`、`transfercorruption`、`goodput`、`crosstraffic`、`ecn`。
- **处理规则**：作为后续扩展验证维护，不影响当前步骤00-13主线验收。

### 附录：开发执行要求（跨目录抄写）

#### 1. Interop 运行与端点契约（来源：`docs/quic-interop-runner/how-to-run.md`）

- Interop Runner 通过环境变量和挂载目录与容器交互。
- 服务端容器必须在 `443` 端口提供服务，并处理挂载到 `/www` 的文件目录。
- 客户端容器需将下载结果写入 `/downloads`，并通过 `REQUESTS` 环境变量接收下载 URL 列表。
- 测试用例通过 `TESTCASE` 环境变量传递。
- 证书与密钥由运行器挂载到 `/certs`，服务端需从 `priv.key` 和 `cert.pem` 加载。
- 实现必须以 Docker 镜像封装；在线互操作环境要求支持 `linux/amd64`。
- 若在非 `amd64` 主机构建镜像，需使用 `docker build --platform linux/amd64` 或 `docker buildx` 多平台构建。
- 在 Linux 上执行 IPv6 相关测试前，应先加载 `ip6table_filter` 模块：`sudo modprobe ip6table_filter`。

#### 2. 容器退出码硬约束（来源：`docs/quic-interop-runner/implement-requirements.md`）

- 退出码 `0`：测试任务成功完成。
- 退出码 `1`：执行出错（运行异常、传输中止、协议验证失败）。
- 退出码 `127`：当前实现不支持该 `TESTCASE`。
- 退出码契约用于区分“失败”和“未支持”，必须严格遵守。

#### 3. 测试与观测要求（来源：`docs/quic-interop-runner/how-to-run.md`、`docs/plans/plan-ai.md`）

- 互操作测试可使用 `python3 run.py`，或用 `-s`、`-c`、`-t` 指定 server/client/testcase 组合。
- 日志目录固定为 `logs/<server>_<client>/<testcase>/`，关键排查入口：
  - `output.txt`：运行器控制台输出与失败原因。
  - `server/`、`client/`：端点日志。
  - `sim/`：模拟器 pcap 抓包。
- TLS 密钥日志应通过 `SSLKEYLOGFILE` 导出，qlog 文件应输出到 `QLOGDIR` 指定目录。
- 开发过程需从早期接入 qlog，保障失败时可追踪。
- 每完成一步实现，都应在 `quic-interop-runner` 环境中验证，不以 AI 生成测试结果作为唯一验收依据。

#### 4. 质量门禁要求（来源：`docs/mega-linter/config.md`、`docs/mega-linter/run-locally.md`、`docs/plans/plan-ai.md`）

- MegaLinter 配置优先级：环境变量（ENV）高于 `.mega-linter.yml`。
- 本地运行前置依赖：NodeJS + Docker/Podman。
- 本地运行方式：`mega-linter-runner` 或 `npx mega-linter-runner [OPTIONS]`。
- 需要自动修复时可启用 `--fix`；启用后仍需执行互操作测试与核心用例复验，避免仅通过格式化掩盖行为问题。
- 建议将代码规范检查与运行时检查结合（如 MegaLinter + Sanitizer）作为质量门禁。

#### 5. 开发流程纪律（来源：`docs/plans/plan-ai.md`）

- 每轮改动优先聚焦单一模块，降低问题定位复杂度。
- 定期更新项目日志与完成状态标记，并记录“如何判定已完成”。
- 定期清理无效文件，并检查是否存在违反既定流程的操作。
- 对于资料缺失或工具不可用场景，必须先提出缺失项与补充请求，再继续推进实现。
