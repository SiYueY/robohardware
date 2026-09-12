# robo-hardware Architecture Design v3

状态：已基线化

阶段：Architecture Design

最后更新：2026-09-12

## 1. 目的

本文定义 robo-hardware V1 的 repository layout、component identity、public/private seam、
依赖方向、namespace、include path、CMake target、package、版本和多仓库演进策略。

本文不定义 Realtime、Queue、Buffer、Serial 或 CAN 的具体 C++ interface。
这些 interface 只能在本文评审通过后进入 API Design。

项目章程和 V1 需求规格是本文的上位约束。本文不得扩大 V1 功能范围。

## 2. Architecture Model

### 2.1 三种身份

Architecture 必须区分三个层次：

| 层次 | 身份 | 职责 |
|---|---|---|
| Repository | `robo-hardware` | 承载源码、组合开发、统一 CI 和 V1 发布编排 |
| Distribution component | `realtime`、`serial`、`can` | 可独立提取、构建、测试、安装、发布的源码和 package 单位 |
| Public identity | `<component>` | 调用者长期依赖的 C++ namespace、include、CMake target 和 package 身份 |

本文使用 component 专指独立分发单位。V1 中每个 component 对应一个 production
library，并拥有一个供调用者使用和测试的公开 interface。V1 不设置额外组织层级；
未来只有出现经过验证的多 library、tool 或 plugin 需求时才重新评审。

### 2.2 Component-first

- component identity 独立于 repository topology；
- repository 是开发容器，不是 SDK interface；
- 根构建不是 component 构建的必要条件；
- 每个 component 拥有自己的构建、安装、测试、文档和版本；repository 根统一
  管理 V1 许可证；
- 将 component 移动到独立仓库不得要求消费者修改 include、namespace、target
  或 package 名；
- 独立性必须由 standalone CI 和安装后消费者测试证明。

### 2.3 Public identity belongs to the ecosystem

公共身份属于各自 component，而不是当前 Git repository：

```text
Libraries
|-- realtime
|-- serial
|-- can
`-- canopen (future)
```

repository 不拥有任何 component 的公共 namespace。每个 component 自己拥有并维护其
独立的公共 identity；同一 repository 维护不构成 API 或运行时归属关系。

## 3. Architecture Drivers

Architecture Design 必须满足：

1. `realtime`、`serial`、`can` 是三个独立 distribution component；
2. 每个 component 能够独立提取、配置、构建、测试、安装和消费；
3. V1 production library 之间不存在依赖；
4. 不建立公共 runtime、HAL、device framework 或 transport 抽象；
5. 不建立 `core`、`common` 或 `platform` production library；
6. Linux 系统细节不得无意成为跨 component interface；
7. 测试必须穿过调用者使用的同一 public interface；
8. 行为仿真不得迫使生产代码暴露测试专用 adapter；
9. 根 repository 不生成 umbrella library 或 umbrella install package；
10. 构建配置不得把 warnings、sanitizer 或测试依赖传播给消费者；
11. 初始设计必须适合一名核心维护者长期维护；
12. public identity 在拆分 repository 后保持不变。

## 4. Repository Layout

目标布局：

```text
robo-hardware/
|-- CMakeLists.txt
|-- LICENSE
|-- README.md
|-- docs/
|   |-- README.md
|   |-- project-charter.md
|   |-- v1-requirements.md
|   |-- architecture.md
|   |-- realtime/
|       |-- README.md
|       |-- api.md
|       `-- implementation.md
|   `-- serial/
|       |-- README.md
|       |-- api.md
|       `-- implementation.md
|-- realtime/
|   |-- CMakeLists.txt
|   |-- README.md
|   |-- CHANGELOG.md
|   |-- cmake/
|   |-- docs/
|   |-- include/realtime/
|   |-- src/
|   |-- tests/
|   |   |-- unit/
|   |   |-- integration/
|   |   |-- consumer/
|   |   `-- support/
|   |-- benchmarks/
|   `-- examples/
|-- serial/
|   |-- CMakeLists.txt
|   |-- README.md
|   |-- CHANGELOG.md
|   |-- cmake/
|   |-- docs/
|   |-- include/serial/
|   |-- src/
|   |-- tests/
|   |   |-- unit/
|   |   |-- integration/
|   |   |-- simulators/
|   |   |-- consumer/
|   |   `-- support/
|   `-- examples/
|-- can/
|   |-- CMakeLists.txt
|   |-- README.md
|   |-- CHANGELOG.md
|   |-- cmake/
|   |-- docs/
|   |-- include/can/
|   |-- src/
|   |-- tests/
|   |   |-- unit/
|   |   |-- integration/
|   |   |-- simulators/
|   |   |-- consumer/
|   |   `-- support/
|   `-- examples/
|-- integration/
|   |-- drivers/
|   |-- tests/
|   `-- docs/
`-- tests/
    `-- repository/
