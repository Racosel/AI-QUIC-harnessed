# 导读：SANDBOXING

源文件：`ai-quic/boringssl/SANDBOXING.md`

## 作用

说明在沙箱环境下使用 BoringSSL 的基线依赖、初始化注意事项和潜在运行风险。

## 重点章节

- `Baseline dependencies`
- `Initialization`
- `Entropy`
- `Fork protection`

## 对 AI-QUIC 的直接价值

- 对接容器、受限系统调用环境时，可提前评估 BoringSSL 运行前提。
- 帮助定位“非协议层”运行失败（熵源、线程、标准库依赖）问题。

## 快速检索

```bash
rg -n "Baseline dependencies|Initialization|Entropy|Fork protection" ai-quic/boringssl/SANDBOXING.md
```
