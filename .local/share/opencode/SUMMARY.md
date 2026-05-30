# C-DSL 规则引擎 — 重构完成

## 新目录结构

```
include/cdsl/
├── cdsl.h           # 总括头文件
├── ai.h             # AI 桥接层
├── ast.h            # AST 类型定义
├── cache.h          # 编译缓存
├── codegen.h        # C 代码生成
├── context.h        # 执行上下文
├── execution.h      # 便利头文件（包含全部 7 个）
├── report.h         # 规则执行报告
├── ruleset.h        # 规则集批处理
├── schema.h         # 模式注册与验证
├── visual.h         # Graphviz 可视化
├── vm.h             # 虚拟机生命周期
└── util/
    ├── arena.h      # 内存池分配器
    ├── error.h      # 结构化错误报告
    ├── hashmap.h    # O(1) 哈希表
    └── json.h       # 零依赖 JSON 解析器

src/
├── ai/bridge.c
├── ast/{ast.c,parse.c,template.c}
├── schema/schema.c
├── util/{arena.c,error.c,hashmap.c,json.c}
└── vm/{builtins.c,cache.c,codegen.c,context.c,eval.c,internal.h,ruleset.c,visualize.c}
```

## 关键变更
- **C23**：`bool` 返回值、`[[nodiscard]]`、`constexpr`、`static_assert`
- **单头文件** → **每个模块一个头文件**：用户仅需 `#include "cdsl/context.h"`
- **`execution.h`** 仍作为包含全部 7 个子模块的便利头文件
- 无循环依赖，无前向声明问题
