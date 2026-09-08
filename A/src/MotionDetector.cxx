// 运动检测
// 两帧差分法实现（成员A）
// 灰度→模糊→帧差→二值化→形态学（腐蚀→膨胀×2）→轮廓→面积过滤→外接矩形

#include "MotionDetector.h"

MotionDetector::MotionDetector(int threshold, int minArea)
    : m_threshold(threshold)
    , m_minArea(minArea)
    , m_hasPrev(false) {}

std::vector<cv::Rect> MotionDetector::detect(const cv::Mat& frame)
{
    std::vector<cv::Rect> result;

    if (frame.empty()) {
        return result;
    }

    // 1. 预处理：转灰度 + 高斯模糊去噪（避免传感器噪点/压缩伪影被误检）
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame;
    }
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    // 2. 第一帧只缓存不检测：没有前一帧可以做差
    if (!m_hasPrev) {
        m_prevGray = gray.clone();   // clone() 深拷贝，避免与局部变量共享内存
        m_hasPrev = true;
        return result;
    }

    // 3. 两帧差分：absdiff 计算逐像素灰度差的绝对值
    cv::Mat diff;
    cv::absdiff(m_prevGray, gray, diff);

    // 4. 二值化：差异 > threshold 的像素置 255（运动），否则 0（背景）
    cv::Mat binary;
    cv::threshold(diff, binary, m_threshold, 255, cv::THRESH_BINARY);

    // 5. 形态学：先腐蚀去孤立噪点，再膨胀两次连接断裂的运动区域
    //    （先腐蚀后膨胀顺序很重要；膨胀两次补偿腐蚀造成的缩小，框更完整）
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::erode(binary, binary, kernel);
    cv::dilate(binary, binary, kernel);
    cv::dilate(binary, binary, kernel);

    // 6. 轮廓提取：只取最外层轮廓（RETR_EXTERNAL），减少计算量
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // 7. 面积过滤 + 外接矩形
    for (const auto& contour : contours) {
        if (cv::contourArea(contour) < m_minArea) {
            continue;   // 太小的轮廓视为噪点
        }
        result.push_back(cv::boundingRect(contour));
    }

    // 8. 滑动窗口：把当前帧作为下一次检测的前一帧
    m_prevGray = gray.clone();

    return result;
}

void MotionDetector::reset()
{
    m_hasPrev = false;
    m_prevGray.release();
}