```

只在产生实际内容时创建目录，不预先提交空目录。

### 4.1 Root repository responsibilities

根 repository 只负责：

- 库族章程、V1 范围和 Architecture 文档；
- 可选的多 component 开发编排；
- repository-level CI 和 Architecture checks；
- 跨 component 的真实驱动、组合测试和验证记录；
- V1 统一发布的版本清单。

根 repository 不负责：

- 定义任何生产 C++ interface；
- 生成 `robo-hardware` library 或 CMake package；
- 提供 component 构建所必需的私有 CMake helper；
- 安装 umbrella header；
- 成为 component 配置、构建或测试的必要入口。

### 4.2 Component responsibilities

每个 component 的构建、测试和安装内容必须自包含：

- `CMakeLists.txt`：standalone project 和安装导出；
- `README.md`：定位、快速开始、支持范围和状态；
- `CHANGELOG.md`：component interface 变更；
- `cmake/`：自身 package config 模板和必要 helper；
- `docs/`：component interface 契约与设计说明；
- `include/`：公开 interface；
- `src/`：私有 implementation；
- `tests/`：Level 1、Level 2 和安装后 consumer 测试；
- `examples/`：只使用公开 interface 的最小示例。

component 不得通过 `../cmake`、`../include` 或其他相对路径依赖 repository 根。

V1 由 repository 根 `LICENSE` 统一声明许可证，不在 component 目录维护重复副本。
生成独立源码分发包或迁移 component 到独立 repository 时，提取流程必须复制根
许可证，使产物具有完整许可信息。component CMake 不得通过读取 `../LICENSE`
破坏 standalone 构建约束；许可证复制属于源码打包或 repository 迁移流程。

### 4.3 Cross-component integration

资产归属遵循：

| 位置 | 责任 |
|---|---|
| `<component>/tests/` | 验证单个 component 的行为和失败路径 |
| `<component>/tests/consumer/` | 从安装树通过 `find_package` 验证外部消费方式 |
| `<component>/examples/` | 用最小代码说明公共 interface，不承担完整验证 |
| `integration/drivers/` | 同时使用两个或多个 component 的真实驱动验证载体 |
| `integration/tests/` | 验证多 component 的组合行为 |
| `integration/docs/` | 记录组合验证和 Level 3 环境、流程及结果 |

单 component 的 Level 3 流程和报告在实际验证开始时写入其 `docs/validation.md`；
测量程序按用途放入 `tests/` 或 `benchmarks/`。Architecture 不预设独立
`validation/` 目录。

根 `integration/` 中的内容是组件的消费者，不属于任何 production library，也
不得反向成为 component 构建依赖。

### 4.4 Layout rationale

根级 `realtime/`、`serial/`、`can/` 表达它们是 repository 中的独立分发单位，
不是名为 robo-hardware 的 SDK 内部子目录。component 内继续使用 `include/src`
以明确公开 seam 和私有 implementation；component-first 不要求混放 header 和
source。

## 5. Production Library Map And Dependencies

### 5.1 V1 production libraries

```text
realtime
serial
can
```

每个 production library 有一个对外 interface。interface 包括公开类型和函数，也包括线程
所有权、生命周期、错误、顺序、时间和性能契约。

### 5.2 V1 dependency graph

```text
realtime ---> C++ standard library + Linux/POSIX

