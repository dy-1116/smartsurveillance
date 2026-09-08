cmake_minimum_required(VERSION 3.16)
project(A_Modules VERSION 0.1 LANGUAGES CXX)

# ============================================================================
# A 部分（成员 A）独立构建与自测骨架
#   - 4 个模块：MotionDetector / Tracker / VideoWorker / ThreadPool
#   - 本 CMake 只构建 A 部分 + 冒烟测试 a_smoke，方便本机验证；
#     源码保持 Qt5/Qt6 兼容，日后可直接并入 B 侧的 Qt5 工程（把下面
#     Qt 组件换成 Qt5 Widgets/Sql 并在 include 路径中加入 A/include 即可）。
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 优先 Qt6，找不到则回退 Qt5（VideoWorker 只需 Core+Gui）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui)
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio highgui)

message(STATUS "A 部分将使用 Qt${QT_VERSION_MAJOR} 与 OpenCV ${OpenCV_VERSION} 构建")

add_executable(a_smoke
    include/MotionDetector.h
    src/MotionDetector.cxx
    include/Tracker.h
    src/Tracker.cxx
    include/VideoWorker.h
    src/VideoWorker.cxx
    include/ThreadPool.h
    src/ThreadPool.cxx
    test/a_smoke.cxx
)

target_include_directories(a_smoke PRIVATE include)
target_link_libraries(a_smoke PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    ${OpenCV_LIBS}
)
