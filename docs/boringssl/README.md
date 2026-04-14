# BoringSSL 资料整理索引

更新于 `2026-04-09 11:04:55 +0800`

## 目的

本目录将 `ai-quic/boringssl` 中对接最常用的说明文件整理为中文导读，便于在 AI-QUIC 项目中快速检索和落地。

## 来源范围

- `ai-quic/boringssl/PORTING.md`
- `ai-quic/boringssl/BUILDING.md`
- `ai-quic/boringssl/INCORPORATING.md`
- `ai-quic/boringssl/API-CONVENTIONS.md`
- `ai-quic/boringssl/STYLE.md`
- `ai-quic/boringssl/include/openssl/`
- `ai-quic/boringssl/FUZZING.md`
- `ai-quic/boringssl/CONTRIBUTING.md`
- `ai-quic/boringssl/BREAKING-CHANGES.md`
- `ai-quic/boringssl/SANDBOXING.md`

## 建议阅读顺序

1. `docs/boringssl/porting.md`
2. `docs/boringssl/incorporating.md`
3. `docs/boringssl/building.md`
4. `docs/boringssl/api-conventions.md`
5. `docs/boringssl/public-headers.md`
6. `docs/boringssl/style.md`
7. `docs/boringssl/breaking-changes.md`
8. `docs/boringssl/sandboxing.md`
9. `docs/boringssl/fuzzing.md`
10. `docs/boringssl/contributing.md`

## 文档清单

| 整理文档 | 对应源文件 | 用途 |
|:--|:--|:--|
| `docs/boringssl/porting.md` | `ai-quic/boringssl/PORTING.md` | OpenSSL 代码迁移到 BoringSSL 的差异与替代方案。 |
| `docs/boringssl/building.md` | `ai-quic/boringssl/BUILDING.md` | BoringSSL 构建、测试、基准的本地操作说明。 |
| `docs/boringssl/incorporating.md` | `ai-quic/boringssl/INCORPORATING.md` | 把 BoringSSL 纳入项目构建与目录管理的方法。 |
| `docs/boringssl/api-conventions.md` | `ai-quic/boringssl/API-CONVENTIONS.md` | API 使用约定、错误处理、生命周期与线程语义。 |
| `docs/boringssl/style.md` | `ai-quic/boringssl/STYLE.md` | BoringSSL 代码风格和开发约束。 |
| `docs/boringssl/public-headers.md` | `ai-quic/boringssl/include/openssl/` | 公共头文件入口和按功能检索方法。 |
| `docs/boringssl/fuzzing.md` | `ai-quic/boringssl/FUZZING.md` | Fuzz 构建和运行建议。 |
| `docs/boringssl/contributing.md` | `ai-quic/boringssl/CONTRIBUTING.md` | 提交流程、评审与贡献规范。 |
| `docs/boringssl/breaking-changes.md` | `ai-quic/boringssl/BREAKING-CHANGES.md` | 潜在破坏性变更的评估与发布策略。 |
| `docs/boringssl/sandboxing.md` | `ai-quic/boringssl/SANDBOXING.md` | 沙箱环境下使用 BoringSSL 的依赖和风险。 |

## 说明

- 本目录是“导读与索引”，不替代上游原文。
- 需要精确语义时，始终回到对应源文件和头文件注释。
