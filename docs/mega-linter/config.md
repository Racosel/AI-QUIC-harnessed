# MegaLinter Configuration Specification (v9.4.0)

## 1\. 配置加载机制 (Configuration Loading)

Agent 应遵循以下优先级和规则读取配置：

  - **配置文件**：项目根目录下的 `.mega-linter.yml`。
  - **环境变量**：可通过系统 ENV 定义。
  - **优先级**：环境变量 (ENV) \> `.mega-linter.yml`。
  - **辅助功能**：支持 [schemastore.org](https://www.google.com/search?q=https://schemastore.org) 的 JSON Schema 校验。

-----

## 2\. 核心通用变量 (Common Variables)

| 变量名 | 默认值 | 描述 |
| :--- | :--- | :--- |
| `VALIDATE_ALL_CODEBASE` | `true` | `true`: 扫描全库; `false`: 仅扫描新文件/编辑过的文件。 |
| `APPLY_FIXES` | `none` | 激活格式化和自动修复。可选值：`all` 或 逗号分隔的 Linter Key 列表。 |
| `DEFAULT_BRANCH` | `HEAD` | 仓库默认分支，配合 `VALIDATE_ALL_CODEBASE: false` 使用。 |
| `LINTER_RULES_PATH` | `.github/linters` | 存放 linter 规则文件的本地目录或远程 URL。 |
| `MEGALINTER_FILES_TO_LINT` | `[]` | 显式指定要分析的文件列表（逗号分隔），将绕过其他过滤逻辑。 |
| `PARALLEL` | `true` | 是否并行处理以提升性能。 |
| `LOG_LEVEL` | `INFO` | 日志级别：`INFO`, `DEBUG`, `WARNING`, `ERROR`。 |
| `REPORT_OUTPUT_FOLDER` | `megalinter-reports` | 报告输出目录，设置为 `none` 则不生成。 |

-----

## 3\. 激活与过滤逻辑 (Activation & Filtering)

### 3.1 启用/禁用控制器

Agent 在决定运行哪些 Linter 时应处理以下逻辑：

1.  **全集控制**：
      - `ENABLE`: 仅激活指定的描述符（Descriptor）。
      - `DISABLE`: 跳过指定的描述符。
2.  **细粒度控制**：
      - `ENABLE_LINTERS`: 仅运行指定的 Linter Key。
      - `DISABLE_LINTERS`: 跳过指定的 Linter Key。
3.  **阻塞控制**：
      - `DISABLE_ERRORS_LINTERS`: 运行但不阻塞（Exit Code 0）。
      - `ENABLE_ERRORS_LINTERS`: 仅指定的 Linter 错误会触发阻塞。

### 3.2 正则过滤 (Regex Filtering)

  - **全局过滤**：使用 `FILTER_REGEX_INCLUDE` 和 `FILTER_REGEX_EXCLUDE`。
  - **特定 Linter 过滤**：使用 `<LINTER_KEY>_FILTER_REGEX_INCLUDE`。
  - **目录排除**：`ADDITIONAL_EXCLUDED_DIRECTORIES`（递归排除列表中的目录名）。

-----

## 4\. 自动修复配置 (Apply Fixes)

**仅限 GitHub Actions 环境变量配置：**

  - `APPLY_FIXES_EVENT`: 触发事件 (`all`, `push`, `pull_request`, `none`)。
  - `APPLY_FIXES_MODE`: 修复方式 (`commit` 直接提交, `pull_request` 创建新 PR)。
  - **安全建议**：建议使用 Fine-Grained PAT 并存储在 Secret `PAT` 中。

-----

## 5\. 命令钩子 (Command Hooks)

支持在执行流中插入自定义 Bash 命令。

### 5.1 结构定义

```yaml
PRE_COMMANDS / POST_COMMANDS:
  - command: string          # 强制：执行的命令
    cwd: string              # 运行目录：'workspace'(默认) 或 'root'
    run_before_linters: bool # 是否在并行扫描前运行（用于安装插件）
    run_after_linters: bool  # 是否在扫描完成后运行
    continue_if_failed: bool # 失败时是否继续，默认 true
    secured_env: bool        # 是否屏蔽敏感变量，默认 true
    venv: string             # 指定 Python 虚拟环境名称
    output_variables: []     # 需存入全局 ENV 的变量名列表
```

-----

## 6\. 安全与环境变量保护 (Security)

### 6.1 变量屏蔽机制

MegaLinter 默认移除传递给外部 Linter 的敏感环境变量。

  - **添加屏蔽**：配置 `SECURED_ENV_VARIABLES` (支持字符串或正则)。
  - **默认屏蔽列表**：包括 `PAT`, `TOKEN`, `PASSWORD`, `GITHUB_TOKEN` 等。
  - **解除屏蔽**：针对特定 Linter 使用 `(LINTER_KEY)_UNSECURED_ENV_VARIABLES`（仅限字符串）。

-----

## 7\. 运行模式 (CLI Lint Modes)

| 模式 | 描述 | 注意事项 |
| :--- | :--- | :--- |
| `list_of_files` | 一次性调用并传递所有文件列表。 | 性能平衡。 |
| `project` | 从根目录运行，Linter 自行扫描文件。 | **忽略** `FILTER_REGEX_INCLUDE/EXCLUDE`。 |
| `file` | 每个文件调用一次。 | 性能最差。 |

-----

## 8\. 默认风格参考 (Default Styles)

  - **JavaScript**: `standard` (可选 `prettier`)
  - **Python**: `black` (可选 `ruff`)
  - **TypeScript**: `standard` (可选 `prettier`)
  - **Markdown**: `markdownlint` (可选 `remark-lint`, `rumdl`)