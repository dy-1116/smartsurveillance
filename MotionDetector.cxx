// 运动检测
// 灰度→模糊→帧差→二值化→形态学→轮廓→画框




#include "include.h"

using namespace std;
using namespace cv;


int main(int argc, char const *argv[])
{
    VideoCapture cap(0, CAP_ANY);
    if (!cap.isOpened())
    {
        cerr << "open error" << endl;
        return -1;
    }

    Mat frame, prevFrame, grayFrame, grayPrev, diff;
    cap.read(prevFrame);
    cvtColor(prevFrame, grayPrev, COLOR_BGR2GRAY);

    while (true)
    {
        cap.read(frame);
        if (frame.empty())
            break;

        cvtColor(frame, grayFrame, COLOR_BGR2GRAY);
        GaussianBlur(grayFrame, grayFrame, Size(3, 3), 0);

        absdiff(grayFrame, grayPrev, diff);

        threshold(diff, diff, 30, 255, THRESH_BINARY);

        Mat clear = getStructuringElement(MORPH_RECT, Size(3, 3));
        erode(diff, diff, clear);
        dilate(diff, diff, clear);


         vector<vector<Point>> contours;
        findContours(diff, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
        for (auto &c : contours)
        {
            if (contourArea(c) < 100)
                continue;
            Rect bbox = boundingRect(c);

            rectangle(frame, bbox, Scalar(0, 0, 255), 2);
        }

        grayPrev = grayFrame.clone();

        imshow("in", frame);
        imshow("out", diff);

        if (waitKey(1) == 'q')
            break;
    }
    cap.release();
    destroyAllWindows();

    return 0;

    }