serial   ---> C++ standard library + Linux tty/ioctl/poll

can      ---> C++ standard library + Linux SocketCAN/socket/poll
```

三个 production library 之间没有依赖。相同的 deadline 和错误处理原则可以形成
一致的 interface，但不能仅为消除少量重复 implementation 而引入公共 library。

### 5.3 Future dependency

CANopen 进入独立里程碑后允许：

```text
canopen ---> can
```

其公共身份预留为：

```text
namespace: canopen
include:   <canopen/...>
target:    canopen::canopen
package:   canopen
```

CANopen 不强制依赖 `realtime`。需要实时执行时，由最终驱动组合二者。
V1 不创建 CANopen 目录、target、package 或占位 implementation。

## 6. Namespace Convention

### 6.1 Public namespaces

```cpp
namespace realtime {}
namespace serial {}
namespace can {}
```

规则：

- component 名称就是其公共 namespace；
- 类型必须位于所属 component namespace；
- 不建立跨 component 的 `core`、`common` 或 `platform` namespace；
- 不使用 inline version namespace，除非未来形成明确 ABI 策略；
- 公共宏使用 `<COMPONENT>_` 前缀；
- 不在公开 header 中使用全局 `using namespace` 或导出 namespace alias。

namespace 表达 component 的独立公共 identity，不表达 repository 物理位置或运行时依赖。

### 6.2 Naming coordination

通用 component 名称存在生态冲突风险。每个 component 的维护者负责在实际发布前完成名称、
package 和发行渠道的可用性核查；该风险不通过 repository 前缀隐藏。

## 7. Include Structure

### 7.1 Public include paths

```cpp
#include <realtime/queue.hpp>
#include <realtime/buffer.hpp>
#include <serial/port.hpp>
#include <can/frame.hpp>
```

公开 header 只能位于：

```text
<component>/include/<component>/
```

每个公开 header 必须：

- 独立可编译；
- 直接包含自身所需依赖；
- 不依赖 include 顺序；
- 不包含其他 V1 component 的 header；
- 尽量不向调用者暴露 Linux UAPI struct；
- 记录 interface 所需的生命周期、错误、线程和性能契约。

### 7.2 Private headers

私有 header 位于 `<component>/src`，不安装，不得被其他 component、示例或
consumer 包含。测试只有在无法从公开 interface 观察的纯 implementation 算法上，
才可以包含私有 header；这类测试不得替代 interface 测试。

### 7.3 Template implementation

V1 的模板组件必须保持 public header 自包含。模板定义保留在声明对应公开类型的
header 中，不预先建立或安装 `detail/`、`internal/`、`common/` 或 `types/` 目录。

非 public、非模板的 implementation 辅助代码应位于 component 源码树内部，不安装、
不进入 public include path，并使用具有明确领域含义的文件和实体名称。未来只有在形成
明确拆分需求后，才可以重新评审必须随模板安装、但不构成受支持 Public Interface 的
implementation header；不得仅为缩短文件或预想复用而增加该 seam。

### 7.4 No umbrella include

V1 不提供：

```cpp
#include <realtime.hpp>
#include <hardware.hpp>
```

是否提供 component 级 convenience header，在 API Design 根据实际 interface
规模决定。它不得跨 component include。

## 8. CMake Architecture

### 8.1 Standalone component projects

每个 component 是可独立配置的 CMake project：

```bash
cmake -S realtime -B build/realtime
cmake --build build/realtime
ctest --test-dir build/realtime
cmake --install build/realtime --prefix <prefix>
```

对应 project 名：

```text
realtime
serial
can
```

component 的 configure、build、test、install 和 package config 生成不得读取根
repository 的 CMake 变量、helper 或生成文件。

### 8.2 Root orchestration

根 `CMakeLists.txt` 可以通过 `add_subdirectory()` 编排被选择的 component，并提供：

```text
REALTIME_BUILD
SERIAL_BUILD
CAN_BUILD
```

根构建不得：

- 修改 component 独立构建时的 public identity；
- 生成额外生产 target；
- 使 component 相互 link；
- 覆盖 component 自己的版本；
- 成为唯一受支持的构建方式。

### 8.3 Production targets

每个 component 的真实 build target：

```text
realtime
serial
can
```

build tree 和 install tree 都必须提供：

```text
realtime::realtime
serial::serial
can::can
```

实现方式应使用普通真实 target、build-tree `ALIAS`，以及安装 export 的
`NAMESPACE <component>::`。真实 target 还必须设置对应的 `EXPORT_NAME`：

```text
realtime -> EXPORT_NAME realtime
serial   -> EXPORT_NAME serial
can      -> EXPORT_NAME can
```

这样安装导入名称才会与 build-tree alias 一致。每个 V1 component 只有一个公开
生产 target。

### 8.4 Library type

生产 target 使用普通 `add_library()` 并尊重 `BUILD_SHARED_LIBS`。V1 不建立分别
命名的 static/shared target，也不把整个 component 强制改为 header-only。

模板 implementation 属于所属 component interface，不形成独立 utility target。
V1 不为假设的 ABI 稳定统一引入 PImpl。

### 8.5 Usage requirements

生产 target 必须：

- `PUBLIC` 声明 C++17 compile feature；
- 只公开自身 include 目录和必要系统链接要求；
- 不传播 warnings、`-Werror`、sanitizer、coverage 或测试选项；
- 不依赖测试 framework；
- 不使用全局 `include_directories()`、`link_libraries()` 或编译 flag；
- 使用 target-scoped CMake 命令。

开发工具必须通过 component 私有 target 或函数应用，且不得安装导出。

### 8.6 CMake baseline

最低 CMake 版本暂定为 3.20。Phase 1 必须分别在三个 standalone component 和
根编排构建中验证选择性构建、GNUInstallDirs、export、package config 和 consumer
测试；验证前不得提高最低版本。

## 9. Package And Installation

### 9.1 CMake package names

每个 component 安装独立、全局可区分的 CMake config package：

```cmake
find_package(realtime CONFIG REQUIRED)
find_package(serial CONFIG REQUIRED)
find_package(can CONFIG REQUIRED)

