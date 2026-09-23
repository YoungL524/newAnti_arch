# 别再把 DDS 当成高级 Socket

## 从一次 Fast DDS 排障，到 AFSIM 外部数据总线的架构重构

![图片](https://mmbiz.qpic.cn/mmbiz_png/ibsc9IrBO0UQNWVxF2LYABpX2U8c6xzlDcb9EI2lZh52rPCjC5yXxyI4lKfztVnZXZ67arvQCGnVhv5coHerpopOB8AptOy7NFrvvk9iaPhico/640?wx_fmt=png&from=appmsg&watermark=1&tp=webp&wxfrom=5&wx_lazy=1#imgIndex=0)

如果你第一次看到 DDS，大概率会产生一种非常强烈的感觉：

这东西是不是把“发个 UDP”故意搞复杂了？

因为如果只是想从进程 A 给进程 B 发一组数据，Socket 世界里可能只需要：

```
socket();
```

结束。

但到了 DDS，你突然会看到一大串东西：

```
DomainParticipant
```

第一次接触时，几乎所有 C++ 开发者都会有同一个疑问：

> 我只是想发个结构体，为什么要创建这么多对象？

如果只站在“一次消息发送”的视角看，DDS 确实显得复杂。

但如果把系统放大十倍、百倍，你会发现真正的问题恰恰相反：

> DDS 不是把简单问题复杂化了，而是把原本散落在整个系统里的复杂性，提前收敛到了一个中间件模型中。

这篇文章，就从一个非常小、但已经可以真实运行的 Fast DDS 工程开始。

工程只做一件事：

Publisher 发布平台遥测信息：

```
platform_id = afsim-air-1
```

Subscriber 自动发现 Publisher，然后接收数据。

表面上只是：

```
Publisher
```

![图片](data:image/svg+xml,%3C%3Fxml version='1.0' encoding='UTF-8'%3F%3E%3Csvg width='1px' height='1px' viewBox='0 0 1 1' version='1.1' xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'%3E%3Ctitle%3E%3C/title%3E%3Cg stroke='none' stroke-width='1' fill='none' fill-rule='evenodd' fill-opacity='0'%3E%3Cg transform='translate(-249.000000, -126.000000)' fill='%23FFFFFF'%3E%3Crect x='249' y='126' width='1' height='1'%3E%3C/rect%3E%3C/g%3E%3C/g%3E%3C/svg%3E)

![图片](data:image/svg+xml,%3C%3Fxml version='1.0' encoding='UTF-8'%3F%3E%3Csvg width='1px' height='1px' viewBox='0 0 1 1' version='1.1' xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'%3E%3Ctitle%3E%3C/title%3E%3Cg stroke='none' stroke-width='1' fill='none' fill-rule='evenodd' fill-opacity='0'%3E%3Cg transform='translate(-249.000000, -126.000000)' fill='%23FFFFFF'%3E%3Crect x='249' y='126' width='1' height='1'%3E%3C/rect%3E%3C/g%3E%3C/g%3E%3C/svg%3E)

![图片](data:image/svg+xml,%3C%3Fxml version='1.0' encoding='UTF-8'%3F%3E%3Csvg width='1px' height='1px' viewBox='0 0 1 1' version='1.1' xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'%3E%3Ctitle%3E%3C/title%3E%3Cg stroke='none' stroke-width='1' fill='none' fill-rule='evenodd' fill-opacity='0'%3E%3Cg transform='translate(-249.000000, -126.000000)' fill='%23FFFFFF'%3E%3Crect x='249' y='126' width='1' height='1'%3E%3C/rect%3E%3C/g%3E%3C/g%3E%3C/svg%3E)

但这个小工程背后，已经藏着 DDS 最核心的架构思想。

更有意思的是：

在真正把这个工程跑起来之前，我们一路撞上了：

```
OpenSSL
```

几个完全不同层级的问题。

而正是这些问题，把 Fast DDS 的整个工程链条暴露得异常清晰。

---

# 一、第一场冲突：为什么不用 UDP？

先假设我们正在做一个仿真系统。

AFSIM 中存在大量实时数据：

```
平台位置
```

外围又有：

```
三维态势显示
```

如果使用最直觉的 UDP 架构，很容易形成：

```
                       ┌──────────── 3D Viewer
```

起初非常舒服。

你只需要定义：

```
IP
```

例如：

```
5001 → PlatformState
```

但是随着系统增长，问题开始出现。

AFSIM 需要知道：

```
谁在接收？
```

最终通信代码非常容易逐渐变成：

```
sendToVisualization();
```

系统的依赖方向也开始反转。

本来：

```
AFSIM
```

只应该负责：

```
仿真
```

最后却逐渐知道所有外围系统。

这就是传统点对点通信最危险的地方：

> 数据生产者开始知道太多消费者。

---

# 二、DDS 真正想砍掉的，不是 Socket，而是“通信拓扑耦合”

DDS 的思路完全不同。

它不问：

```
我要把数据发给谁？
```

而是问：

```
我正在发布什么数据？
```

于是架构从：

```
Point-to-Point
```

变成：

```
                  DDS Data Space
```

AFSIM 不再知道：

```
Viewer IP
```

它只表达：

```
我正在发布 PlatformState。
```

而 Viewer 只表达：

```
我需要 PlatformState。
```

Recorder 也是：

```
我需要 PlatformState。
```

AI 也是。

这就是 DDS 的核心范式：

# Data-Centric Publish-Subscribe

数据中心发布订阅。

---

# 三、Socket 架构和 DDS 架构，真正差在哪里？

把两种模型并排来看：

```
┌──────────────────── Socket ────────────────────┐
```

Socket 世界中，应用关注：

```
Where?
```

DDS 世界中，应用关注：

```
What?
```

也就是说：

```
Socket：
```

这一层抽象差异，在两台机器时可能几乎没有价值。

但一旦系统进入：

```
几十个节点
```

它会变成完全不同的系统架构。

---

# 四、真正开始工程：一个 Telemetry Topic

我们的工程非常简单：

```
fastdds_cmake_demo/
```

其中真正的起点不是：

```
main()
```

而是：

```
Telemetry.idl
```

---

# 五、IDL：不要把它理解成一个“跨进程 struct”

我们的数据定义：

```
struct Telemetry
```

C++ 程序员第一反应是：

```
struct Telemetry
```

但是 IDL 的意义远远超过一个 struct。

它实际上是：

# Data Contract

数据契约。

今天：

```
Publisher = C++
```

明天可能变成：

```
Publisher = AFSIM C++
```

真正连接这些系统的不是：

```
Telemetry.hpp
```

而是：

```
Telemetry.idl
```

这就是接口描述语言真正的价值。

---

# 六、第一段真实排障：OpenSSL 从哪里冒出来的？

我们第一次执行：

```
find_package(fastdds 3 REQUIRED)
```

Fast DDS 已经被找到。

然后突然报：

```
Could NOT find OpenSSL
```

这很容易让人产生错觉：

> 为什么我发个 DDS 消息还得研究 TLS？

实际上不是 DDS 本身强制要求这个 Demo 使用 OpenSSL。

问题来自链接形态。

当 CMake 默认走到了：

```
Fast DDS static
```

那么 Fast DDS 的静态依赖会继续向应用暴露。

依赖图变成：

```
application.exe
```

于是应用需要承担完整的静态依赖闭包。

而对于这个教学工程，我们根本不需要。

于是明确：

```
set(BUILD_SHARED_LIBS ON)
```

依赖关系变成：

```
Application
```

第一关通过。

这一段排障其实已经暴露出一个很重要的工程事实：

> 中间件从来不只是 API，依赖管理本身就是中间件工程的一部分。

---

# 七、第二段排障：Fast DDS-Gen 找不到

接着 CMake 来到：

```
fastddsgen was not found
```

于是第二个系统组件出现了。

Fast DDS 不是只靠：

```
#include
```

就能完成完整类型系统。

IDL 还要经过：

```
Telemetry.idl
```

例如生成：

```
Telemetry.hpp
```

这些代码负责：

```
类型描述
```

于是整个链条第一次变得完整：

```
IDL
```

---

# 八、第三段排障：Java 又从哪里出来了？

找到：

```
fastddsgen.bat
```

以后，继续运行。

新的错误出现：

```
Java binary cannot be found.
```

第一次看到这里的时候非常有意思。

整个工程明明是：

```
C++
```

结果突然冒出来：

```
Java
```

原因很简单：

# Fast DDS-Gen 本身是 Java 程序。

所以真实工具链其实是：

```
CMake
```

配置：

```
JAVA_HOME
```

以后：

```
Java 17
```

开始正常工作。

第二关通过。

---

# 九、第四段排障：为什么 CMake 找得到 cl.exe，Fast DDS-Gen 却找不到？

接下来又来了一个很有代表性的错误：

```
Cannot run program ”cl.exe”
```

但 CMake 明明已经打印：

```
C++ compiler:
```

这看起来非常矛盾。

实际上并不矛盾。

CMake 知道：

```
CMAKE_CXX_COMPILER
```

的绝对路径。

但 Fast DDS-Gen 是另外一个进程。

它内部只是：

```
执行 cl.exe
```

于是 Java 子进程会从：

```
PATH
```

里查找。

这两套发现机制互相完全不知道对方。

于是我们在 CMake 中显式建立联系：

```
-ppPath
```

整个工具链终于闭环：

```
CMake
```

这一步非常漂亮。

因为它意味着工程不再依赖：

```
“请从 Visual Studio Developer Command Prompt 启动”
```

而是：

> 构建系统自己知道工具在哪里，并把依赖关系显式传递下去。

这才是工程化。

---

# 十、第五段排障：工具链全部正常，最后死在了一个变量名

终于：

```
Fast DDS
```

全部通过。

Fast DDS-Gen 开始真正解析：

```
Telemetry.idl
```

然后报：

```
mismatched input 'sequence' expecting ID
```

问题出现在：

```
unsigned long sequence;
```

原因：

```
sequence
```

本身就是 IDL 关键字。

例如：

```
sequence values;
```

所以：

```
unsigned long sequence;
```

类似于 C++：

```
int class;
```

最终改成：

```
unsigned long sequence_id;
```

整个工程第一次完整构建成功。

---

# 十一、这条排障链其实非常有意义

回头看，我们从一个看起来只是：

```
Publisher
```

的小 Demo，一口气穿过了：

```
CMake
```

这就是为什么真正的中间件能力不能简单等同于：

```
会不会调用 write()
```

整个构建链才是一个完整系统。

---

# 十二、现在终于可以讲 DDS 实体模型

Fast DDS 中最重要的一张图：

```
                       DomainParticipant
```

第一次看到会觉得对象太多。

但每一个都有非常明确的职责。

---

# 十三、DomainParticipant：进入 DDS 世界的入口

Publisher：

```
participant_ =
```

可以把它理解为：

```
这个进程在 DDS Domain 中的身份
```

例如：

```
DDS Domain 0
```

它们在同一个 Domain 中，才有可能互相发现。

---

# 十四、Domain ID：最简单的一层系统隔离

比如：

```
Domain 0
```

那么：

```
Domain 0 的 Participant
```

和：

```
Domain 20 的 Participant
```

天然属于不同数据空间。

所以 Domain ID 实际上已经是一种非常简单但有效的系统边界。

---

# 十五、TypeSupport：为什么 DDS 不能直接“认识 C++ struct”？

我们创建：

```
TypeSupport type_{
```

然后：

```
type_.register_type(participant_);
```

这一点对于新手特别关键。

DDS 真正需要知道的不是：

```
struct Telemetry
```

这几个字。

而是：

```
如何创建实例
```

Fast DDS-Gen 正是在自动生成这些能力。

于是：

```
Telemetry.idl
```

Participant 从此认识：

```
Telemetry
```

这种数据。

---

# 十六、Topic：DDS 真正的中心

创建 Topic：

```
topic_ = participant_->create_topic(
```

这里最容易犯的错误，就是继续把它理解成：

```
Topic = Channel Name
```

其实不够。

Topic 更接近：

```
Topic
```

于是：

```
SimulationTelemetryTopic
```

不是一个字符串频道。

它表达的是：

> DDS 数据空间中存在一种名为 SimulationTelemetryTopic 的 Telemetry 数据。

这是数据建模。

---

# 十七、为什么 Publisher 和 DataWriter 要分开？

Publisher：

```
publisher_ =
```

DataWriter：

```
writer_ =
```

因为：

```
Publisher
```

并不是“一个发数据函数”。

它是发布侧实体容器。

例如真实 AFSIM 插件里可能是：

```
DDS Publisher
```

而 DataWriter 才真正绑定：

```
一个 Topic
```

并发送 Sample。

---

# 十八、终于到了最简单的一行代码

构造数据：

```
Telemetry sample;
```

发送：

```
writer_->write(&sample);
```

结束。

一行代码。

但是这一行下面是：

```
C++ Object
```

这才是真正意义上的：

# Middleware

---

# 十九、Subscriber 是完全镜像的世界

Subscriber：

```
subscriber_ =
```

DataReader：

```
reader_ =
```

两边于是形成：

```
Publisher Process                  Subscriber Process
```

注意这里始终没有：

```
IP Address
```

也没有：

```
Port
```

---

# 二十、那它们到底怎么互相找到？

答案是：

# Discovery

Fast DDS 不要求业务代码手工维护：

```
peer list
```

Participant 会自动发现彼此。

然后 Endpoint 再进一步发现：

```
DataWriter
```

并检查：

```
Domain
```

于是可以粗略理解成：

```
阶段 1：
```

只要匹配：

```
Topic
```

通信关系自动形成。

---

# 二十一、这就是 DDS 最震撼的地方

Publisher 并不知道：

```
Subscriber 有几个
```

它只知道：

```
我有一个 DataWriter
```

这就是：

# Location Transparency

位置透明性。

在大型分布式系统中，这种能力极其重要。

---

# 二十二、Listener：DDS 天然更适合事件驱动系统

Subscriber 不是：

```
while (true)
```

而是：

```
on_data_available()
```

DDS 中间件通知：

```
有数据来了
```

然后：

```
reader->take_next_sample(
```

这非常适合：

```
GUI
```

因为它天然是：

```
event-driven
```

而不是：

```
blocking receive
```

---

# 二十三、为什么回调里还要 while？

因为一次回调并不意味着：

```
只有一条数据
```

DataReader 自己拥有 History。

于是：

```
while (true)
```

表达的是：

```
把 DataReader 当前积压的数据一次消费完
```

这时 DDS 已经明显不再只是：

```
网络接口
```

DataReader 本身就是一个具有：

```
缓存
```

的数据实体。

---

# 二十四、真正让 DDS 脱离“高级 UDP”的关键：QoS

我们的 Demo 使用：

```
RELIABLE_RELIABILITY_QOS
```

以及：

```
KEEP_LAST_HISTORY_QOS
```

这两行是理解 DDS 的转折点。

因为 DDS 不是只描述：

```
What data?
```

它还描述：

```
How should this data behave?
```

也就是说：

```
Data
```

这就是 QoS。

---

# 二十五、QoS 本质上是在给数据定义“物理规律”

比如平台状态：

```
air-1 在 10.0 秒的位置
```

很多时候：

```
只要最新一条
```

那么：

```
KEEP_LAST
```

可能已经足够。

但导弹发射事件：

```
MISSILE_LAUNCHED
```

如果丢了一次：

```
可能整个评估结果都错了
```

于是它可能需要：

```
Reliable
```

再比如：

```
Heartbeat
```

它真正关注的可能不是历史，而是：

```
一定时间内有没有更新
```

于是应该关注：

```
Deadline
```

DDS 的强大之处就在这里。

它让不同数据拥有不同的通信生命规则。

---

# 二十六、状态、事件、命令——应该分开建模

真正的系统，不应该做成：

```
EverythingTopic
```

更不应该：

```
struct Message
```

然后把所有内容塞进去。

这样虽然“也能通信”，但本质上是在用 DDS 模拟：

```
UDP + JSON
```

DDS 的类型系统和 QoS 价值几乎被全部抹掉。

更合理的是：

```
PlatformState
```

每一种都有：

```
独立 IDL
```

于是数据空间变成：

```
                  DDS Domain
```

这才是真正的：

# Typed Data Bus

强类型数据总线。

---

# 二十七、现在把它放回 AFSIM

这也是这个案例真正有价值的地方。

AFSIM 本身是一个典型的复杂仿真系统。

内部有：

```
Simulation
```

外围又可能连接：

```
飞控
```

传统做法很容易让每一个插件自己：

```
开 UDP
```

最终形成：

```
AFSIM Plugin A → UDP
```

整个系统的通信基础设施碎片化。

---

# 二十八、如果用 DDS 做 AFSIM 外部数据总线

我们可以构建一个统一 Bridge：

```
                     AFSIM Engine
```

控制方向：

```
External Controller
```

于是整个系统开始形成：

```
AFSIM
```

---

# 二十九、AFSIM 不再知道外部系统是谁

这是最重要的变化。

过去：

```
AFSIM Plugin
```

现在：

```
AFSIM Plugin
```

外部有多少消费者：

```
0
```

AFSIM 不关心。

这才是真正意义上的：

# Decoupling

解耦。

---

# 三十、DDS 甚至可以成为 AFSIM 的“系统边界”

理想情况下，可以设计：

```
AFSIM Internal World
```

DDS Bridge 负责：

```
AFSIM Object
```

这就是一个非常清晰的 Anti-Corruption Layer。

外部系统不需要：

```
#include WsfPlatform.hpp
```

AFSIM 也不需要：

```
include external SDK
```

双方只共享：

```
IDL
```

---

# 三十一、这和之前 RuntimeMessageBus 的思想其实高度一致

对于同进程插件：

```
Plugin A
```

它解决的是：

```
进程内解耦
```

DDS：

```
Process A
```

解决的是：

```
进程间 / 机器间数据解耦
```

于是可以形成两层总线：

```
AFSIM Process
```

这是一种非常漂亮的架构分层。

---

# 三十二、进程内总线和进程外总线不要混成一个东西

这一点非常重要。

不要让每一个 AFSIM 内部插件都自己创建：

```
DomainParticipant
```

否则 DDS 又会渗入所有业务代码。

更合理的是：

```
AFSIM Business Plugins
```

这就是：

# Adapter Architecture

DDS 是基础设施。

不是业务模型。

---

# 三十三、最终的数据模型应该是什么样？

可以逐步设计：

```
afsim::PlatformState
```

例如：

```
struct PlatformState
```

而不是：

```
struct UniversalMessage
```

---

# 三十四、下一步最值得研究的是 Keyed Topic

假设：

```
PlatformState Topic
```

中有：

```
air-1
```

DDS 最理想的建模方式不是：

```
一堆无关联消息
```

而是：

```
PlatformState
```

也就是说：

```
platform_id
```

应该逐渐演化为：

# DDS Key

这一步之后，DDS 才真正开始展现数据中心模型的力量。

---

# 三十五、真正值得学习的不是 Fast DDS API

当这个 Demo 跑起来以后，我并不建议下一步去背：

```
几十个 Class
```

更好的学习路线是：

### 第一阶段

理解：

```
DomainParticipant
```

### 第二阶段

研究：

```
Discovery
```

### 第三阶段

研究：

```
Reliability
```

### 第四阶段

研究：

```
Keyed Topic
```

### 第五阶段

研究：

```
UDP
```

### 第六阶段

再进入：

```
Discovery Server
```

这条路线比死背 API 有意义得多。

---

# 三十六、最终回头看：DDS 到底复杂吗？

如果你的系统永远只有：

```
Process A
```

那么 DDS 可能真的太重。

UDP：

```
20 行
```

就够了。

但如果系统未来是：

```
几十个服务
```

你迟早需要实现：

```
Discovery
```

问题只是：

```
这些复杂性由谁承担？
```

你自己？

还是中间件？

---

# 三十七、所以 DDS 真正卖的不是“通信”

这是理解 DDS 最关键的一句话：

# DDS 卖的不是消息传输，而是分布式数据模型。

它真正提供的是：

```
Strong Type
```

它把：

```
网络世界
```

提升成：

```
数据世界
```

---

# 三十八、从 writer_->write() 再看一次整个系统

最终发送代码：

```
writer_->write(&sample);
```

只有一行。

但是现在我们已经知道：

这一行背后站着：

```
IDL
```

这就是成熟中间件真正应该做的事情：

> 把复杂性压到基础设施里，让业务代码变得极其简单。

---

# 结语：从一个 Demo，到一条新的 AFSIM 系统边界

我们一开始只是想做：

```
Publisher
```

然后一路经历：

```
找不到 Fast DDS
```

看起来是一条“踩坑史”。

但换一个角度看：

这其实是一条完整的：

# Middleware Stack Discovery

我们从最上层应用，一路摸到了：

```
构建系统
```

最后再回到架构。

而真正值得带走的，并不是：

```
fastddsgen 应该怎么配
```

也不是：

```
create_datawriter 怎么调用
```

真正重要的是理解：

> 一个大型仿真系统，最终一定需要一个稳定的数据边界。

AFSIM 内部应该继续保持它擅长的：

```
Simulation
```

而外部世界：

```
AI
```

不应该通过无数条私有 UDP 链路侵入引擎。

更理想的形态是：

```
                     AFSIM
```

AFSIM 不知道它们是谁。

它只知道：

```
PlatformState
```

而外部系统，也不需要知道：

```
WsfPlatform
```

它们只需要知道：

```
IDL
```

这就是一条真正稳定的系统边界。

所以这个 Fast DDS Demo 最终最有价值的地方，不是它证明了：

> “DDS 能发数据。”

而是它让我们第一次真正看到了一种可能：

# 把 AFSIM 从“连接所有系统的程序”，升级成“向数据空间发布仿真世界的引擎”。

当系统走到这一步，

DDS 就不再只是一个通信库。

它开始成为：

# 仿真基础设施的一部分。
