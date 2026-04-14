# AI-QUIC 仓库文件层级与模块划分

更新于 `2026-04-09 10:17:21 +0800`

## 目标

本文件用于记录当前仓库的实际文件层级，并给出后续 `ai-quic/` 的建议模块划分。它不是在宣称“目录已经落地”，而是用于避免后续把参考实现、文档、runner 和我方代码落点混在一起。

## Agent 执行职责

在本仓库中，agent 执行新设计时遵循以下边界：

- 以 `docs/` 为第一约束来源，先读阶段计划、runner 契约、RFC 笔记，再开始设计或实现。
- 若当前工作树重新引入 `xquic/` 或其他参考实现快照，可用它帮助拆模块、找入口、对齐最小运行路径；但不把新设计直接写进参考实现目录。
- 以 `ai-quic/` 为新的设计与实现落点；新的源码、测试、demo、interop 包装都应放在该目录下。
- 若 `docs/` 与参考实现行为不一致，优先服从 `docs/` 与用户最新要求；若 `docs/` 缺口导致无法安全推进，应先补文档或记录缺口。

## 当前仓库现状

当前仓库顶层可以按职责先分成以下几类：

| 路径 | 当前角色 | 说明 |
|:--|:--|:--|
| `docs/` | 规划与参考文档 | 阶段计划、RFC 笔记、interop 使用说明都在这里。 |
| `quic-interop-runner/` | 外部互操作测试运行器 | 负责 testcase 调度、容器编排、日志落盘与结果判定。 |
| `ai-quic/` | 我方实现主目录 | 当前保留项目顶层构建入口与 `boringssl/` 子树，步骤01源码骨架尚未重新落地。 |
| `skills/` | 仓库本地 skill | 约束工作流、日志、治理和代码健康检查。 |

需要明确的一点：

- 当前工作树中未保留 `xquic/` 参考目录。
- 当前 `ai-quic/` 仅保留项目顶层文件与 `boringssl/` 子树，`include/src/demo/interop/tests` 这类步骤01骨架目录当前未在工作树中。
- 后续实现应继续沿用本文的模块职责划分，逐步替换占位文件为真实实现。

## 参考实现的模块划分

若后续重新引入参考实现快照，可优先借鉴如下层级职责，而不是依赖某个当前并不存在的本地路径：

| 参考层级 | 模块职责 | 对 AI-QUIC 的启发 |
|:--|:--|:--|
| `include/` | 对外公开头文件与类型定义 | 我方公共 API、错误码、配置头应独立放在 `include/`。 |
| `src/common/` | 日志、时间、随机数、字符串、容器等公共基础设施 | 公共工具层与协议层分开，避免把基础设施塞进 transport。 |
| `src/transport/` | 连接、包、帧、流、发送控制、定时器、传输参数等 QUIC 传输核心 | 步骤01到步骤11的大部分核心逻辑都应集中在这里。 |
| `src/tls/` | QUIC 与 TLS 的接口、HKDF、密钥安装、SSL 适配层 | TLS/加解密逻辑与 transport 解耦，便于调试与替换后端。 |
| `src/congestion_control/` | Cubic、BBR、Reno、Copa 等拥塞控制实现 | 拥塞控制应独立于连接/收发主逻辑。 |
| `src/http3/` | HTTP/3 连接、请求、流、QPACK | 应与纯 QUIC transport 分层，避免步骤01过早混入 H3。 |
| `demo/` | demo client/server 与最小 HQ 请求路径 | 最小可运行样例与库核心代码分开。 |
| `interop/` | interop 包装脚本与镜像入口 | 互操作入口应单独组织，避免污染核心协议目录。 |
| `tests/unittest/` | 单元测试与工具型测试 | 窄测试应与 demo/interop 分离。 |
| `mini/` | 更轻量的最小样例 | 如需超小闭环 demo，可独立于主 demo。 |
| 扩展目录（如 `moq/`） | MOQ 等扩展能力 | 扩展协议或实验特性不应混进步骤01主线目录。 |

## 建议中的 `ai-quic/` 目录骨架

参考 `xquic` 后，建议后续我方代码按如下层级组织：

