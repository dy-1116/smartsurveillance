# smartsurveillance
这是一个基于C++，Qt，OpenCV的智能监控方案



# 基于 Qt 与 OpenCV 的多路智能视频监控系统 — 完整技术方案

> 项目定位：简历项目（二人开发）
> 技术栈：C++17 / Qt 5 Widgets / OpenCV / SQLite / CMake
> 文档版本：v1.0

---

## 目录

1. [整体思路](#1-整体思路)
2. [技术选型](#2-技术选型)
   - 2.1 原始文档技术栈回顾
   - 2.2 保留项与舍弃项
   - 2.3 最终技术栈
3. [目录结构](#3-目录结构)
   - 3.1 项目文件树
   - 3.2 文件清单表
4. [技术原理解释](#4-技术原理解释)
   - 4.1 视频采集与格式转换
   - 4.2 两帧差分法运动检测
   - 4.3 基于质心与 IOU 的多目标追踪
   - 4.4 多线程架构与线程池
   - 4.5 九路并发性能优化
   - 4.6 断线自动重连机制
   - 4.7 SQLite 元数据管理
   - 4.8 运动报警与事件快照
   - 4.9 视频存储与编码
   - 4.10 GUI 界面设计
5. [代码片段解释](#5-代码片段解释)
   - 5.1 两帧差法核心流程
   - 5.2 IOU 计算与贪心匹配
   - 5.3 线程安全队列与条件变量
   - 5.4 指数退避重连状态机
   - 5.5 Mat 转 QImage（深拷贝原因）
   - 5.6 报警闪烁（QTimer 样式切换）
6. [二人分工方案](#6-二人分工方案)
7. [开发阶段与里程碑](#7-开发阶段与里程碑)

---

## 1. 整体思路

### 1.1 项目目标

构建一个支持最多 9 路摄像头同时接入的实时视频监控系统，核心能力包括：

- 多路视频实时显示（3×3 九宫格）
- 运动目标检测与矩形框标注
- 多目标跨帧追踪与 ID 持久化
- 手动视频录制（H.264/MP4）
- 断线自动重连
- SQLite 元数据记录（录制文件 + 运动事件）
- 运动报警（界面高亮 + 事件快照）

### 1.2 设计原则

| 原则 | 说明 |
|---|---|
| **精简优先** | 舍弃工业级复杂技术，确保二人在 3~4 周内完成可演示的 demo |
| **模块解耦** | 分层架构，UI 层不持业务逻辑，视频处理与显示通过信号槽通信 |
| **接口先行** | 开发前对齐所有跨模块信号和类接口，二人并行开发，最后联调 |
| **简历导向** | 保留 2~3 个有技术深度的亮点（线程池并发优化、追踪算法、重连状态机），面试时可展开讲解 |

### 1.3 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                   UI 展示层（Qt Widgets）                     │
│   3×3 九宫格视频 │ 控制面板 │ 参数调节 │ 报警闪烁 │ 全屏     │
├─────────────────────────────────────────────────────────────┤
│                   业务调度层                                   │
│   CameraManager（多摄像头生命周期）│ DatabaseManager（SQLite） │
├─────────────────────────────────────────────────────────────┤
│                   视频处理层（工作线程）                        │
│   ThreadPool 线程池 │ VideoWorker 单路处理 │ MotionDetector   │
│   Tracker 多目标追踪 │ 断线重连状态机 │ 视频录制              │
├─────────────────────────────────────────────────────────────┤
│                   数据存储层                                   │
│   MP4 视频文件（H.264）│ SQLite 数据库 │ JPG 事件快照        │
└─────────────────────────────────────────────────────────────┘
```

### 1.4 数据流（单路摄像头）

```
RTSP流/本地视频
    │
    ▼
VideoCapture 抓帧 ──► 降分辨率（320×240 处理）
    │
    ▼
两帧差法运动检测 ──► 输出 vector<Rect> 运动区域
    │
    ▼
追踪器更新 ──► 质心+IOU匹配 ──► 分配/更新 ID
    │
    ▼
在帧上绘制矩形框 + ID标签
    │
    ├──────────────► emit frameReady → UI 显示（放大回原尺寸）
    ├──────────────► 运动触发 → emit motionDetected → 报警 + 快照 + 写事件库
    └──────────────► 录制中 → VideoWriter 写入 MP4 → 写录制记录库
```

---

## 2. 技术选型

### 2.1 原始文档技术栈回顾

用户提供的两份原始技术架构文档中使用的技术栈如下：

| 技术 | 文档1 | 文档2 | 用途 |
|---|---|---|---|
| C++ / STL | ✅ | ✅ | 开发语言 |
| Boost（Asio/thread） | ✅ | ✅ | 网络通信、线程 |
| Qt（QML/C++混合） | ✅ | ✅ | GUI 界面 |
| OpenCV | ✅ | ✅ | 视频捕获、运动检测 |
| VLC（libvlc） | ✅ | ✅ | 视频编解码、RTSP接收、存储 |
| FFmpeg | ❌ | ✅（备选） | 视频解码 |
| RTSP 协议 | ✅ | ✅ | 视频流传输 |
| H.264 | ✅ | ✅ | 视频编码 |
| H.265 | ❌ | ✅（备选） | 视频编码 |
| MOG2 背景减法 | ✅ | ✅ | 运动检测算法 |
| KCF / 光流法 | ✅ | ❌ | 目标追踪 |
| 卡尔曼滤波 / SORT / DeepSORT | ❌ | ✅（备选） | 目标追踪 |
| SQLite / MySQL | ❌ | ✅ | 元数据存储 |
| 多线程 / 多进程 | ✅ | ✅ | 并发模型 |

### 2.2 保留项与舍弃项

#### 保留项

| 技术 | 保留理由 |
|---|---|
| **C++17** | 原始文档指定语言，STL 容器和智能指针足够使用 |
| **Qt 5** | 原始文档指定 GUI 框架，但**从 QML 改为 Widgets**——QML 学习成本高、与 C++ 交互复杂，Widgets 更适合简历项目，九宫格用 QGridLayout 即可实现 |
| **OpenCV** | 核心视频处理库，VideoCapture 可直接读 RTSP（替代 VLC），VideoWriter 可直接存 MP4（替代 VLC 转码），运动检测和轮廓提取一站式完成 |
| **RTSP + H.264** | 原始文档指定的视频流传输和编码标准，兼容性最好 |
| **多线程** | 多路视频必须并发处理，但**从每路一线程优化为线程池**，这是简历亮点 |
| **SQLite** | 文档2提出的元数据方案，比 MySQL 轻量得多，零配置，Qt 原生支持 |
| **运动检测 + 目标追踪** | 核心功能，但算法从 MOG2/KCF/卡尔曼/SORT 简化为两帧差法 + 质心IOU匹配 |

#### 舍弃项

| 技术 | 舍弃理由 | 替代方案 |
|---|---|---|
| **VLC（libvlc）** | 与 OpenCV 功能严重重叠；VLC 拿帧转 OpenCV Mat 极其麻烦（需要回调+内存拷贝）；sout 转码存储调试成本高；引入额外依赖 | OpenCV VideoCapture 直接读 RTSP，VideoWriter 直接存 MP4 |
| **Boost 全部** | Boost.Asio 网络层是重复造轮子（RTSP 握手由 VideoCapture 内部完成）；Boost 线程与 std::thread 重叠；引入额外依赖 | std::thread + std::mutex + std::condition_variable |
| **FFmpeg** | OpenCV 编译时通常已内置 FFmpeg 后端，不需要直接调用 FFmpeg API | 直接用 OpenCV 高层接口 |
| **H.265** | 编码兼容性差、CPU 开销大，OpenCV VideoWriter 对 H.265 支持不稳定 | 只用 H.264 |
| **MOG2 背景减法** | 比两帧差法复杂，调参成本高；简历项目两帧差法足够演示，且原理更简单易懂 | 两帧差分法 |
| **KCF / 光流法 / 卡尔曼 / SORT / DeepSORT** | 调参复杂、实时性差、代码量大，二人项目做不完；DeepSORT 还需要深度学习模型 | 基于质心距离 + IOU 的贪心匹配（≤80行代码） |
| **MySQL** | 需要单独安装数据库服务器、配置账号端口，部署麻烦 | SQLite（单文件，零配置） |
| **多进程模型** | 进程间通信复杂，9 路以内多线程完全够用 | 多线程 + 线程池 |
| **GPU 加速 / 无锁数据结构 / 带宽自适应** | 过度设计，简历项目用不上，增加实现复杂度 | 全部砍掉 |

### 2.3 最终技术栈

| 层次 | 技术 | 版本/规格 |
|---|---|---|
| 语言 | C++ | C++17 |
| GUI | Qt | Qt 5 Widgets（不含 QML） |
| 视频处理 | OpenCV | 4.x（VideoCapture + VideoWriter + imgproc） |
| 数据库 | SQLite | 通过 Qt SQL 模块（QSQLITE 驱动） |
| 并发 | std::thread | + mutex + condition_variable + atomic |
| 构建 | CMake | 3.10+，开启 AUTOMOC |
| 视频编码 | H.264 | fourcc='avc1'，回退 'mp4v' |
| 传输协议 | RTSP | TCP 传输 |
| 样式 | QSS | 深色主题 |

---

## 3. 目录结构

### 3.1 项目文件树

```
SmartSurveillance/
├── CMakeLists.txt              # 构建配置
├── main.cpp                    # 程序入口
├── style.qss                   # 深色主题样式表
├── include/                    # 头文件（10个）
│   ├── MotionDetector.h        # 两帧差法运动检测
│   ├── Tracker.h               # 多目标追踪
│   ├── VideoWorker.h           # 单路视频处理线程
│   ├── ThreadPool.h            # 线程池
│   ├── CameraManager.h         # 多摄像头管理
│   ├── DatabaseManager.h       # SQLite 元数据
│   ├── MainWindow.h            # 主窗口
│   ├── VideoLabel.h            # 视频格子自定义控件
│   └── FullScreenDialog.h      # 全屏单路查看
├── src/                        # 源文件（10个）
│   ├── MotionDetector.cpp
│   ├── Tracker.cpp
│   ├── VideoWorker.cpp
│   ├── ThreadPool.cpp
│   ├── CameraManager.cpp
│   ├── DatabaseManager.cpp
│   ├── MainWindow.cpp
│   ├── VideoLabel.cpp
│   └── FullScreenDialog.cpp
├── resources/                  # 静态资源（可选）
│   └── icons/
├── recordings/                 # 运行时生成：录制视频
├── snapshots/                  # 运行时生成：报警快照
└── surveillance.db             # 运行时生成：SQLite 数据库
```

### 3.2 文件清单表

| # | 文件 | 类型 | 模块 | 核心职责 | 负责人 |
|---|---|---|---|---|---|
| 1 | `CMakeLists.txt` | 构建 | 构建系统 | Qt5+OpenCV+SQLite，AUTOMOC，链接 pthread | B |
| 2 | `main.cpp` | 源文件 | 入口 | QApplication 初始化，加载样式表，启动主窗口 | B |
| 3 | `style.qss` | 资源 | UI | 深色主题：视频格子/按钮/滑块/状态栏/录制高亮 | B |
| 4 | `include/MotionDetector.h` | 头文件 | 运动检测 | 两帧差法检测器类声明 | A |
| 5 | `src/MotionDetector.cpp` | 源文件 | 运动检测 | 灰度→模糊→帧差→二值化→形态学→轮廓→画框 | A |
| 6 | `include/Tracker.h` | 头文件 | 目标追踪 | DetectedObject 结构 + 追踪器类声明 | A |
| 7 | `src/Tracker.cpp` | 源文件 | 目标追踪 | IOU计算、贪心匹配、ID分配、生命周期管理 | A |
| 8 | `include/VideoWorker.h` | 头文件 | 视频处理 | 单路工作线程类，4个信号定义 | A |
| 9 | `src/VideoWorker.cpp` | 源文件 | 视频处理 | 抓帧→检测→追踪→录制→重连→emit信号 | A |
| 10 | `include/ThreadPool.h` | 头文件 | 并发 | 线程池类声明，任务队列 | A |
| 11 | `src/ThreadPool.cpp` | 源文件 | 并发 | 固定线程数，条件变量调度，有界队列 | A |
| 12 | `include/CameraManager.h` | 头文件 | 摄像头管理 | 多摄像头增删、生命周期 | B |
| 13 | `src/CameraManager.cpp` | 源文件 | 摄像头管理 | map管理VideoWorker，信号转发 | B |
| 14 | `include/DatabaseManager.h` | 头文件 | 数据存储 | SQLite 管理器类声明 | B |
| 15 | `src/DatabaseManager.cpp` | 源文件 | 数据存储 | 建表、录制记录、运动事件写入 | B |
| 16 | `include/MainWindow.h` | 头文件 | UI | 主窗口类声明 | B |
| 17 | `src/MainWindow.cpp` | 源文件 | UI | 九宫格布局、控制面板、参数调节、报警响应 | B |
| 18 | `include/VideoLabel.h` | 头文件 | UI | 视频格子自定义控件 | B |
| 19 | `src/VideoLabel.cpp` | 源文件 | UI | 状态切换、报警闪烁、双击全屏 | B |
| 20 | `include/FullScreenDialog.h` | 头文件 | UI | 全屏对话框 | B |
| 21 | `src/FullScreenDialog.cpp` | 源文件 | UI | 全屏单路显示、信息栏、ESC退出 | B |

**统计**：头文件 10 个 + 源文件 10 个 + 构建文件 1 个 + 样式资源 1 个 = 22 个代码文件。成员A负责 8 个（算法+并发），成员B负责 14 个（UI+数据+构建）。

---

## 4. 技术原理解释

### 4.1 视频采集与格式转换

#### 4.1.1 视频采集

使用 OpenCV 的 `cv::VideoCapture` 统一处理 RTSP 网络流和本地视频文件，接口完全一致：

- **RTSP 流**：传入 `rtsp://` 地址，OpenCV 内部通过 FFmpeg/GStreamer 后端完成 RTSP 握手、RTP 收包、H.264 解码，输出 `cv::Mat` 原始帧
- **本地文件**：传入 `.mp4` 路径，用于无摄像头环境下的开发测试
- **USB 摄像头**：传入设备编号（如 `0`）

采集循环以固定帧率（默认 15fps）运行，通过 `std::this_thread::sleep_for` 控制节奏。

#### 4.1.2 cv::Mat → QImage 转换

OpenCV 的帧格式是 `cv::Mat`（BGR 通道顺序），Qt 显示需要 `QImage`（RGB 通道顺序）：

1. `cv::cvtColor` 将 BGR 转为 RGB
2. 用 `QImage(uchar* data, width, height, bytesPerLine, format)` 构造图像
3. **必须调用 `.copy()`**：构造函数只持有数据指针的引用，不拷贝内存；原 `cv::Mat` 在工作线程结束后会被释放，UI 线程再访问就会崩溃。`.copy()` 确保 QImage 拥有独立的内存副本。

---

### 4.2 两帧差分法运动检测

#### 4.2.1 基本原理

两帧差分法是最简单的运动检测算法：**相邻两帧中，像素灰度值发生显著变化的区域即为运动区域**。

对于固定摄像头场景，背景静止，只有运动物体导致像素变化。将当前帧与前一帧做差，差异大的像素就是运动物体的轮廓。

#### 4.2.2 处理流程

```
当前帧 ──► 转灰度 ──► 高斯模糊去噪 ──┐
                                        ├─► absdiff 帧差 ──► threshold 二值化
前一帧（灰度+模糊）─────────────────────┘
                                                        │
                                                        ▼
                                          形态学：腐蚀 → 膨胀×2
                                                        │
                                                        ▼
                                          findContours 轮廓提取
                                                        │
                                                        ▼
                                          面积过滤 → boundingRect 输出矩形框
```

#### 4.2.3 各步骤作用

| 步骤 | 作用 | 关键参数 |
|---|---|---|
| 转灰度 | 三通道降为单通道，减少计算量，帧差只需亮度信息 | — |
| 高斯模糊 | 去除传感器噪点和压缩伪影，避免噪点被误检为运动 | 核大小 5×5 |
| absdiff 帧差 | 逐像素计算两帧灰度差的绝对值，输出差异图 | — |
| threshold 二值化 | 差异大于阈值的像素标为白色（运动），否则黑色（背景） | threshold=30 |
| 腐蚀 | 去除孤立小白点（噪点），缩小白色区域 | 5×5 结构元 |
| 膨胀 | 连接断裂的运动区域，让检测框更完整；膨胀两次补偿腐蚀的缩小 | 5×5 结构元 |
| findContours | 提取白色区域的外轮廓，每个轮廓对应一个候选目标 | RETR_EXTERNAL |
| 面积过滤 | 轮廓面积小于 minArea 的视为噪点丢弃 | minArea=500 |
| boundingRect | 计算轮廓的最小外接矩形，作为最终输出 | — |

#### 4.2.4 两帧差法的局限

| 局限 | 表现 | 本项目的应对 |
|---|---|---|
| 双影问题 | 运动物体前后沿都被检测到，框比实际物体大 | 膨胀操作连接前后沿，形成一个完整框 |
| 对缓慢运动不敏感 | 物体移动速度低于每帧1像素时检测不到 | 降低 threshold（如20），固定场景下调参平衡 |
| 光照突变误检 | 开灯/关灯导致整帧差异，大面积误检 | 高斯模糊+面积过滤，极端场景需结合背景建模 |
| 无法检测静止目标 | 物体停下来后不再产生帧差，框消失 | 追踪器生命值机制让ID保留若干帧 |

---

### 4.3 基于质心与 IOU 的多目标追踪

#### 4.3.1 追踪的目标

运动检测每帧输出一组矩形框，但不知道"这一帧的框A"和"上一帧的框B"是不是同一个物体。追踪器的任务是**跨帧关联同一目标，分配持久化 ID**。

#### 4.3.2 核心数据结构

每个被追踪的目标维护以下状态：

| 字段 | 含义 |
|---|---|
| id | 全局唯一编号，从 0 递增 |
| bbox | 当前帧的外接矩形 |
| centroid | 质心坐标（bbox 中心点） |
| life | 剩余生命值，连续未匹配时递减，到 0 则移除 |
| hitCount | 连续匹配帧数，新目标 hitCount<2 时不绘制 ID，避免闪烁 |

#### 4.3.3 匹配算法：贪心 + 综合评分

对每一个已追踪目标，在当前帧的所有检测框中找最佳匹配：

1. **计算 IOU（交并比）**：两个矩形的交集面积 ÷ 并集面积。IOU 越高说明重叠越多，越可能是同一目标。IOU 低于 0.1 的候选直接排除。
2. **计算质心距离**：两个框中心点的欧氏距离。距离超过 100 像素的候选排除。
3. **综合评分**：`score = IOU × 0.7 + (1 - 距离/最大距离) × 0.3`，IOU 权重更高。
4. **贪心匹配**：每个目标选评分最高的检测框，标记为"已匹配"，后续目标不再使用。

#### 4.3.4 目标生命周期管理

```
新检测框（未被任何已有目标匹配）
    │
    ▼
创建新目标，分配新 ID，life=最大值，hitCount=1
    │
    ▼
每帧更新：
    ├─ 匹配成功 → 更新 bbox/centroid，life 恢复最大值，hitCount++
    └─ 匹配失败 → life--，hitCount=0
         │
         ▼
    life <= 0 → 目标被移除
```

**生命值机制的意义**：物体被短暂遮挡时，检测框消失，但 ID 不会立刻丢失；遮挡结束后物体重新出现，还能匹配回原来的 ID。

#### 4.3.5 IOU 计算原理

```
IOU = 交集面积 / (矩形A面积 + 矩形B面积 - 交集面积)
```

- IOU = 1：两个矩形完全重合
- IOU = 0：两个矩形完全不相交
- 运动物体在相邻帧间位移不大，IOU 通常在 0.3~0.8 之间

---

### 4.4 多线程架构与线程池

#### 4.4.1 为什么需要多线程

如果所有工作都在 UI 线程做：抓帧→检测→追踪→画框→显示，9 路视频串行处理会导致界面完全卡死。必须将视频处理放到工作线程，UI 线程只负责渲染。

#### 4.4.2 架构：线程池替代每路一线程

- **初版（每路一线程）**：每接入一路就创建一个 `std::thread`，9 路就是 9 个线程。实现简单，但线程数不可控，CPU 上下文切换开销大。
- **优化版（线程池）**：创建固定数量的工作线程（等于 CPU 核心数，如 8 个），所有摄像头的处理任务提交到任务队列，工作线程从队列取任务执行。线程数固定，CPU 利用率更稳定。

#### 4.4.3 线程安全队列（生产者-消费者模型）

线程池的核心数据结构是线程安全的任务队列：

- **生产者**：摄像头采集线程把"处理某一帧"的任务推入队列
- **消费者**：线程池中的工作线程从队列取出任务执行
- **同步原语**：
  - `std::mutex`：保护队列的并发访问
  - `std::condition_variable`：工作线程在队列为空时阻塞等待（`wait`），有新任务时被唤醒（`notify_one`），避免空转占 CPU
- **有界队列**：队列长度设上限（如 10），队列满时丢弃最旧的帧（视频监控场景，旧帧不如新帧有价值），避免内存暴涨

#### 4.4.4 跨线程 UI 更新：Qt 信号槽

工作线程不能直接操作 UI 控件（Qt 规定 UI 只能在主线程操作），通过 Qt 的信号槽机制安全跨线程通信：

- 工作线程 `emit frameReady(cameraId, qImage)`
- Qt 自动将信号排队到 UI 线程的事件循环（队列连接，Queued Connection）
- UI 线程的槽函数被调用，更新 `QLabel` 的 `QPixmap`

---

### 4.5 九路并发性能优化

> 核心矛盾：9 路视频同时做运动检测，普通笔记本 CPU 扛不住。

#### 4.5.1 性能瓶颈分析

假设每路 720p（1280×720）、15fps：

- 9 路 × 15fps = 135 帧/秒
- 每帧像素数 = 921,600
- 每秒处理像素 ≈ 1.24 亿
- 每帧约 10 次图像操作（灰度+模糊+帧差+二值化+形态学×3+轮廓等）
- 实际计算量 ≈ 12 亿像素操作/秒，普通 CPU 无法实时完成

#### 4.5.2 四层降载优化

| 优化层 | 手段 | 效果 | 实现方式 |
|---|---|---|---|
| **输入层** | 降分辨率 | 处理像素量降为 1/16 | 抓帧后 `cv::resize` 到 320×240 做检测，显示时放大回原尺寸 |
| **计算层** | 跳帧检测 | 计算量降为 1/3 | 每 3 帧只做 1 次检测，其余帧直接显示不检测 |
| **调度层** | 线程池 | 避免线程过度订阅 | 固定 8 个工作线程（=CPU核心数），9 路任务共享 |
| **显示层** | UI 刷新节流 | 减少 UI 线程负担 | 全局限制 UI 刷新帧率（如 10fps），旧帧丢弃 |

#### 4.5.3 跳帧策略

维护帧计数器 `frameCount`，每收到一帧递增：
- `frameCount % 3 == 0`：做完整的运动检测 + 追踪更新
- 其余帧：不做检测，直接用追踪器中已有目标位置绘制框，然后显示

效果：运动检测计算量降为 1/3，人眼几乎察觉不到差异（15fps 中 5fps 检测，运动连贯）。

#### 4.5.4 降分辨率策略

- 抓帧得到原始帧（如 1280×720）
- `cv::resize` 缩小到 320×240（面积缩为 1/16）
- 在小图上做运动检测和追踪
- 检测得到的矩形框坐标按比例放大（×4），绘制在原始帧上
- UI 显示原始帧（带放大后的框），画质不受影响

> 注意：`minArea` 参数要按缩小后的分辨率调整，320×240 画面中 500 像素已经很大，可能需调到 100~200。

---

### 4.6 断线自动重连机制

#### 4.6.1 为什么需要重连

RTSP 网络流不稳定，常见断线原因：网络波动、摄像头重启、流媒体服务器超时。如果不做重连，一路断线后就永久黑屏，需要人工重新连接。

#### 4.6.2 状态机设计

VideoWorker 内部维护简单状态机：

```
start() → 连接中 → open()成功 → 正常运行 → read()失败 → 重连等待
                                                          │
                                              指数退避计时结束 │
                                                          ▼
                                                    重连尝试 → open()成功 → 重置状态 → 正常运行
                                                          │
                                                    open()失败 │
                                                          ▼
                                              重试次数 < 上限？
                                                    │ 是        │ 否
                                                    ▼           ▼
                                              回到重连等待   彻底断开，通知UI
```

#### 4.6.3 指数退避算法

重连不能立刻疯狂重试（会打爆摄像头），采用指数退避：

- 第 1 次：等 3 秒
- 第 2 次：等 6 秒
- 第 3 次：等 12 秒
- 第 4 次：等 24 秒
- ...
- 等待上限：60 秒
- 最大重试：10 次，超过后彻底断开

```
等待时间 = min(3 × 2^(重试次数-1), 60) 秒
```

**为什么用指数退避**：网络故障通常需要时间恢复，立即重试大概率失败；等待时间递增既给网络恢复留了时间，又避免固定间隔的资源浪费。

#### 4.6.4 重连成功后的状态重置

重连成功后，视频流从新的关键帧开始，之前的缓存全部失效，必须重置：

1. **MotionDetector::reset()**：清空缓存的前一帧，否则新流第一帧和旧流最后一帧做差，会产生全画面运动误检
2. **Tracker::reset()**：清空所有追踪目标和 ID 计数器，否则旧目标 ID 会和新画面物体错误关联
3. **帧队列清空**：丢弃重连前积压的所有帧
4. **UI 状态更新**：从"重连中..."恢复为正常显示

---

### 4.7 SQLite 元数据管理

#### 4.7.1 为什么用 SQLite

- **零配置**：单文件数据库，不需要安装服务器，不需要配置端口账号
- **Qt 原生支持**：`QSqlDatabase` 自带 QSQLITE 驱动，`find_package(Qt5 COMPONENTS Sql REQUIRED)` 即可
- **够用**：简历项目数据量小（几百条记录）
- **简历价值**：体现数据库设计能力

#### 4.7.2 数据库表设计

**表1：recordings（录制文件记录）**

| 字段 | 类型 | 约束 | 说明 |
|---|---|---|---|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | 自增主键 |
| camera_id | INTEGER | NOT NULL | 摄像头编号 0~8 |
| file_path | TEXT | NOT NULL | 视频文件绝对路径 |
| start_time | TEXT | NOT NULL | 开始时间 `YYYY-MM-DD HH:MM:SS` |
| end_time | TEXT | NOT NULL | 结束时间 |
| duration | INTEGER | NOT NULL | 录制时长（秒） |
| file_size | INTEGER | NOT NULL | 文件大小（字节） |

**表2：motion_events（运动事件记录）**

| 字段 | 类型 | 约束 | 说明 |
|---|---|---|---|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT | 自增主键 |
| camera_id | INTEGER | NOT NULL | 摄像头编号 |
| start_time | TEXT | NOT NULL | 事件开始时间 |
| end_time | TEXT | NOT NULL | 事件结束时间 |
| object_count | INTEGER | NOT NULL | 事件期间同时出现的目标数（平均值） |
| max_objects | INTEGER | NOT NULL | 事件期间峰值目标数 |
| snapshot_path | TEXT | | 事件快照图片路径 |

**索引**：在 `camera_id` 和 `start_time` 上建索引，加速按摄像头和时间范围查询。

#### 4.7.3 事件检测逻辑（运动从无到有/从有到无）

运动事件不是每一帧都写一条，而是将连续的运动帧合并为一个事件：

```
维护状态：eventInProgress、eventStartTime、currentMaxObjects

每帧处理完运动检测后：
    if 检测到运动目标:
        if eventInProgress == false:
            eventInProgress = true
            eventStartTime = 当前时间
            currentMaxObjects = 目标数
        else:
            currentMaxObjects = max(currentMaxObjects, 目标数)
    else:
        if eventInProgress == true:
            连续 N 帧无运动后（宽限期2秒）:
                写入 motion_events 表
                eventInProgress = false
```

**宽限期（debounce）的意义**：运动检测偶尔会丢帧（物体被遮挡），如果一帧没检测到就结束事件，会把一个连续运动拆成很多碎片。设置 2 秒宽限期，合并更合理。

#### 4.7.4 线程安全

- 数据库连接在主线程创建和打开
- 工作线程需要写库时，通过信号槽将写操作投递到主线程执行（Qt 的数据库连接不能跨线程使用）
- DatabaseManager 在主线程接收信号并执行 SQL，简单且线程安全

---

### 4.8 运动报警与事件快照

#### 4.8.1 报警触发条件

满足以下条件时触发报警：
1. 运动检测器输出至少一个有效目标（面积超过阈值）
2. 目标持续时间超过 1 秒（避免瞬时噪点触发，如飞虫、光影闪烁）
3. 同一通道距离上次报警超过 5 秒（报警冷却时间，避免连续运动反复刷屏）

#### 4.8.2 报警表现形式

| 表现 | 实现方式 | 效果 |
|---|---|---|
| **视频格子边框闪烁** | QTimer 每 500ms 交替切换红色/正常边框样式 | 最直观，一眼看到哪路有情况 |
| **状态栏提示** | `statusBar()->showMessage(...)`，3~5秒自动消失 | 文字提示 |
| **事件快照保存** | 报警时将当前帧（带框）用 `cv::imwrite` 存为 JPG | 事后可查看，路径写入 SQLite |
| **事件列表**（可选） | QDockWidget + QListWidget 显示最近报警 | 加分项，时间够再做 |

#### 4.8.3 快照保存

- 文件命名：`snapshots/cameraX_YYYYMMDD_HHMMSS.jpg`
- 质量参数：`cv::IMWRITE_JPEG_QUALITY = 80`，平衡画质和文件大小
- 快照路径写入 `motion_events.snapshot_path` 字段

---

### 4.9 视频存储与编码

#### 4.9.1 录制流程

使用 OpenCV 的 `cv::VideoWriter` 将处理后的帧写入 MP4 文件：

1. 用户点击"开始录制"，获取当前视频的宽、高、帧率
2. 创建 `VideoWriter`，指定编码器、帧率、分辨率
3. 工作线程每处理一帧，如果正在录制，就调用 `writer.write(frame)`
4. 用户点击"停止录制"，调用 `writer.release()`，文件写入完成
5. 将文件信息写入 SQLite 的 recordings 表

#### 4.9.2 编码器选择

| 编码器 | fourcc | 优点 | 缺点 |
|---|---|---|---|
| H.264 | `avc1` | 压缩率高，文件小，兼容性好 | 需要 OpenCV 编译时启用 FFmpeg，部分平台默认不支持 |
| MPEG-4 | `mp4v` | 所有平台都支持 | 压缩率一般，文件比 H.264 大 2~3 倍 |

**策略**：优先尝试 `avc1`（H.264），如果 `open()` 返回 false，自动回退到 `mp4v`（MPEG-4），保证录制功能在任何平台都能用。

#### 4.9.3 文件命名

```
recordings/camera0_20260902_143000.mp4
recordings/camera1_20260902_143500.mp4
```

文件名包含摄像头编号和时间戳，便于按时间排序和查找。程序启动时自动创建 `recordings/` 和 `snapshots/` 目录。

---

### 4.10 GUI 界面设计

#### 4.10.1 整体窗口结构

主窗口继承 `QMainWindow`，采用"上显示、下控制、底状态"三段式布局：

- **中央区域（上）**：`QGridLayout` 3×3 九宫格，每个格子是自定义 `VideoLabel`，`spacing=4`
- **控制面板（下）**：`QHBoxLayout`，包含通道选择下拉框、RTSP地址输入框、连接/断开按钮、录制按钮、参数按钮、状态标签
- **状态栏（底）**：`QMainWindow` 自带 `statusBar()`，显示系统运行状态、CPU、连接数、录制状态

#### 4.10.2 视频格子（VideoLabel 自定义控件）

每个视频格子封装了单路的显示与状态管理，是 GUI 层最核心的类：

- **成员**：cameraId、isConnected、isRecording、isAlarming、alarmTimer（QTimer）、originalPixmap
- **核心方法**：updateFrame(QImage)、setConnected(bool)、setRecording(bool)、startAlarm()、stopAlarm()、reset()
- **7种状态视觉表现**：未连接（深灰边框+灰色文字）、连接中（黄色边框）、已连接（正常视频）、重连中（黄色闪烁+文字）、连接失败（红色边框+红色文字）、录制中（橙色边框+REC标签）、报警中（红色闪烁+ALARM标签）

#### 4.10.3 报警闪烁实现

不使用复杂动画框架，用最简单的 `QTimer` 交替切换样式表：

- `startAlarm()`：启动 QTimer（500ms），设置 alarmOn=true
- QTimer timeout 槽：alarmOn 取反，交替设置红色边框/正常边框样式
- `stopAlarm()`：停止 QTimer，恢复正常边框

#### 4.10.4 参数调节面板

可折叠的 `QGroupBox`，包含 4 个水平滑块，实时调节运动检测参数：

| 参数 | 范围 | 默认 | 作用 |
|---|---|---|---|
| 检测阈值 | 10~100 | 30 | 帧差灰度阈值 |
| 最小面积 | 100~5000 | 500 | 目标最小像素面积 |
| 跳帧率 | 1~5 | 3 | 每 N 帧做一次检测 |
| 报警冷却 | 1~30秒 | 5 | 同一路两次报警最小间隔 |

滑块值变化时通过信号调用对应通道 VideoWorker 的 setter 方法，参数用 `std::atomic<int>` 存储，线程安全且实时生效。

#### 4.10.5 全屏单路查看

双击任意视频格子进入全屏：

- 创建 `FullScreenDialog`（继承 QDialog），设置 `Qt::FramelessWindowHint` 无边框，`showFullScreen()` 全屏
- Dialog 内放大尺寸的 QLabel，接收对应通道的 frameReady 信号
- 顶部半透明信息栏显示通道号、时间、录制状态
- 按 ESC 或双击退出全屏

#### 4.10.6 样式表（深色主题）

使用 QSS（类似 CSS）统一设置外观，单独存为 `style.qss` 文件：

- 全局背景 `#1e1e1e`，文字 `#e0e0e0`
- 视频格子：黑色背景 + 深灰边框
- 按钮：深灰背景 + hover 变亮 + pressed 变暗 + disabled 灰色
- 录制中按钮：红色高亮（通过动态属性 `[recording="true"]` 切换）
- 滑块：蓝色手柄 + 深灰轨道
- 状态栏：深灰背景 + 顶部边框线

---

## 5. 代码片段解释

### 5.1 两帧差法核心流程

```cpp
std::vector<cv::Rect> MotionDetector::detect(cv::Mat& frame)
{
    // 1. 预处理：转灰度 + 高斯模糊（去噪，避免传感器噪点被误检）
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    // 2. 第一帧只缓存，不检测（没有前一帧可以做差）
    if (!m_hasPrev) {
        m_prevGray = gray.clone();  // clone()深拷贝，避免指向同一块内存
        m_hasPrev = true;
        return {};
    }

    // 3. 两帧差分：absdiff计算逐像素灰度差的绝对值
    cv::Mat diff;
    cv::absdiff(m_prevGray, gray, diff);

    // 4. 二值化：差异>threshold的像素标为255（白色=运动），否则0（黑色=背景）
    cv::Mat binary;
    cv::threshold(diff, binary, m_threshold, 255, cv::THRESH_BINARY);

    // 5. 形态学操作：先腐蚀去孤立噪点，再膨胀连接断裂区域
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::erode(binary, binary, kernel);    // 腐蚀：小白点消失
    cv::dilate(binary, binary, kernel);   // 膨胀：断裂区域连接
    cv::dilate(binary, binary, kernel);   // 再膨胀：补偿腐蚀缩小，框更完整

    // 6. 轮廓检测：提取白色区域的外轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // 7. 面积过滤 + 外接矩形
    std::vector<cv::Rect> result;
    for (const auto& contour : contours) {
        if (cv::contourArea(contour) < m_minArea) continue;  // 太小的是噪点
        cv::Rect bbox = cv::boundingRect(contour);
        result.push_back(bbox);
        cv::rectangle(frame, bbox, cv::Scalar(0, 255, 0), 2);  // 绿色框，线宽2
    }

    // 8. 更新前帧（滑动窗口）
    m_prevGray = gray.clone();
    return result;
}
```

**关键解释**：
- `gray.clone()` 必须深拷贝，因为 `gray` 是局部变量，函数返回后内存释放，如果只赋值（浅拷贝），`m_prevGray` 会指向已释放内存
- 先腐蚀后膨胀的顺序很重要：腐蚀先去掉小噪点，膨胀再把剩下的真实运动区域连接起来；如果先膨胀后腐蚀，噪点会被膨胀放大，腐蚀不掉
- `RETR_EXTERNAL` 只提取最外层轮廓，不提取嵌套的内部轮廓，减少计算量

---

### 5.2 IOU 计算与贪心匹配

```cpp
// IOU（交并比）计算：两个矩形的交集面积 ÷ 并集面积
double Tracker::calculateIoU(const cv::Rect& a, const cv::Rect& b) const
{
    // 交集矩形的左上角（取两个矩形x/y的较大值）
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    // 交集矩形的右下角（取两个矩形右下角的较小值）
    int x2 = std::min(a.x + a.width, b.x + b.width);
    int y2 = std::min(a.y + a.height, b.y + b.height);

    // 交集宽高：如果x2<=x1或y2<=y1，说明不相交，交集为0
    int interW = std::max(0, x2 - x1);
    int interH = std::max(0, y2 - y1);
    double interArea = static_cast<double>(interW * interH);

    // 并集面积 = A面积 + B面积 - 交集面积（交集被算了两次，减去一次）
    double unionArea = static_cast<double>(a.area() + b.area()) - interArea;
    if (unionArea <= 0) return 0.0;

    return interArea / unionArea;
}

// 贪心匹配：对每个已追踪目标，找评分最高的未匹配检测框
for (auto& obj : m_trackedObjects) {
    double bestScore = -1.0;
    int bestDetIdx = -1;

    for (size_t i = 0; i < dets.size(); ++i) {
        if (dets[i].matched) continue;  // 已被其他目标匹配，跳过

        double iou = calculateIoU(obj.bbox, dets[i].bbox);
        if (iou < m_minIou) continue;   // IOU太低，不可能是同一目标

        double dist = cv::norm(obj.centroid - dets[i].centroid);
        if (dist > m_maxCentroidDist) continue;  // 距离太远，排除

        // 综合评分：IOU权重0.7（位置重叠更可靠），距离权重0.3
        double score = iou * 0.7 + (1.0 - dist / m_maxCentroidDist) * 0.3;
        if (score > bestScore) {
            bestScore = score;
            bestDetIdx = static_cast<int>(i);
        }
    }

    if (bestDetIdx >= 0) {
        // 匹配成功：更新目标位置，恢复生命值
        obj.bbox = dets[bestDetIdx].bbox;
        obj.centroid = dets[bestDetIdx].centroid;
        obj.life = m_maxLife;
        obj.hitCount++;
        dets[bestDetIdx].matched = true;  // 标记为已匹配，其他目标不能再用
    } else {
        // 未匹配：生命值递减
        obj.life--;
        obj.hitCount = 0;
    }
}
```

**关键解释**：
- IOU 的交集计算用 `max(0, x2-x1)` 处理不相交的情况，如果不相交，交集宽或高为负数，max 后变为 0
- 贪心匹配不是全局最优（匈牙利算法才是），但简历项目足够用，实现简单，面试时可以讲"为什么用贪心而不是匈牙利——目标数少（<10），贪心精度足够，复杂度 O(n²) 远低于匈牙利的 O(n³)"
- `dets[i].matched = true` 确保一个检测框只能被一个目标匹配，避免两个目标抢同一个框

---

### 5.3 线程安全队列与条件变量

```cpp
template<typename T>
class ThreadSafeQueue {
public:
    // 生产者：推入任务
    void push(T item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 有界队列：满了就丢弃最旧的任务（视频监控场景，旧帧不如新帧有价值）
        if (m_queue.size() >= m_maxSize) {
            m_queue.pop();
        }
        m_queue.push(std::move(item));
        m_cv.notify_one();  // 唤醒一个等待的消费者线程
    }

    // 消费者：阻塞等待并取出任务
    void waitAndPop(T& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        // wait 会自动释放锁并阻塞，被 notify 后重新加锁
        // 第二个参数是谓词（lambda），防止虚假唤醒：被唤醒但队列还是空的，继续等
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_stopped; });
        if (m_stopped && m_queue.empty()) return;
        item = std::move(m_queue.front());
        m_queue.pop();
    }

    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_cv.notify_all();  // 唤醒所有等待线程，让它们检查 stopped 标志并退出
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::queue<T>           m_queue;
    size_t                  m_maxSize = 10;  // 有界队列上限
    bool                    m_stopped = false;
};
```

**关键解释**：
- `std::lock_guard` 用于 push（简单加锁解锁），`std::unique_lock` 用于 waitAndPop（因为 condition_variable::wait 需要 unique_lock，它要临时释放锁）
- `m_cv.wait(lock, predicate)` 的谓词参数非常重要：没有谓词时，线程可能被"虚假唤醒"（操作系统层面的假唤醒，不是真的有新任务），醒来后队列还是空的，直接 pop 会崩溃；有谓词时，wait 内部会循环检查谓词，谓词为 false 就继续等待
- `notify_one` 只唤醒一个线程（有新任务时只需要一个线程来处理），`notify_all` 在 stop 时唤醒所有线程（让所有工作线程都检查 stopped 标志并退出循环）
- 有界队列满了丢弃最旧任务，这是视频监控的特殊设计：如果处理不过来，旧帧已经没有价值（用户要看的是实时画面），丢弃旧帧保证新帧能被及时处理

---

### 5.4 指数退避重连状态机

```cpp
void VideoWorker::run()
{
    cv::Mat frame;
    int reconnectAttempt = 0;
    const int maxReconnect = 10;

    while (m_running) {
        // ===== 状态1：正常抓帧 =====
        if (m_capture.isOpened() && m_capture.read(frame)) {
            reconnectAttempt = 0;  // 成功读到帧，重置重连计数

            // ... 运动检测、追踪、录制、emit信号 ...

            std::this_thread::sleep_for(std::chrono::milliseconds(frameDelay));
            continue;
        }

        // ===== 状态2：抓帧失败，进入重连 =====
        if (reconnectAttempt >= maxReconnect) {
            // 超过最大重试次数，彻底断开
            emit cameraError(m_cameraId, "重连失败，已断开");
            m_running = false;
            break;
        }

        reconnectAttempt++;
        emit reconnecting(m_cameraId, reconnectAttempt);

        // 指数退避：等待时间 = min(3 × 2^(n-1), 60) 秒
        int waitSec = std::min(3 * (1 << (reconnectAttempt - 1)), 60);
        // 分段sleep，每100ms检查一次m_running，避免stop时卡住等待
        for (int i = 0; i < waitSec * 10 && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!m_running) break;

        // ===== 状态3：尝试重连 =====
        m_capture.release();  // 先释放旧连接
        if (m_capture.open(m_rtspUrl)) {
            // 重连成功：重置检测器和追踪器（关键！否则旧缓存帧会导致全画面误检）
            m_detector.reset();
            m_tracker.reset();
            // 清空帧队列
            // ... 回到正常抓帧状态
        }
        // 重连失败：循环回到状态2，继续指数退避
    }
}
```

**关键解释**：
- 指数退避用位运算 `1 << (reconnectAttempt - 1)` 计算 2 的幂，比 pow 函数快且精确
- `std::min(..., 60)` 设置等待上限，避免重试次数多了等几分钟
- sleep 分段（每 100ms 检查一次 `m_running`）很重要：如果用 `sleep_for(seconds(waitSec))`，用户点击停止时，线程会卡在 sleep 里最多等 60 秒才退出，界面假死；分段 sleep 可以在 100ms 内响应停止信号
- 重连成功后必须调用 `m_detector.reset()` 和 `m_tracker.reset()`：因为重连后视频流从新的关键帧开始，画面内容可能完全不同（比如摄像头被移动了），如果用旧的前一帧做差，会产生全画面的运动误检；追踪器里的旧目标 ID 也会和新画面物体错误关联
- `reconnectAttempt` 在成功读到帧时重置为 0：只要有一帧成功，就认为连接恢复了，下次断线时从第 1 次重连开始（等 3 秒），而不是继续之前的高次数（等 60 秒）

---

### 5.5 Mat 转 QImage（深拷贝原因）

```cpp
QImage VideoWorker::matToQImage(const cv::Mat& mat)
{
    if (mat.empty()) return QImage();

    // OpenCV默认BGR，QImage需要RGB，先转通道顺序
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);

    // 构造QImage：传入数据指针、宽、高、每行字节数、格式
    // 注意：这个构造函数不会拷贝数据！QImage内部指针直接指向rgb.data
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);

    // 必须深拷贝！
    // 原因：rgb是局部变量，函数返回后rgb的内存被释放
    // 如果不copy，QImage的内部指针指向已释放内存（野指针）
    // UI线程稍后访问这个QImage显示画面时，就会读到垃圾数据或直接段错误崩溃
    return img.copy();
}
```

**关键解释**：
- `QImage(uchar* data, ...)` 这个构造函数的文档明确说明："QImage does not take ownership of the data, the data must remain valid throughout the lifetime of the QImage"——QImage 不拥有数据，数据必须在 QImage 生命周期内保持有效
- `rgb` 是栈上的局部变量，函数返回时栈帧销毁，`rgb.data` 指向的内存被释放
- `img.copy()` 会分配新内存并把像素数据拷贝过去，QImage 拥有这块新内存的所有权，生命周期独立，跨线程安全
- `rgb.step`（每行字节数）必须传入，不能假设 `width * 3`：OpenCV 的 Mat 每行可能有填充字节（行对齐），`step` 才是真实的每行字节数，直接用 `width * 3` 会导致画面倾斜（颜色错位）

---

### 5.6 报警闪烁（QTimer 样式切换）

```cpp
// VideoLabel.h
class VideoLabel : public QLabel {
    Q_OBJECT
public:
    void startAlarm();
    void stopAlarm();

private slots:
    void onAlarmBlink();  // QTimer timeout 槽函数

private:
    QTimer* m_alarmTimer;
    bool    m_alarmOn;     // 当前闪烁帧是否亮红
    static const QString NORMAL_STYLE;
    static const QString ALARM_STYLE;
};

// VideoLabel.cpp
const QString VideoLabel::NORMAL_STYLE =
    "border: 2px solid #333333; background: black;";
const QString VideoLabel::ALARM_STYLE =
    "border: 3px solid #ff3333; background: black;";

void VideoLabel::startAlarm()
{
    m_alarmOn = true;
    setStyleSheet(ALARM_STYLE);  // 立刻变红
    m_alarmTimer->start(500);    // 每500ms触发一次timeout，即每秒闪烁2次
}

void VideoLabel::onAlarmBlink()
{
    m_alarmOn = !m_alarmOn;  // 交替切换
    setStyleSheet(m_alarmOn ? ALARM_STYLE : NORMAL_STYLE);
}

void VideoLabel::stopAlarm()
{
    m_alarmTimer->stop();     // 停止计时器，不再闪烁
    setStyleSheet(NORMAL_STYLE);  // 恢复正常边框
}
```

**关键解释**：
- 用 QTimer 而不是 QPropertyAnimation（Qt 属性动画）的原因：样式表的 border-color 不是可动画的属性（Q_PROPERTY 没有 MEMBER 声明），用 QTimer 手动切换最简单可靠
- 闪烁频率 500ms（每秒 2 次）是经验值：太慢（如 2 秒）看不出闪烁，太快（如 100ms）眼睛不舒服且占用 UI 线程
- `startAlarm()` 里先 `setStyleSheet(ALARM_STYLE)` 再 `start()`：确保立刻变红，而不是等 500ms 后第一次 timeout 才变红
- `stopAlarm()` 里必须显式设置回 NORMAL_STYLE：因为停止计时器时，当前可能正处于 ALARM_STYLE（红色）状态，如果不恢复，边框会一直是红色
- 边框宽度从 2px 变 3px：报警时稍微加粗，视觉上更醒目，不只是颜色变化

---

## 6. 二人分工方案

### 6.1 分工原则

- **按技术领域划分**：一人偏算法/视频处理，一人偏工程/UI/数据，各自领域内聚，减少跨人依赖
- **接口先行**：开发前对齐所有跨模块信号和类接口，二人并行开发，最后联调
- **工作量均衡**：算法侧涉及性能优化和状态机（技术密度高），工程侧涉及 UI 和数据库（文件数多但逻辑相对简单），工作量大致相当

### 6.2 成员 A：视频处理与算法侧

**负责模块**：`MotionDetector`、`Tracker`、`VideoWorker`、`ThreadPool`

**具体任务**：

| 任务 | 对应技术 | 产出 |
|---|---|---|
| 两帧差法运动检测实现 | 4.2 节 | MotionDetector 类，参数可调 |
| 多目标追踪实现 | 4.3 节 | Tracker 类，ID 持久化 |
| 单路视频处理主循环 | 4.1、4.9 节 | VideoWorker 类，抓帧→检测→追踪→画框→录制 |
| cv::Mat → QImage 转换 | 4.1.2 节 | VideoWorker 内静态方法 |
| 线程池实现 | 4.4 节 | ThreadPool 类，任务队列 + 条件变量 |
| 九路并发优化 | 4.5 节 | 降分辨率 + 跳帧 + UI 节流，集成到 VideoWorker |
| 断线自动重连 | 4.6 节 | 状态机 + 指数退避 + 状态重置，集成到 VideoWorker |
| 运动报警信号触发 | 4.8 节 | 报警条件判断，emit motionDetected 信号 |
| 事件快照保存 | 4.8.3 节 | cv::imwrite 保存 JPG |

**成员 A 的简历描述**：

> 负责视频处理核心链路设计与实现，包括基于两帧差分法的运动检测、基于 IOU 与质心距离的多目标追踪算法；设计线程池任务调度模型替代每路一线程，配合降分辨率与跳帧策略实现 9 路视频并发实时处理；实现摄像头断线指数退避自动重连机制，保证系统在网络不稳定场景下的持续运行。

### 6.3 成员 B：系统工程与 UI 侧

**负责模块**：`CameraManager`、`DatabaseManager`、`MainWindow`、`VideoLabel`、`FullScreenDialog`、CMake 构建配置

**具体任务**：

| 任务 | 对应技术 | 产出 |
|---|---|---|
| Qt 主窗口与 3×3 九宫格 UI | 4.10 节 | MainWindow 类，视频格子 + 控制面板 |
| 视频格子自定义控件 | 4.10.2 节 | VideoLabel 类，状态切换 + 报警闪烁 |
| 全屏单路查看 | 4.10.5 节 | FullScreenDialog 类 |
| 多摄像头管理 | 4.4 节 | CameraManager 类，增删摄像头、生命周期管理 |
| 帧显示与 UI 刷新节流 | 4.5.2 节 | MainWindow 内 onFrameReady 槽，全局帧率限制 |
| SQLite 数据库设计与实现 | 4.7 节 | DatabaseManager 类，两张表 + CRUD 接口 |
| 录制记录写入 | 4.7、4.9 节 | 开始/结束录制时写 recordings 表 |
| 运动事件写入 | 4.7.3 节 | 接收 motionDetected 信号，写 motion_events 表 |
| 运动报警 UI 表现 | 4.8.2 节 | 边框闪烁 + 状态栏提示 + 冷却计时器 |
| 视频录制 UI 控制 | 4.9 节 | 开始/停止录制按钮，文件命名，目录创建 |
| 参数调节面板 | 4.10.4 节 | 4 个滑块，实时调参 |
| 样式表设计 | 4.10.6 节 | style.qss 深色主题 |
| CMakeLists.txt | 2.3 节 | 构建配置，Qt5 Widgets+Sql + OpenCV |
| 程序入口 main.cpp | — | QApplication + MainWindow |

**成员 B 的简历描述**：

> 负责系统架构与工程化实现，包括基于 Qt 的 3×3 多路视频监控界面、自定义视频格子控件（状态管理+报警闪烁）、多摄像头生命周期管理；设计 SQLite 元数据管理模块，建立录制文件表与运动事件表，实现视频片段与运动事件的结构化存储；实现运动报警机制（界面高亮闪烁 + 事件快照）与视频录制功能（H.264/MP4）；完成 CMake 构建系统配置与深色主题样式表。

### 6.4 协作接口（联调时对齐）

两人在开发前必须确认以下接口：

1. **VideoWorker 的信号签名**：
   - `frameReady(int cameraId, QImage frame)`
   - `cameraError(int cameraId, QString message)`
   - `motionDetected(int cameraId, QImage snapshot, int objectCount)`
   - `reconnecting(int cameraId, int attempt)`
2. **CameraManager 的公共方法**：`addCamera(int id, string url)`、`removeCamera(int id)`、`getWorker(int id)`、`stopAll()`
3. **DatabaseManager 的公共方法**：`open()`、`addRecording()`、`finishRecording()`、`addMotionEvent()`
4. **视频格子编号**：0~8，3×3 网格行优先（0=左上，8=右下）
5. **文件路径约定**：录制存 `recordings/`，快照存 `snapshots/`，数据库 `surveillance.db`
6. **参数默认值**：检测阈值 30、最小面积 500、处理分辨率 320×240、跳帧间隔 3、目标帧率 15fps

---

## 7. 开发阶段与里程碑

> 总工期：3~4 周（业余时间，二人并行）

### 阶段一：基础框架（第 1 周）

**目标**：单路视频能显示、能检测、能追踪，项目能编译运行

| 成员 A | 成员 B |
|---|---|
| MotionDetector 两帧差法实现 | CMakeLists.txt + main.cpp 项目骨架 |
| Tracker 追踪算法实现 | style.qss 深色主题基础版 |
| VideoWorker 主循环（抓帧→检测→追踪→emit） | MainWindow 基础 UI（单视频格子 + 连接按钮） |
| 单路本地视频测试通过 | VideoLabel 基础版（帧显示 + 连接状态） |
| | CameraManager 基础版（单路增删） |
| | 单路帧显示联调通过 |

**里程碑**：打开本地视频文件，界面显示视频画面，运动物体有绿色框和 ID。

### 阶段二：核心功能（第 2 周）

**目标**：多路显示、录制、九路优化、重连，系统核心功能完整

| 成员 A | 成员 B |
|---|---|
| ThreadPool 线程池实现 | MainWindow 扩展为 3×3 九宫格 |
| 降分辨率 + 跳帧优化 | CameraManager 支持多路管理 |
| 断线重连状态机 + 指数退避 | VideoLabel 完善（7种状态 + 报警闪烁） |
| 九路压力测试与调参 | FullScreenDialog 全屏查看 |
| 重连状态重置（detector/tracker reset） | VideoWriter 录制功能集成 |
| | UI 刷新节流实现 |
| | 录制文件命名与目录管理 |

**里程碑**：同时打开 9 路本地视频，九宫格流畅显示，运动检测正常，手动录制生成可播放 MP4，断开视频源后自动重连。

### 阶段三：数据与报警（第 3 周）

**目标**：SQLite 元数据、运动报警、事件快照、参数调节，功能全部完成

| 成员 A | 成员 B |
|---|---|
| 报警条件判断逻辑（持续时间 + 冷却） | DatabaseManager 设计与实现（建表 + 接口） |
| motionDetected 信号 emit | 录制记录写入（开始/结束更新） |
| 事件快照保存（cv::imwrite） | 运动事件写入（接收信号 + 宽限期合并） |
| 报警与快照联调 | 报警 UI 表现（边框闪烁 + 状态栏） |
| 参数 setter 方法（atomic 线程安全） | 参数调节面板（4个滑块） |
| 全链路测试 | 数据库查询验证（sqlite3 命令行检查） |
| | 样式表完善（录制高亮、滑块样式） |

**里程碑**：检测到运动时界面报警闪烁，快照自动保存，SQLite 中有对应的录制记录和运动事件记录，参数滑块实时调节生效。

### 阶段四：优化与文档（第 4 周）

**目标**：性能调优、bug 修复、简历材料准备

| 成员 A | 成员 B |
|---|---|
| 参数最终调优（阈值/面积/跳帧率） | UI 细节打磨（样式、提示、图标） |
| 极端场景测试（9 路长时间运行） | 存储空间检查与提示 |
| 内存泄漏检查（Valgrind） | 代码整理与注释 |
| 性能数据记录（CPU/帧率，写简历用） | README 编写 |
| 简历技术描述撰写 | 简历项目描述撰写 |
| | 录屏演示视频 |

**里程碑**：项目稳定运行，简历材料就绪，可录屏演示。

---

> 文档结束
