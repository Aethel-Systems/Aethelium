# Aethelium 语言参考手册

**版本**: 1.0 (Bootstrap Core)  
**适用对象**: 系统架构师、内核开发者、裸机编程人员

## 1. 引言与设计哲学 (Paradigm Purge)

Aethelium 是一门为现代硬件而生的系统级语言。为了消除 Unix 时代的遗留包袱，我们执行了严格的“范式清洗”：

1.  **拒绝下划线**：标识符禁止使用 `_`（蛇形命名法），推荐使用 `/` 进行逻辑分层。
2.  **符号重定义**：`/` 被归还给标识符命名空间，除法运算回归其数学本源 `÷`。
3.  **层级访问**：成员访问与枚举使用 `\`（反斜杠），以此区别于命名空间路径。
4.  **无运行时**：没有 `main` 函数，只有 `@entry` 入口点；没有 libc，只有直接的硬件指令。

---

## 2. 词法结构

### 2.1 标识符 (Identifiers)
标识符用于变量名、函数名、类型名。
*   **允许**：字母、数字、`/` (正斜杠)。
*   **禁止**：`_` (下划线)、以数字开头。
*   **风格**：Aethelium 推荐使用 **Path-Case** 命名法。

```aethelium
// 正确
var system/cpu/count: UInt64
var pci/bus/id: UInt8

// 错误
var system_cpu_count: UInt64  // 禁止下划线
var 2nd/bus: UInt8            // 禁止数字开头
```

### 2.2 运算符变更
由于 `/` 用于标识符，数学运算符发生了以下变更：

| 运算 | 符号 | 示例 | 说明 |
| :--- | :--- | :--- | :--- |
| 除法 | `÷` | `a = 100 ÷ 4` | 必须使用 UTF-8 字符 `0xC3 0xB7` |
| 成员访问 | `\` | `ctx\rip` | 访问结构体成员或对象属性 |
| 枚举引用 | `\` | `\Success` | 引用枚举值或状态 |
| 路径分隔 | `/` | `sys/io/port` | **属于标识符名称的一部分** |

### 2.3 字面量
*   **整数**: `123`, `0xFF`, `0b1010`。
*   **字符串**: `"Hello World"` (UTF-8)。
*   **布尔值**: `true`, `false`。
*   **空值**: `nil`。

---

## 3. 类型系统 (Type System)

Aethelium 采用强类型系统，所有类型必须显式声明。

### 3.1 基础类型
*   `UInt8`, `UInt16`, `UInt32`, `UInt64` (无符号整数)
*   `Int8`, `Int16`, `Int32`, `Int64` (有符号整数)
*   `Float`, `Double` (浮点数)
*   `Bool` (布尔)
*   `Void` (空类型)

### 3.2 硬件强类型
为了防止内核开发中的地址混淆，Aethelium 引入了地址强类型：
*   `PhysAddr`: 物理地址（不可直接解引用，需映射）。
*   `VirtAddr`: 虚拟地址。

### 3.3 指针与视图
*   **指针**: `ptr<T>`。表示指向类型 T 的内存地址。
*   **视图 (Zero-Copy View)**: `view<T>`。
    *   `view<T>` 是 Aethelium 的核心特性，它将一段裸内存区域“解释”为类型 T，而不发生数据拷贝。
    *   常用于解析网络包头、ACPI 表、文件系统元数据。

```aethelium
// 将原始字节指针转换为 ACPI 表头视图
let raw/ptr: ptr<UInt8> = ...
let acpi/table = raw/ptr as view<ACPI/Header>

// 直接读取，无需 memcpy
if acpi/table\signature == 0x54445352 { ... }
```

### 3.4 硬件绑定类型
*   `reg<"name", T>`: 物理寄存器绑定。将变量直接映射到 CPU 寄存器。
*   `port<T>`: I/O 端口抽象。
*   `vector<T, N>`: SIMD 向量类型（如 `vector<UInt8, 32>` 对应 YMM）。

---

## 4. 声明与定义

### 4.1 变量
使用 `var` (可变) 和 `let` (不可变)。

```aethelium
// 变量声明语法：var 名称: 类型 = 值
var counter: UInt64 = 0
let max/limit: UInt64 = 100

