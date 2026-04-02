# MegaLinter Local Execution Specification (v9.4.0)

## 1. 概述 (Overview)
`mega-linter-runner` 允许在本地运行 MegaLinter，用于在 CI/CD 流程前验证代码、应用格式化和修复，而无需在本地逐个安装数十种 Linter 工具。

---

## 2. 安装指南 (Installation)

### 2.1 前置条件 (Pre-requisites)
- **NodeJS**: 用于运行 Runner 脚本。
- **Docker / Podman**: 用于运行包含所有 Linter 的容器镜像。

### 2.2 安装方式
- **全局安装**: `npm install mega-linter-runner -g`
- **本地项目安装**: `npm install mega-linter-runner --save-dev`
- **无安装执行**: 使用 `npx mega-linter-runner [OPTIONS]`

### 2.3 Pre-commit Hook 配置
支持集成到 `pre-commit` 框架中。在 `.pre-commit-config.yaml` 中添加：
```yaml
repos:
  - repo: https://github.com/oxsecurity/megalinter
    rev: v6.8.0 # 对应 hook 的版本
    hooks:
      - id: megalinter-incremental # 增量模式，较快
        stages: [commit]
      - id: megalinter-full        # 全量模式，较慢
        stages: [push]
```

---

## 3. 命令行用法 (Usage)

**基本语法**: `mega-linter-runner [OPTIONS] [FILES]`

### 参数列表 (Options)

| 短参数 | 长参数 | 描述 | 默认值 |
| :--- | :--- | :--- | :--- |
| `-p` | `--path` | 待扫描文件所在目录 | 当前目录 |
| `-f` | `--flavor` | 指定使用特定的 [MegaLinter Flavor](https://megalinter.io/latest/flavors/) | `all` |
| `-d` | `--image` | 覆盖默认 Docker 镜像（支持自定义 Registry） | - |
| `-e` | `--env` | 传递环境变量给 MegaLinter。格式: `'KEY=VALUE'` 或 `"'KEY=V1,V2'"` | - |
| - | `--fix` | **核心功能**：自动应用代码格式化和修复 | - |
| `-r` | `--release` | 指定 MegaLinter 镜像版本 | `v5` |
| - | `--container-engine` | 指定容器引擎 (`docker` 或 `podman`) | `docker` |
| - | `--container-name` | 指定容器运行时的名称 | - |
| - | `--remove-container` | 任务完成后自动删除 Docker 容器 | - |
| `-i` | `--install` | 在当前仓库生成本地配置及 CI/CD 工作流文件 | - |
| `-i` | `--upgrade` | 将配置文件升级至最新版本兼容格式 | - |
| - | `--custom-flavor-setup`| 初始化新仓库以生成自定义 Flavor | - |
| - | `--custom-flavor-linters`| 配合自定义架构使用的 Linter Key 列表（逗号分隔） | - |
| `-h` | `--help` | 显示帮助信息 | - |
| `-v` | `--version` | 显示 Runner 版本 | - |

---

## 4. 示例场景 (Examples)

- **标准运行（全部默认配置）**:
  ```bash
  mega-linter-runner
  ```
- **扫描特定目录并自动修复**:
  ```bash
  mega-linter-runner -p myFolder --fix
  ```
- **通过命令行传递环境变量**:
  ```bash
  mega-linter-runner -r beta -e "'ENABLE=MARKDOWN,YAML'" -e 'SHOW_ELAPSED_TIME=true'
  ```
- **使用特定 Flavor、指定版本并仅扫描部分文件**:
  ```bash
  mega-linter-runner --flavor python --release beta path/to/file1.py path/to/file2.js
  ```

