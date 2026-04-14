# 导读：BREAKING-CHANGES

源文件：`ai-quic/boringssl/BREAKING-CHANGES.md`

## 作用

说明 BoringSSL 处理潜在破坏性变更的策略，包括风险评估、消费者修复、灰度与回滚思路。

## 重点章节

- `Breakage risk`
- `Fixing consumers`
- `Fail early, fail closed`
- `Unexpected breakage`
- `Canary changes and bake time`

## 对 AI-QUIC 的直接价值

- 为升级 `ai-quic/boringssl` 版本提供变更风险评估框架。
- 指导在仓库内实施“先局部验证，再全面切换”的升级策略。

## 快速检索

```bash
rg -n "Breakage risk|Fixing consumers|Fail early|Unexpected breakage|Canary" ai-quic/boringssl/BREAKING-CHANGES.md
```
