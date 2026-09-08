// 目标追踪
// IOU 计算、贪心匹配、ID 分配、生命周期管理实现（成员A）
// README 4.3 / 5.2

#include "Tracker.h"

#include <algorithm>
#include <cmath>

Tracker::Tracker(int maxLife, double minIou, double maxCentroidDist)
    : m_maxLife(maxLife)
    , m_minIou(minIou)
    , m_maxCentroidDist(maxCentroidDist)
    , m_nextId(0) {}

// IOU = 交集面积 / (A 面积 + B 面积 - 交集面积)
double Tracker::calculateIoU(const cv::Rect& a, const cv::Rect& b)
{
    // 交集矩形的左上角：取两个矩形 x/y 的较大值
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    // 交集矩形的右下角：取两个矩形右下角的较小值
    int x2 = std::min(a.x + a.width, b.x + b.width);
    int y2 = std::min(a.y + a.height, b.y + b.height);

    // 不相交时 interW/interH <= 0，max 归零
    int interW = std::max(0, x2 - x1);
    int interH = std::max(0, y2 - y1);
    double interArea = static_cast<double>(interW) * static_cast<double>(interH);

    // 并集面积：交集被两个矩形各算了一次，需减去一次
    double unionArea = static_cast<double>(a.area() + b.area()) - interArea;
    if (unionArea <= 0.0) {
        return 0.0;
    }
    return interArea / unionArea;
}

const std::vector<TrackedObject>& Tracker::update(const std::vector<cv::Rect>& detections)
{
    // 1. 把本帧检测框组织成候选列表（含质心、used 标记）
    std::vector<Candidate> candidates;
    candidates.reserve(detections.size());
    for (const auto& bbox : detections) {
        Candidate c;
        c.bbox = bbox;
        c.centroid = cv::Point2f(bbox.x + bbox.width * 0.5f,
                                 bbox.y + bbox.height * 0.5f);
        candidates.push_back(c);
    }

    // 2. 贪心匹配：对每个已追踪目标，找评分最高的"未被占用"候选框
    //    score = IOU*0.7 + (1 - 质心距离/最大距离)*0.3，IOU 权重更高。
    //    目标数少（<10）时贪心精度足够，复杂度 O(n²)，无需匈牙利算法。
    for (auto& obj : m_objects) {
        double bestScore = -1.0;
        int    bestIdx   = -1;

        for (size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].used) {
                continue;   // 已被其他目标匹配，跳过
            }

            double iou = calculateIoU(obj.bbox, candidates[i].bbox);
            if (iou < m_minIou) {
                continue;   // 重叠太低，不可能是同一目标
            }

            double dist = cv::norm(obj.centroid - candidates[i].centroid);
            if (dist > m_maxCentroidDist) {
                continue;   // 质心距离太远，排除
            }

            double score = iou * 0.7 + (1.0 - dist / m_maxCentroidDist) * 0.3;
            if (score > bestScore) {
                bestScore = score;
                bestIdx = static_cast<int>(i);
            }
        }

        if (bestIdx >= 0) {
            // 匹配成功：更新位置、质心，恢复生命值
            obj.bbox = candidates[bestIdx].bbox;
            obj.centroid = candidates[bestIdx].centroid;
            obj.life = m_maxLife;
            ++obj.hitCount;
            candidates[bestIdx].used = true;   // 该框不能再被其他目标使用
        } else {
            // 未匹配：生命值递减（短暂遮挡期间 ID 不立即丢失）
            --obj.life;
            obj.hitCount = 0;
        }
    }

    // 3. 过滤已消亡目标（life <= 0）
    std::vector<TrackedObject> next;
    next.reserve(m_objects.size() + candidates.size());
    for (const auto& obj : m_objects) {
        if (obj.life > 0) {
            next.push_back(obj);
        }
    }

    // 4. 未被任何目标匹配的候选框 → 新建目标，分配新 ID
    for (const auto& c : candidates) {
        if (c.used) {
            continue;
        }
        TrackedObject t;
        t.id       = m_nextId++;
        t.bbox     = c.bbox;
        t.centroid = c.centroid;
        t.life     = m_maxLife;
        t.hitCount = 1;    // hitCount < 2 时上层不画 ID，避免新目标标签闪烁
        next.push_back(t);
    }

    // 5. 按 ID 升序，保证绘制顺序稳定
    std::sort(next.begin(), next.end(),
              [](const TrackedObject& a, const TrackedObject& b) { return a.id < b.id; });

    m_objects = std::move(next);
    return m_objects;
}

void Tracker::reset()
{
    m_objects.clear();
    m_nextId = 0;
}
