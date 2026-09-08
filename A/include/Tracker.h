#ifndef TRACKER_H
#define TRACKER_H

// 目标追踪
// 基于质心距离与 IOU 的贪心多目标匹配、ID 持久化、生命周期管理
// 成员A（README 4.3 / 5.2）

#include <vector>

#include <opencv2/core.hpp>

// 单个被追踪目标的状态
struct TrackedObject {
    int          id;          // 全局唯一编号，从 0 递增，匹配期间保持不变
    cv::Rect     bbox;        // 当前帧的外接矩形（检测平面坐标）
    cv::Point2f  centroid;    // 质心（bbox 中心）
    int          life;        // 剩余生命值，连续未匹配时递减，到 0 移除
    int          hitCount;    // 连续匹配帧数，新目标 <2 时不画 ID，避免闪烁
};

class Tracker {
public:
    // maxLife        ：目标最长可"失联"帧数（短暂遮挡后还能找回同一 ID）
    // minIou         ：IOU 低于此值即排除，不可能是同一目标（默认 0.1）
    // maxCentroidDist：质心距离超过此值即排除（默认 100，检测平面坐标）
    explicit Tracker(int maxLife = 12, double minIou = 0.1,
                     double maxCentroidDist = 100.0);

    // 用本帧检测框更新所有已追踪目标；返回更新后的存活目标列表（引用内部表）。
    // detections 为检测平面坐标下的 bbox。调用后可用 objects() 取目标绘制。
    const std::vector<TrackedObject>& update(const std::vector<cv::Rect>& detections);

    // 当前所有存活目标
    const std::vector<TrackedObject>& objects() const { return m_objects; }
    bool empty() const { return m_objects.empty(); }
    int  objectCount() const { return static_cast<int>(m_objects.size()); }

    // 清空所有追踪目标与 ID 计数器（重连成功后调用，README 4.6.4）
    void reset();

    // IOU（交并比）= 交集面积 / 并集面积
    static double calculateIoU(const cv::Rect& a, const cv::Rect& b);

    int nextId() const { return m_nextId; }
    int maxLife() const { return m_maxLife; }

private:
    // 单个候选检测框（含是否已被某目标占用的标记，保证一个框只被匹配一次）
    struct Candidate {
        cv::Rect    bbox;
        cv::Point2f centroid;
        bool        used = false;
    };

    int         m_maxLife;
    double      m_minIou;
    double      m_maxCentroidDist;
    int         m_nextId = 0;
    std::vector<TrackedObject> m_objects;
};

#endif // TRACKER_H
