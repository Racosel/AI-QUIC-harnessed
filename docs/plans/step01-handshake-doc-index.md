# 步骤01 文档目录：握手最小闭环

更新于 `2026-04-09 14:29:18 +0800`

## 目标

本目录用于步骤01 `handshake` 的最小阅读集，目标是让后续实现与排障始终围绕“单连接完成握手并成功下载小文件”这个最小闭环推进，而不是过早混入 Retry、0-RTT、H3 或迁移问题。

## 当前工作树状态

- 当前工作树未保留 `xquic/` 参考实现目录。
- 当前工作树未保留 `ai-quic/demo/`、`ai-quic/interop/` 与 `ai-quic/src/` 的步骤01落地文件。
- 因此，本文件中涉及这些路径的内容应视为“实现目标与阅读计划”，不应解读为“文件已存在”或“切片已完成”。

## 建议阅读顺序

1. `docs/plans/plan-quic.md`
   - 明确步骤01在全局计划中的位置、验收标准和前后依赖。
2. `docs/plans/repo-file-hierarchy.md`
   - 先确认当前仓库现状、建议中的 `ai-quic/` 目录，以及参考 `xquic` 的模块切分方式。
3. `docs/plans/step01-handshake-logic-design.md`
   - 在编码前先审查“路径1”的模块落点、状态机、收发时序与待拍板决策，避免边写边改架构。
4. `docs/quic-interop-runner/quic-test-cases.md`
   - 明确 `TESTCASE="handshake"` 的测试语义：单连接、小文件下载、服务端不发送 Retry。
5. `docs/quic-interop-runner/how-to-run.md`
   - 明确 `/www`、`/downloads`、`REQUESTS`、`TESTCASE`、`/certs`、日志目录、`SSLKEYLOGFILE`、`QLOGDIR` 的运行契约。
6. `docs/quic-interop-runner/implement-requirements.md`
   - 明确退出码 `0 / 1 / 127` 的判定边界。
7. `docs/ietf/notes/9000.md`
   - 核对 Initial 最小 1200 字节、ACK 行为、包号空间、`CRYPTO` / `HANDSHAKE_DONE` / 反放大规则。
8. `docs/ietf/notes/9001.md`
   - 核对 TLS 1.3 与 QUIC `CRYPTO` 帧对接、密钥安装、Initial/Handshake 密钥丢弃、`HANDSHAKE_DONE`。
9. `docs/ietf/notes/9002.md`
   - 核对握手阶段的 RTT/PTO、Initial/Handshake 空间恢复、反放大与地址验证配合。

## 主题目录

| 主题 | 首选文档 | 本步要点 |
|:--|:--|:--|
| 阶段目标 | `docs/plans/plan-quic.md` | 步骤01只做最小握手闭环，不提前吞并后续步骤。 |
| testcase 语义 | `docs/quic-interop-runner/quic-test-cases.md` | `handshake` 需要单连接、小文件下载、无 Retry。 |
| 路径1逻辑设计 | `docs/plans/step01-handshake-logic-design.md` | 编码前先审查模块落点、状态机、密钥时序和阻断决策。 |
| runner 契约 | `docs/quic-interop-runner/how-to-run.md` | 服务端读 `/www`，客户端写 `/downloads`，请求来自 `REQUESTS`。 |
| 结果判定 | `docs/quic-interop-runner/implement-requirements.md` | 成功必须返回 `0`；错误 `1`；不支持 `127`。 |
| 仓库层级与代码落点 | `docs/plans/repo-file-hierarchy.md` | 先区分当前仓库现状与建议中的 `ai-quic/` 目录，再决定代码应落在哪一层。 |
| Initial / ACK / 帧限制 | `docs/ietf/notes/9000.md` | Initial 至少 1200 字节；Initial/Handshake 立即 ACK；`CRYPTO` 可靠重传。 |
| TLS 集成 | `docs/ietf/notes/9001.md` | TLS 握手必须通过 QUIC `CRYPTO` 帧传输，不走 TLS record。 |
| 恢复与 PTO | `docs/ietf/notes/9002.md` | 握手前应用数据空间不开 PTO；Initial/Handshake 的 `max_ack_delay=0`。 |

## 当前可执行的阅读重点

在当前仓库里，步骤01应先以文档与 runner 契约为主，而不是假定本地已经存在可阅读的参考实现目录。

1. `docs/plans/repo-file-hierarchy.md`
   - 先确认未来 `ai-quic/` 目录应该如何重新落位，避免恢复代码时把模块放错层。
2. `docs/quic-interop-runner/quic-test-cases.md`
   - 先锁定 `handshake` testcase 的真实验收语义。
3. `docs/quic-interop-runner/how-to-run.md`
   - 先锁定 `/www`、`/downloads`、`REQUESTS`、`TESTCASE`、`SSLKEYLOGFILE`、`QLOGDIR` 等 runner 契约。