// 物理寄存器绑定 (仅在 hardware 块中有效)
var rax: reg<"rax", UInt64>
rax = 1  // 生成: mov rax, 1
```

### 4.2 结构体 (Struct)
支持 `@packed` 和 `@aligned(n)` 装饰器。

```aethelium
@packed
struct EFI/SystemTable {
    header: EFI/TableHeader
    firmware/vendor: ptr<UInt16>
    firmware/revision: UInt32
    // ...
}
```

### 4.3 函数 (Func)
函数定义使用 `func` 关键字。返回值类型使用 `:` 指定。

```aethelium
// 语法: func 名称(参数: 类型): 返回类型
func kernel/init(boot/info: ptr<BootInfo>): Void {
    // ...
}
```

### 4.4 装饰器 (Decorators)
*   `@entry`: 标记程序的入口点（替代 main）。
*   `@gate(type: ...)`: 定义特殊的硬件门函数（中断、异常、系统调用）。
    *   `@gate(type: \interrupt)`: 自动生成 `iretq` 返回。
    *   `@gate(type: \syscall)`: 自动处理 `sysretq`。
    *   `@gate(type: \efi)`: 遵循 Microsoft x64 ABI (用于 UEFI)。
    *   `@gate(type: \naked)`: 不生成序言和尾声。

---

## 5. 控制流 (Control Flow)

### 5.1 条件与循环
Aethelium 摒弃了 C 风格的 `for(;;)`，采用更现代的迭代器风格。

```aethelium
if x > 10 {
    // ...
} else {
    // ...
}

// 无限循环 (替代 while(true))
loop {
    if status\ready { break }
}

// 条件循环
while x < 100 {
    // ...
}
```

### 5.2 模式匹配 (Match)
用于替代 switch，支持枚举和数值。

```aethelium
match port\status {
    case \Ready => { handle/ready() }
    case \Error => { handle/error() }
    default => { wait() }
}
```

### 5.3 形态置换 (Morph)
**硬件层独有特性**。`morph` 块表示控制流的终结与上下文的彻底切换。

```aethelium
hardware func context/switch(next: ptr<Thread>) {
    // ... 保存当前状态 ...
    
    // 执行形态置换：加载新栈、切换页表、跳转指令指针
    morph {
        reg<"rsp"> = next\rsp
        reg<"cr3"> = next\cr3
        goto next\rip
    }
    // 此处代码永远不会执行
}
```

---

## 6. 系统层级块 (System Blocks)

Aethelium 将代码分为不同的抽象层级，通过特定的块语法隔离。

### 6.1 `hardware { ... }`
硬件层代码块。在此块中可以：
*   绑定物理寄存器 (`reg<...>`)。
*   执行 ISA 直通指令 (`hardware\isa\...`)。
*   操作 I/O 端口。
*   访问控制寄存器 (`CPU/Current\Control\CR3` 等)。

```aethelium
hardware {
    var cr0: UInt64 = CPU/Current\Control\CR0
    hardware\isa\cli()
    // ...
}
```

### 6.2 `silicon { ... }`
硅基语义块。用于微架构级别的调优和配置。
*   流水线控制。
*   缓存管理。
*   预取提示。

```aethelium
silicon {
    // 配置 MSR (Model Specific Register)
    using CPU/Current {
        MSR/EFER\Syscall/Enable = true
    }
    
    // 缓存行冲刷
    silicon\cache\flush(addr)
}
```

### 6.3 `metal { ... }`
裸金属逻辑块。允许直接的内存指针操作和不安全的类型转换（如 `as view<T>`）。

```aethelium
metal {
    let ptr = 0xB8000 as ptr<UInt16> // VGA 显存
    *ptr = 0x0F41  // 打印白色 'A'
}
```

---

## 7. 模块化与导入

Aethelium 目前使用编译规则自动导入。

*   aethelc [源文件1] [源文件2] -o [输出]

**注意**：导入路径同样遵循 `/` 分隔符规则。

---

## 8. 附录：常用运算符速查表

| 运算符 | 描述 | C 语言对应 | 备注 |
| :--- | :--- | :--- | :--- |
| `÷` | 除法 | `/` | **必须使用 `÷`，`/` 是标识符字符** |
| `\` | 成员访问 | `.` 或 `->` | 用于结构体成员、枚举值 |
| `as` | 类型转换 | `(Type)` | `val as UInt8` |
| `^` | 按位异或 | `^` | |
| `&` | 按位与 | `&` | |
| `|` | 按位或 | `|` |
| `<<` | 左移 | `<<` | |
| `>>` | 右移 | `>>` | |

---

*有关各条指令对应的具体机器码生成规则，请参阅 `docs/硬件层手册_机器码对照版_2026-03-06.md`。*