target_link_libraries(app PRIVATE
  realtime::realtime
  serial::serial
  can::can
)
```

package、namespace、include root 与 target 均使用 component identity。CMake target 通过
`<component>::<component>` 形成其唯一 target scope。

package naming 服从可安装 component 的 distribution identity，并与 component 的 namespace、
include root 保持一致。V1 不提供 repository umbrella package。

### 9.2 Installation layout

```text
<prefix>/include/realtime/
<prefix>/include/serial/
<prefix>/include/can/

<prefix>/<libdir>/librealtime.{a,so}
<prefix>/<libdir>/libserial.{a,so}
<prefix>/<libdir>/libcan.{a,so}

<prefix>/<libdir>/cmake/realtime/
<prefix>/<libdir>/cmake/serial/
<prefix>/<libdir>/cmake/can/
```

`<libdir>` 必须来自 `GNUInstallDirs`。每个 package config 必须：

- 只导入所属 component target；
- 使用所属 component 的版本；
- 不假设根 repository 或其他 component 已安装；
- 支持 relocatable installation；
- 在缺少必要系统依赖时给出清晰错误。

未来系统 package 可以命名为：

```text
realtime-dev
serial-dev
can-dev
```

V1 只保证安装结构允许拆分，不要求立即发布 Debian/RPM package。

## 10. Versioning Strategy

### 10.1 Component owns its version

每个 component 从第一天起在自己的 `project(... VERSION ...)` 和 package version
file 中拥有版本。component standalone build 不读取根 repository 版本。

### 10.2 V1 coordinated releases

V1 阶段为了降低单人维护成本，三个 component 使用相同版本号并协调发布。根
repository tag 可以使用：

```text
v0.1.0
v0.2.0
```

协调版本是发布策略，不是 Architecture 依赖。根 CI 必须检查 V1 coordinated
release 中三个 component 声明的版本一致。

### 10.3 Future independent releases

当 component 的发布节奏、兼容承诺或维护者明显分化时，可以转为：

```text
realtime-v0.4.0
serial-v0.3.0
can-v0.2.0
```

根 repository release manifest 记录经过组合验证的 component 版本元组。转为独立
版本不得修改 namespace、include path、CMake target 或 package 名。

每个 component 独立遵循：

- `0.x` 期间允许依据真实使用反馈调整 interface；
- 破坏性修改记录在自己的 changelog；
- `1.0` 后首先承诺源码兼容；
- ABI 稳定需由该 component 单独决策。

## 11. Test Architecture

### 11.1 Interface is the test surface

Level 1 和 Level 2 测试默认通过 production library 的公开 interface 验证可观察行为。
implementation 重构而 interface 行为不变时，这些测试不应修改。

### 11.2 OS seam

Serial 和 CAN 只有 Linux production implementation，因此 V1 不创建公开 adapter
interface。为测试添加 `ISerialBackend`、`ICanBackend` 或 `ITransport` 会制造只有
一个 production adapter 的假 seam。

```text
Serial interface ---> Linux tty implementation ---> PTY ---> firmware simulator