4. `docs/ietf/notes/9000.md`、`docs/ietf/notes/9001.md`、`docs/ietf/notes/9002.md`
   - 先锁定步骤01最小握手必须满足的传输、TLS 与恢复语义。
5. 若后续重新引入参考实现快照
   - 再补读相应的 demo、interop、transport、tls 目录，不把这一步写死到当前仓库路径上。

## 步骤01最小实现清单

- 客户端能发送填充后的 Initial。
- 服务端能接收 Initial，启动 TLS，回发 Initial/Handshake。
- 客户端能处理服务端 Initial/Handshake，推进到 Handshake / 1-RTT。
- 双端能在正确时机安装新密钥并丢弃 Initial 密钥。
- 服务端握手完成后发送 `HANDSHAKE_DONE`。
- 双端能在单连接上完成最小文件下载。
- 失败时能从 runner 日志、端点日志、pcap、qlog 快速定位卡点。

## 关键约束速查

- 本步不允许把 Retry 作为成功路径的一部分。
- Initial 和 Handshake 包必须立即 ACK。
- `CRYPTO` 数据必须可靠重传，且用原始加密级别重传。
- 地址验证完成前，服务端受 3 倍反放大限制。
- 客户端首次发送 Handshake 包后丢弃 Initial 密钥；服务端首次成功处理 Handshake 包后丢弃 Initial 密钥。
- `HANDSHAKE_DONE` 只能由服务端在握手完成后发送。

## 如何启动 Interop 测试

### 1. 进入 runner 目录

```bash
cd /home/racosel/Desktop/quic/quic-interop-runner
```

### 2. 首次运行前安装依赖

```bash
pip3 install -r requirements.txt
```

说明：

- `run.py` 在当前目录下执行。
- 需要本机已安装 Docker、docker compose、Wireshark 4.5.0+。
- 如果显式指定 `-l <日志目录>`，该目录必须不存在；否则 runner 会直接退出。

### 3. 推荐的步骤01运行命令

#### 命令 A：同实现自检烟雾测试

先确认 runner、镜像、日志路径和最小 handshake 流程本身能跑起来：

```bash
python3 run.py -d -s xquic -c xquic -t handshake -l logs_step01_handshake_smoke
```

适用场景：

- 先验证 runner 基础链路是否可用。
- 先确认 `handshake` testcase 的日志结构和结果目录是否正常生成。

注意：

- 这只是最小烟雾测试，不足以证明真实互操作性。
- 同实现双端更容易掩盖兼容性问题，不能当作步骤01最终完成证据。

#### 命令 B：更有价值的跨实现测试

优先用跨实现组合验证，至少跑一组“本实现做 server”或“本实现做 client”的组合：

```bash
python3 run.py -d -s xquic -c quic-go -t handshake -l logs_step01_handshake_xquic_server
python3 run.py -d -s quic-go -c xquic -t handshake -l logs_step01_handshake_xquic_client
```

说明：

- `-s` 是 server 实现名，`-c` 是 client 实现名。
- 当前 runner 注册文件 `implementations_quic.json` 中可直接使用的名字包括 `xquic`、`quic-go`、`ngtcp2`、`aioquic`、`msquic` 等。
- 若目标是验证“我方实现作为服务端”能力，优先看 `-s 我方 -c 对端`。
- 若目标是验证“我方实现作为客户端”能力，优先看 `-s 对端 -c 我方`。

#### 命令 C：替换为本地镜像

如果你已经构建了本地镜像，不想直接使用 `implementations_quic.json` 里的远端镜像，可以用 `-r` 替换：

```bash
python3 run.py -d -s xquic -c quic-go -t handshake -l logs_step01_handshake_local -r xquic=<你的本地镜像tag>
```

例子：

```bash
python3 run.py -d -s xquic -c quic-go -t handshake -l logs_step01_handshake_local -r xquic=my-xquic-interop:dev
```

适用场景：

- 你在本地修改了实现，已经构建了新的 interop 镜像。
- 想保留 runner 中 `xquic` 这个实现名，但临时换成自己的镜像测试。

### 4. 推荐参数说明

- `-d`：打印更多调试日志，步骤01建议始终开启。
- `-t handshake`：只跑步骤01对应 testcase，避免被其他 testcase 噪音干扰。
- `-l <dir>`：显式指定日志目录，方便反复比较不同轮结果。
- `-s` / `-c`：锁定 server/client 组合，避免一次性跑完整矩阵。

## 如何读取测试结果

### 1. 先看 runner 总输出

无论成功还是失败，先看终端输出；如果指定了 `-l logs_step01_handshake_smoke`，重点目录是：

```text
logs_step01_handshake_smoke/<server>_<client>/handshake/
```

例如：

```text
logs_step01_handshake_smoke/xquic_xquic/handshake/
logs_step01_handshake_xquic_server/xquic_quic-go/handshake/
logs_step01_handshake_xquic_client/quic-go_xquic/handshake/
```

先列一下实际生成了哪些文件：

