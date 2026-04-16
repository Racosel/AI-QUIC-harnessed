1. 以总结性文件驱动：由参考源代码转为参考设计理念。
   选取了lsquic、xquic、mvfst三个基于C/C++代码简洁的quic实现，由Agent的plan模式直接阅读整个仓库，并分析其设计（模块划分、RFC中未提及的设计细节、测试方法等），生成供编写代码时参考的技术报告。
   [lsquic](docs/refs/lsquic.md)
   [mvfst](docs/refs/mvfst.md)
   [xquic](docs/refs/xquic.md)

2. 调整实现顺序：先实现收发能力
   将`transfer`置于`chacha20`之前，吸取上次失败的教训，先保证收发相关数据结构和基础流控功能的可用性。
   4/15晚：`transfer`用例可概率性通过，2M、3M文件均可传输完成，5M文件的尾部无法收齐，与ACK确认、PTO等相关（16日上午仍在排查）

3. skills：索引与调用
   引入[skill-index](skills/skill-index)作为所有skill的入口，根据当前任务选择适用的skill。
   仍存在问题：`log`更新时断时续，有时需要人工触发，推测其他skills也有可能不参与工作。尝试强制每次任务都调用skill-index，并在输出中加入对已调用skills的显示。

4. codex计费规则