CAN interface    ---> SocketCAN implementation  ---> vcan --> CAN node simulator
```

必要的 syscall seam 可以作为 component 私有 implementation 存在，但不得泄漏到公开
interface。

### 11.3 Component-local tests

每个 component 自身必须验证：

1. standalone configure、build 和 test；
2. 公开 header 自包含；
3. 安装后 `find_package(<component>)` 成功；
4. `<component>::<component>` target 可链接；
5. 不需要根 repository 或其他 component；
6. static/shared 构建在支持配置中均可消费；
7. 私有 include、测试 target 和开发 flag 不泄漏到安装 export。

### 11.4 Repository-level checks

根 `tests/repository` 验证：

- component 不读取根私有文件；
- V1 component 之间没有 include 或 target link；
- 根构建不生成 umbrella production target/package；
- component public identity 在 standalone 和 orchestrated build 中一致；
- coordinated release 的 component 版本一致；
- 每个 component 目录能够被独立提取后构建和测试。

## 12. Multi-repository Evolution

### 12.1 Extraction invariants

component 迁移到独立 repository 时必须保持：

- C++ namespace 不变；
- include path 不变；
- CMake target 不变；
- CMake package 名不变；
- library output name 不变；
- interface 和兼容承诺不因移动而改变。

### 12.2 Preconditions for extraction

component 在拆分前必须已经：

- standalone configure/build/test/install；
- 不引用 repository 根相对路径；
- 具备 README、changelog、docs 和 package config，并在提取流程中获得根许可证
  的副本；
- 拥有自己的版本；
- 拥有可独立运行的 consumer test；
- 将跨 component 组合测试留在 portfolio repository。

### 12.3 After extraction

拆分后，robo-hardware repository 可以选择：

- 仅保留 portfolio 文档和兼容版本 manifest；
- 在开发编排中固定获取各 component 的已知兼容版本；
- 将跨 component 驱动与组合验证继续保留在 `integration/`。

是否使用 Git submodule、FetchContent、package manager 或 CI checkout 是拆分时的
开发编排决策，不进入 component public interface。

## 13. Architecture Decisions

| 决策 | Architecture v2 选择 | 拒绝方案与原因 |
|---|---|---|
| Repository role | portfolio 与开发编排容器 | SDK 产品模型混淆 repository 与 component identity |
| Source layout | 根级 `realtime/serial/can` | `modules/` 表达 SDK 内部子目录而非独立分发单位 |
| Component build | 每个目录是 standalone CMake project | 仅根项目选择性构建使根成为必要条件 |
| Public identity | `<component>` | repository/brand 前缀会把独立基础库绑定为内部 SDK component |
| Include | `<component>/...` | repository-first 路径 |
| CMake target | `<component>::<component>` | 无 namespace 或 repository-first target |
| CMake package | `<component>` | umbrella package 偏 SDK |
| Version ownership | component 自己持有版本，V1 协调一致 | 根版本作为 component 构建依赖 |
| Future release | 允许独立 component version/tag | 永久 lockstep 限制独立演进 |
| Public headers | component-local include tree | 根共享 include tree 弱化 locality 和可提取性 |
| Test seam | 公开 interface + PTY/vcan simulator | 公开 mock backend 制造假 seam |
| Library type | 尊重 `BUILD_SHARED_LIBS` | 双 target 和全 header-only 增加维护成本 |
| Common code | V1 不建立公共 production library | 尚无经验证的共享复杂性 |
| CANopen | V1 无目录、target、package 或占位实现 | 占位 interface 提前冻结错误设计 |

## 14. Consequences And Risks

### 14.1 Standalone build duplication

三个 component 会重复少量 CMake 安装、warnings 和测试编排逻辑。这是保证可独立
提取所接受的成本。根 repository 不得通过共享私有 helper 消除这种重复；只有形成
可独立版本化的公开 CMake helper package 后才能重新评审。

### 14.2 Component-name governance

裸 component identity 提供最短的独立消费路径，但不消除名称冲突风险。每个 component
在独立发布前必须审查 namespace、package、包管理器和发行渠道的名称可用性。

### 14.3 Coordinated to independent versions

从统一版本转为独立版本会增加兼容组合矩阵。只有真实发布节奏分化后才执行；在此
之前通过 coordinated releases 降低维护成本。

### 14.4 Local implementation duplication

各 component 可能分别实现少量 fd 生命周期、poll/deadline 和 errno 映射。
Architecture 接受这种局部重复以保持独立 seam。不得因几段相似代码提前创建
公共 production library。

### 14.5 Source extraction is supported, not informal copying

Architecture 支持将完整 component 目录作为有版本的源码单位提取或 vendoring，
不鼓励复制单个 header/source 后失去版本、许可证和验证上下文。

## 15. Architecture Definition Of Done

Architecture Design v2 在以下条件满足后可以进入 API Design：

1. root-level component layout 被接受；
2. standalone component CMake 模型被接受；
3. `<component>` public identity 被接受；
4. include、target、package 和 library naming 被接受；
5. component-owned、V1 coordinated versioning 被接受；
6. public/private header seam 被接受；
7. component-local 测试、文档和 Level 3 记录策略被接受；
8. multi-repository extraction invariants 被接受；
9. 开放问题已明确记录，不通过 implementation 静默决定。

## 16. API Design 前仍需解决的问题

Architecture 接受后，API Design 按以下顺序进行：

1. 跨 component 的错误和 deadline 语义词汇，但不建立共享类型；
2. Realtime 执行基础 interface；
3. `Queue<T, Capacity>` 与 `Buffer<T>` contract；
4. Serial interface；
5. CAN frame model 和 SocketCAN interface。

以下问题保留给对应 API Design：

- open 操作采用 factory、显式状态对象还是其他形式；
- 编程错误和初始化错误是否允许异常；
- deadline 的公开表达形式；
- native handle access 是否存在；
- Queue/Buffer 类型约束、初始化和内存顺序；
- Serial 部分传输结果；
- Classical CAN/CAN FD frame 类型关系；
- 接收软件时间戳的 clock domain 和表示。
