# 导读：INCORPORATING

源文件：`ai-quic/boringssl/INCORPORATING.md`

## 作用

说明如何把 BoringSSL 以“随上游更新”的方式纳入项目，包括分支策略、目录布局、构建支持和符号处理。

## 重点章节

- `Which branch to use`
- `Directory layout`
- `Build support`
- `Defines`
- `Symbols`

## 对 AI-QUIC 的直接价值

- 帮助确定 `ai-quic/boringssl` 在仓库中的集成边界与升级策略。
- 为后续链接方式、符号前缀冲突规避提供依据。
- 降低升级 BoringSSL 时对主干代码的破坏风险。

## 快速检索

```bash
rg -n "branch|layout|Build support|Defines|Symbols|Bazel" ai-quic/boringssl/INCORPORATING.md
```
