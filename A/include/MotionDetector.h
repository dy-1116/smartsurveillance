#ifndef MOTION_DETECTOR_H
#define MOTION_DETECTOR_H

// 运动检测
// 两帧差分法：灰度→高斯模糊→帧差→二值化→形态学→轮廓→外接矩形
// 成员A（README 4.2 / 5.1）

#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

class MotionDetector {
public:
    // threshold：帧差灰度阈值（默认 30）
    // minArea  ：轮廓最小面积，单位是"检测所用帧的像素"（默认 150）。
    //            检测通常在降采样后的小分辨率帧上进行（README 4.5.4），
    //            320x240 平面下 500 已过大，经验值 100~200 更合适。
    explicit MotionDetector(int threshold = 30, int minArea = 150);

    // 对一帧做运动检测，返回面积达标目标的 bbox 列表（检测平面坐标）。
    // 说明：本类只负责"算出框"，不负责绘制；最终叠加框由上层在
    // 原始分辨率帧上统一绘制，避免在小图上画了又放大导致双重绘制。
    // 首帧只会缓存灰度、不产出任何框（没有前一帧可做差）。
    std::vector<cv::Rect> detect(const cv::Mat& frame);

    void setThreshold(int threshold) { m_threshold = threshold; }
    void setMinArea(int minArea)     { m_minArea = minArea; }
    int  threshold() const { return m_threshold; }
    int  minArea() const   { return m_minArea; }

    // 切换视频源 / 重连成功后调用：清空缓存的前一帧。
    // 否则新流第一帧与旧流最后一帧做差会产生全画面运动误检（README 4.6.4）。
    void reset();

private:
    int     m_threshold;
    int     m_minArea;
    cv::Mat m_prevGray;   // 上一检测帧的灰度图（深拷贝，独立内存）
    bool    m_hasPrev;
};

#endif // MOTION_DETECTOR_H
