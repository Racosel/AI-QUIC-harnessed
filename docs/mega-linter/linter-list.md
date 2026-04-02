# MegaLinter Linter List (v9.4.0)
为了方便在 `.megalinter.yml` 配置文件中配置，表格保留了 **编程语言/格式**、**工具名称** 以及 **配置关键字 (Key)**。

### 使用提示
1.  **启用特定 Linter**: 在配置文件中使用 `ENABLE_LINTERS` 加上表格中的 Key。
2.  **禁用特定 Linter**: 使用 `DISABLE` 加 Key。
3.  **自动修复**: 如果工具支持修复，运行 `mega-linter-runner --fix` 会自动调用这些工具的修复模式（如 `black`, `prettier`, `ruff` 等）。
---

## 1. 编程语言类 (Languages)
这是最核心的部分，涵盖了从主流到小众的各类编程语言。

| 语言 (Language) | 工具 (Linter) | 配置关键字 (Key) |
| :--- | :--- | :--- |
| **BASH** | bash-exec | `BASH_EXEC` |
| **BASH** | shellcheck | `BASH_SHELLCHECK` |
| **BASH** | shfmt | `BASH_SHFMT` |
| **C** | cppcheck | `C_CPPCHECK` |
| **C** | cpplint | `C_CPPLINT` |
| **C** | clang-format | `C_CLANG_FORMAT` |
| **CLOJURE** | clj-kondo | `CLOJURE_CLJ_KONDO` |
| **CLOJURE** | cljstyle | `CLOJURE_CLJSTYLE` |
| **COFFEE** | coffeelint | `COFFEE_COFFEELINT` |
| **C++ (CPP)** | cppcheck | `CPP_CPPCHECK` |
| **C++ (CPP)** | cpplint | `CPP_CPPLINT` |
| **C++ (CPP)** | clang-format | `CPP_CLANG_FORMAT` |
| **C# (CSHARP)** | dotnet-format | `CSHARP_DOTNET_FORMAT` |
| **C# (CSHARP)** | csharpier | `CSHARP_CSHARPIER` |
| **C# (CSHARP)** | roslynator | `CSHARP_ROSLYNATOR` |
| **DART** | dartanalyzer | `DART_DARTANALYZER` |
| **GO** | golangci-lint | `GO_GOLANGCI_LINT` |
| **GO** | revive | `GO_REVIVE` |
| **GROOVY** | npm-groovy-lint | `GROOVY_NPM_GROOVY_LINT` |
| **JAVA** | checkstyle | `JAVA_CHECKSTYLE` |
| **JAVA** | pmd | `JAVA_PMD` |
| **JAVASCRIPT** | eslint | `JAVASCRIPT_ES` |
| **JAVASCRIPT** | standard | `JAVASCRIPT_STANDARD` |
| **JAVASCRIPT** | prettier | `JAVASCRIPT_PRETTIER` |
| **JSX** | eslint | `JSX_ESLINT` |
| **KOTLIN** | ktlint | `KOTLIN_KTLINT` |
| **KOTLIN** | detekt | `KOTLIN_DETEKT` |
| **LUA** | luacheck | `LUA_LUACHECK` |
| **LUA** | selene | `LUA_SELENE` |
| **LUA** | stylua | `LUA_STYLU` |
| **MAKEFILE** | checkmake | `MAKEFILE_CHECKMAKE` |
| **PERL** | perlcritic | `PERL_PERLCRITIC` |
| **PHP** | phpcs | `PHP_PHPCS` |
| **PHP** | phpstan | `PHP_PHPSTAN` |
| **PHP** | psalm | `PHP_PSALM` |
| **PHP** | phplint | `PHP_PHPLINT` |
| **PHP** | php-cs-fixer | `PHP_PHPCSFIXER` |
| **POWERSHELL** | powershell | `POWERSHELL_POWERSHELL` |
| **POWERSHELL** | powershell_formatter | `POWERSHELL_POWERSHELL_FORMATTER` |
| **PYTHON** | pylint | `PYTHON_PYLINT` |
| **PYTHON** | black | `PYTHON_BLACK` |
| **PYTHON** | flake8 | `PYTHON_FLAKE8` |
| **PYTHON** | isort | `PYTHON_ISORT` |
| **PYTHON** | bandit | `PYTHON_BANDIT` |
| **PYTHON** | mypy | `PYTHON_MYPY` |
| **PYTHON** | nbqa | `PYTHON_NBQA_MYPY` |
| **PYTHON** | pyright | `PYTHON_PYRIGHT` |
| **PYTHON** | ruff | `PYTHON_RUFF` |
| **PYTHON** | ruff-format | `PYTHON_RUFF_FORMAT` |
| **R** | lintr | `R_LINTR` |
| **RAKU** | raku | `RAKU_RAKU` |
| **RUBY** | rubocop | `RUBY_RUBOCOP` |
| **RUST** | clippy | `RUST_CLIPPY` |
| **SALESFORCE** | code-analyzer-apex | `SALESFORCE_CODE_ANALYZER_APEX` |
| **SALESFORCE** | code-analyzer-aura | `SALESFORCE_CODE_ANALYZER_AURA` |
| **SALESFORCE** | code-analyzer-lwc | `SALESFORCE_CODE_ANALYZER_LWC` |
| **SALESFORCE** | sfdx-scanner-apex | `SALESFORCE_SFDX_SCANNER_APEX` |
| **SALESFORCE** | sfdx-scanner-aura | `SALESFORCE_SFDX_SCANNER_AURA` |
| **SALESFORCE** | sfdx-scanner-lwc | `SALESFORCE_SFDX_SCANNER_LWC` |
| **SALESFORCE** | lightning-flow-scanner | `SALESFORCE_LIGHTNING_FLOW_SCANNER` |
| **SCALA** | scalafix | `SCALA_SCALAFIX` |
| **SQL** | sqlfluff | `SQL_SQLFLUFF` |
| **SQL** | tsqllint | `SQL_TSQLLINT` |
| **SWIFT** | swiftlint | `SWIFT_SWIFTLINT` |
| **TSX** | eslint | `TSX_ESLINT` |
| **TYPESCRIPT** | eslint | `TYPESCRIPT_ES` |
| **TYPESCRIPT** | ts-standard | `TYPESCRIPT_STANDARD` |
| **TYPESCRIPT** | prettier | `TYPESCRIPT_PRETTIER` |
| **VB.NET** | dotnet-format | `VBDOTNET_DOTNET_FORMAT` |

