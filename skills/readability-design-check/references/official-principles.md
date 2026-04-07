# Official Principles

本文件汇总了适合“周期性可读性与设计检查”的官方原则。优先使用仓库本地规范；当本地规范不足时，再用这里的规则做基线。

## 跨语言基线

- 优先改善整体 code health，而不是追求一次性完美。来源：Google Engineering Practices, The Standard of Code Review
  - https://google.github.io/eng-practices/review/reviewer/standard.html
- 检查设计、功能、复杂度、测试、命名、注释、风格和文档；复杂度的判断标准之一是“新读者能否快速理解”。来源：Google Engineering Practices, What to look for in a code review
  - https://google.github.io/eng-practices/review/reviewer/looking-for.html
- 如果已经看到重大设计问题，应尽早指出，不要等把所有细节都看完。来源：Google Engineering Practices, Navigating a CL in review
  - https://google.github.io/eng-practices/review/reviewer/navigate.html

## C 和 C++ 基线

- 优先为读者而不是作者优化；保持与现有代码一致；避免让普通维护者觉得诡异或难维护的构造。来源：Google C++ Style Guide
  - https://google.github.io/styleguide/cppguide.html
- 名称应让新读者理解用途；变量应尽量放在最小作用域；靠近首次使用处声明。来源：Google C++ Style Guide
  - https://google.github.io/styleguide/cppguide.html
- 把“有意义的动作”包装成命名良好的函数；保持函数短而简单；把杂乱结构封装起来，不要扩散。来源：C++ Core Guidelines
  - https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines?lang=en
- 把相关数据组织为结构，显式表达接口和实现边界，减少暴露面。来源：C++ Core Guidelines
  - https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines?lang=en
- 注释应说明代码试图做什么以及为什么，避免微观层面复述“怎么做”；公共接口和边界情况需要足够说明。来源：LLVM Coding Standards
  - https://llvm.org/docs/CodingStandards.html
- 扩展现有代码时先保持局部风格一致，不要顺手做大范围重排。来源：LLVM Coding Standards
  - https://llvm.org/docs/CodingStandards.html

## Python 基线

- `Readability counts.` 与 `If the implementation is hard to explain, it's a bad idea.` 可作为复杂度告警信号。来源：PEP 20
  - https://peps.python.org/pep-0020/
- 项目内一致性高于通用风格，一致性不足时再回退到风格指南；代码被阅读的频率高于被编写。来源：PEP 8
  - https://peps.python.org/pep-0008/
- 函数和变量名应使用易读命名；注释必须保持更新，内联注释应少而精，只在非显然处解释原因。来源：PEP 8
  - https://peps.python.org/pep-0008/

## 用法建议

- 若仓库内已有 `.clang-format`、`pyproject.toml`、项目 style guide、贡献指南或既有目录模式，先遵循本地约定。
- 若你面对的是设计决策而非格式问题，优先使用“是否更容易理解、维护、测试、演进”作为判断标准。
- 若一个改动让解释成本持续升高，即使暂时功能正确，也应尽早做小步整理或向用户提出设计预警。