```text
ai-quic/
├── include/
│   └── ai_quic/
├── src/
│   ├── common/
│   ├── transport/
│   ├── tls/
│   ├── congestion_control/
│   └── http3/
├── demo/
├── interop/
└── tests/
    └── unittest/
```

各层职责建议如下：

- `ai-quic/include/ai_quic/`
  - 放对外暴露的公共头文件、类型定义、错误码和基础配置。
- `ai-quic/src/common/`
  - 放日志、时间、缓冲区、队列、哈希、随机数、平台无关小工具。
- `ai-quic/src/transport/`
  - 放连接、包头、帧编解码、流、ACK/丢包恢复、发送控制、CID、路径与传输参数。
- `ai-quic/src/tls/`
  - 放 QUIC-TLS 对接、密钥派生、密钥安装、TLS 上下文与 SSL 适配。
- `ai-quic/src/congestion_control/`
  - 放拥塞控制算法与采样辅助逻辑。
- `ai-quic/src/http3/`
  - 放 HTTP/3 控制流、请求流、QPACK 与 H3 扩展；步骤01可暂不实现，但目录职责应预留。
- `ai-quic/demo/`
  - 放最小 client/server 样例、HTTP/0.9 或 HQ 下载路径。
- `ai-quic/interop/`
  - 放 runner 适配脚本、Dockerfile、镜像入口和 testcase 包装逻辑。
- `ai-quic/tests/unittest/`
  - 放窄范围单元测试；若后续需要更高保真测试，可再追加 `tests/integration/`。

## 步骤01优先需要的模块

若当前目标仍是 `步骤01 handshake`，建议只先创建并优先阅读以下目录：

| 建议目录 | 步骤01用途 | 参考 `xquic` |
|:--|:--|:--|
| `ai-quic/src/transport/` | Initial/Handshake 包收发、帧处理、状态推进、ACK/PTO | 参考实现的 `transport` 层 |
| `ai-quic/src/tls/` | `CRYPTO`、TLS 驱动、密钥安装、密钥丢弃 | 参考实现的 `tls` 层 |
| `ai-quic/src/common/` | 日志、时间、缓冲区与基础工具 | 参考实现的 `common` 层 |
| `ai-quic/demo/` | 最小下载闭环与本地 smoke 入口 | 参考实现的 `demo` 层 |
| `ai-quic/interop/` | `handshake` testcase 的镜像入口与运行脚本 | 参考实现的 `interop` 层 |
| `ai-quic/tests/unittest/` | 包解析、TLS 过渡、状态机窄测试 | 参考实现的 `tests/unittest` 层 |

步骤01暂不应把以下目录当主线：

- `ai-quic/src/http3/`
- `ai-quic/src/congestion_control/`
- 任何类似 `moq/` 的扩展实验目录

原因是步骤01验收只要求“单连接完成握手并成功下载小文件”，不要求先做完 H3、复杂拥塞控制变体或扩展协议。

## 文件落点规则

为了避免后续目录继续长歪，新增文件时可按下面的判断方式放置：

- 新增包头解析、帧编解码、连接状态推进、流状态、ACK/PTO、CID、传输参数相关代码：
  - 放 `ai-quic/src/transport/`
- 新增 TLS 握手、密钥派生、加解密后端适配、TLS 上下文：
  - 放 `ai-quic/src/tls/`
- 新增日志、计时器基础封装、字符串/缓冲区/队列等通用工具：
  - 放 `ai-quic/src/common/`
- 新增最小 client/server 示例、HTTP/0.9 下载路径、手工 smoke 命令入口：
  - 放 `ai-quic/demo/`
- 新增 runner 入口、Dockerfile、容器启动包装脚本：
  - 放 `ai-quic/interop/`
- 新增单元测试、回归测试夹具：
  - 放 `ai-quic/tests/unittest/`
- 仅用于阶段规划、目录说明、实现边界说明的文档：
  - 继续放 `docs/plans/`

## 与步骤01文档的关系

- `docs/plans/step01-handshake-doc-index.md` 负责告诉你“步骤01应该先读什么、怎么跑、怎么验”。
- 本文件负责告诉你“代码应该落在哪、参考 `xquic` 时应如何映射目录”。
- 开始真正创建我方代码目录前，应先同时读这两个文件，避免一边按步骤01实现，一边把文件放错层。