---

## 2. 文件格式类 (Formats)
针对配置文件、文档和结构化数据的检查。

| 格式 (Format) | 工具 (Linter) | 配置关键字 (Key) |
| :--- | :--- | :--- |
| **CSS** | stylelint | `CSS_STYLELINT` |
| **ENV** | dotenv-linter | `ENV_DOTENV_LINTER` |
| **GRAPHQL** | graphql-schema-linter | `GRAPHQL_GRAPHQL_SCHEMA_LINTER` |
| **HTML** | djlint | `HTML_DJLINT` |
| **HTML** | htmlhint | `HTML_HTMLHINT` |
| **JSON** | jsonlint | `JSON_JSONLINT` |
| **JSON** | eslint-plugin-jsonc | `JSON_ESLINT_PLUGIN_JSONC` |
| **JSON** | v8r | `JSON_V8R` |
| **JSON** | prettier | `JSON_PRETTIER` |
| **JSON** | npm-package-json-lint | `JSON_NPM_PACKAGE_JSON_LINT` |
| **LATEX** | chktex | `LATEX_CHKTEX` |
| **MARKDOWN** | markdownlint | `MARKDOWN_MARKDOWNLINT` |
| **MARKDOWN** | remark-lint | `MARKDOWN_REMARK_LINT` |
| **MARKDOWN** | markdown-table-formatter | `MARKDOWN_MARKDOWN_TABLE_FORMATTER` |
| **MARKDOWN** | rumdl | `MARKDOWN_RUMDL` |
| **PROTOBUF** | protolint | `PROTOBUF_PROTOLINT` |
| **RST** | rst-lint | `RST_RST_LINT` |
| **RST** | rstcheck | `RST_RSTCHECK` |
| **RST** | rstfmt | `RST_RSTFMT` |
| **XML** | xmllint | `XML_XMLLINT` |
| **YAML** | prettier | `YAML_PRETTIER` |
| **YAML** | yamllint | `YAML_YAMLLINT` |
| **YAML** | v8r | `YAML_V8R` |