```bash
find logs_step01_handshake_smoke/xquic_xquic/handshake -maxdepth 2 -type f | sort
```

### 2. 第一优先级：`output.txt`

`output.txt` 是 runner 的总控制台输出，也是第一排查入口：

```bash
sed -n '1,220p' logs_step01_handshake_smoke/xquic_xquic/handshake/output.txt
```

如果只想先看失败关键词：

```bash
rg -n "exited with code|Didn't expect a Retry|Expected exactly 1 handshake|Wrong version|Missing files|doesn't match" \
  logs_step01_handshake_smoke/xquic_xquic/handshake/output.txt
```

为什么先看这里：

- runner 会先记录容器退出码。
- `handshake` 的 testcase 检查失败原因会直接写进这里。
- 这里能最快区分“镜像/容器没起来”和“协议握手失败但 runner 正常执行”。

### 3. 第二优先级：端点日志

先列出 client/server 各自产生了哪些日志文件：

```bash
find logs_step01_handshake_smoke/xquic_xquic/handshake/client -type f | sort
find logs_step01_handshake_smoke/xquic_xquic/handshake/server -type f | sort
```

对 `xquic` 来说，通常重点先看：

```bash
sed -n '1,220p' logs_step01_handshake_smoke/xquic_xquic/handshake/client/client.log
sed -n '1,220p' logs_step01_handshake_smoke/xquic_xquic/handshake/server/server.log
```

读取顺序建议：

1. 先看服务端有没有成功收到 Initial，是否推进到 Handshake。
2. 再看客户端有没有安装 Handshake/1-RTT 密钥，是否收到 `HANDSHAKE_DONE`。
3. 最后看下载路径是否已经进入最小文件请求/保存。

### 4. 第三优先级：模拟器抓包 `sim/`

如果 `output.txt` 和端点日志不足以判断问题在“未发包 / 未回包 / 回包未解密 / 状态未推进”，就看 `sim/`：

```bash
find logs_step01_handshake_smoke/xquic_xquic/handshake/sim -maxdepth 1 -type f | sort
```

这里通常重点是 pcap 文件，例如 `trace_node_left.pcap`、`trace_node_right.pcap`。步骤01下看抓包主要是回答这些问题：

- 客户端 Initial 是否真的发出去了。
- 服务端是否回了 Initial / Handshake。
- 是否出现了不该出现的 Retry。
- 握手是否重复发生了多次。

### 5. 第四优先级：qlog / keylog

如果实现导出了 qlog 或 TLS keylog，再继续看这些文件：

```bash
find logs_step01_handshake_smoke/xquic_xquic/handshake -type f \( -name '*.qlog' -o -name '*.sqlog' -o -name '*keylog*' \) | sort
```

适用场景：

- 端点日志不足以判断具体卡在哪个加密级别。
- 需要确认 `CRYPTO` 数据、密钥安装、`HANDSHAKE_DONE`、ACK/PTO 时序。

## `handshake` testcase 实际如何判成功

根据 `quic-interop-runner/testcases_quic.py` 与 `testcase.py`，`handshake` 不是“只要握手包交换过就算成功”，而是至少要同时满足：

- 下载文件成功，且下载内容与 server 端生成的文件完全一致。
- 版本检查通过，当前应是单一 QUIC v1。
- 服务端没有发送 Retry。
- 抓包中只能出现恰好 1 次握手。

这意味着：

- “客户端和服务端都打印了 handshake finished” 还不够。
- “只建连成功但文件没落到 `/downloads`” 也不算通过。
- “握手成功但出现 Retry” 在步骤01里仍算失败。

## 建议的排障顺序

1. 先看 `output.txt`，确认是容器启动/退出问题，还是 testcase 语义检查失败。
2. 再看 `server/` 与 `client/` 日志，定位卡在 Initial、Handshake、1-RTT 还是下载阶段。
3. 如果日志不足，再看 `sim/` 抓包确认真实发包与回包。
4. 如果仍不够，再看 qlog / keylog。
5. 每次定位后，把结论同步回当前仍在维护的状态文档或交接记录；不要假定 `process.md`、`log`、`debug`、`break` 一定存在。

## 验证证据目录

- `logs/<server>_<client>/handshake/output.txt`
- `logs/<server>_<client>/handshake/server/`
- `logs/<server>_<client>/handshake/client/`
- `logs/<server>_<client>/handshake/sim/`
- `QLOGDIR` 输出目录

## 使用方式

- 规划步骤01时，先读本文件，再按“建议阅读顺序”补齐原始文档。
- 需要决定代码应该落在哪时，先读 `docs/plans/repo-file-hierarchy.md`，不要把“当前参考实现目录”和“未来我方目录”混在一起。
- 运行步骤01时，优先复制本文件中的命令模板，并显式指定新的 `-l` 日志目录。
- 读测试结果时，固定按 `output.txt -> server/client -> sim -> qlog` 的顺序排查。
- 新增、删除或重命名本文件后，必须同步更新 `docs/menu.md`。
