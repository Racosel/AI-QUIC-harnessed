# 导读：BUILDING

源文件：`ai-quic/boringssl/BUILDING.md`

## 作用

说明 BoringSSL 的构建前置条件、平台构建方式、测试与基准执行方法。

## 重点章节

- `Build Prerequisites`
- `Building`
- `Running Tests`
- `Running Benchmarks`

## 对 AI-QUIC 的直接价值

- 给 `ai-quic/boringssl` 本地编译与验证提供操作基线。
- 便于定位“编译器版本/构建工具”引起的非协议问题。
- 支撑后续把 BoringSSL 编译过程接入仓库级 `Makefile` 目标。

## 快速检索

```bash
rg -n "Prerequisites|Building|Running Tests|Benchmarks|Android|iOS" ai-quic/boringssl/BUILDING.md
```