---

## 3. 工具与基础设施类 (Tooling Formats)
针对 DevOps、云原生及各类自动化工具的配置检查。

| 工具格式 (Tooling) | 工具 (Linter) | 配置关键字 (Key) |
| :--- | :--- | :--- |
| **ACTION** | actionlint | `ACTION_ACTIONLINT` |
| **ANSIBLE** | ansible-lint | `ANSIBLE_ANSIBLE_LINT` |
| **API** | spectral | `API_SPECTRAL` |
| **ARM** | arm-ttk | `ARM_ARM_TTK` |
| **BICEP** | bicep_linter | `BICEP_BICEP_LINTER` |
| **CLOUDFORMATION** | cfn-lint | `CLOUDFORMATION_CFN_LINT` |
| **DOCKERFILE** | hadolint | `DOCKERFILE_HADOLINT` |
| **EDITORCONFIG** | editorconfig-checker | `EDITORCONFIG_EDITORCONFIG_CHECKER` |
| **GHERKIN** | gherkin-lint | `GHERKIN_GHERKIN_LINT` |
| **KUBERNETES** | kubeconform | `KUBERNETES_KUBECONFORM` |
| **KUBERNETES** | helm | `KUBERNETES_HELM` |
| **KUBERNETES** | kubescape | `KUBERNETES_KUBESCAPE` |
| **PUPPET** | puppet-lint | `PUPPET_PUPPET_LINT` |
| **ROBOTFRAMEWORK** | robocop | `ROBOTFRAMEWORK_ROBOCOP` |
| **SNAKEMAKE** | snakemake | `SNAKEMAKE_LINT` |
| **SNAKEMAKE** | snakefmt | `SNAKEMAKE_SNAKEFMT` |
| **TEKTON** | tekton-lint | `TEKTON_TEKTON_LINT` |
| **TERRAFORM** | tflint | `TERRAFORM_TFLINT` |
| **TERRAFORM** | terrascan | `TERRAFORM_TERRASCAN` |
| **TERRAFORM** | terragrunt | `TERRAFORM_TERRAGRUNT` |
| **TERRAFORM** | terraform-fmt | `TERRAFORM_TERRAFORM_FMT` |

---

## 4. 仓库质量与安全检查 (Other)
侧重于安全扫描、拼写检查和代码重复度。

| 类别 (Category) | 工具 (Linter) | 配置关键字 (Key) |
| :--- | :--- | :--- |
| **COPYPASTE** | jscpd | `COPYPASTE_JSCPD` |
| **REPOSITORY** | checkov | `REPOSITORY_CHECKOV` |
| **REPOSITORY** | devskim | `REPOSITORY_DEVSKIM` |
| **REPOSITORY** | dustilock | `REPOSITORY_DUSTILOCK` |
| **REPOSITORY** | git_diff | `REPOSITORY_GIT_DIFF` |
| **REPOSITORY** | gitleaks | `REPOSITORY_GITLEAKS` |
| **REPOSITORY** | grype | `REPOSITORY_GRYPE` |
| **REPOSITORY** | kics | `REPOSITORY_KICS` |
| **REPOSITORY** | ls-lint | `REPOSITORY_LS_LINT` |
| **REPOSITORY** | secretlint | `REPOSITORY_SECRETLINT` |
| **REPOSITORY** | semgrep | `REPOSITORY_SEMGREP` |
| **REPOSITORY** | syft | `REPOSITORY_SYFT` |
| **REPOSITORY** | trivy | `REPOSITORY_TRIVY` |
| **REPOSITORY** | trivy-sbom | `REPOSITORY_TRIVY_SBOM` |
| **REPOSITORY** | trufflehog | `REPOSITORY_TRUFFLEHOG` |
| **REPOSITORY** | kingfisher | `REPOSITORY_KINGFISHER` |
| **SPELL** | cspell | `SPELL_CSPELL` |
| **SPELL** | proselint | `SPELL_PROSELINT` |
| **SPELL** | vale | `SPELL_VALE` |
| **SPELL** | lychee | `SPELL_LYCHEE` |
| **SPELL** | codespell | `SPELL_CODESPELL` |

---

