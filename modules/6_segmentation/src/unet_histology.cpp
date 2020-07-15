#include "segmentation.hpp"

#include <opencv2/core/utils/filesystem.hpp>

using namespace InferenceEngine;
using namespace cv;
using namespace cv::utils::fs;

Blob::Ptr wrapMatToBlob(const Mat& m) {
    CV_Assert(m.depth() == CV_32F);
    std::vector<size_t> dims(&m.size[0], &m.size[0] + m.dims);
    return make_shared_blob<float>(TensorDesc(Precision::FP32, dims, Layout::ANY),
                                   (float*)m.data);
}

UNetHistology::UNetHistology() {
    Core ie;

    // Load deep learning network into memory
    CNNNetwork net = ie.ReadNetwork(join(DATA_FOLDER, "frozen_unet_histology.xml"),
                                    join(DATA_FOLDER, "frozen_unet_histology.bin"));

    outputName = net.getOutputsInfo().begin()->first;

    // Initialize runnable object on CPU device
    ExecutableNetwork execNet = ie.LoadNetwork(net, "CPU");

    // Create a single processing thread
    req = execNet.CreateInferRequest();
}

void UNetHistology::bgr2rgb(const Mat& src, Mat& dst) {
    cvtColor(src, dst, COLOR_BGR2RGB);
}


void UNetHistology::normalize(const Mat& src, Mat& dst) {
    Scalar mean, std;
    meanStdDev(src, mean, std);
    dst = Mat::zeros(src.size(), CV_32FC3);
    dst += src;
    dst -= mean;
    dst /= std;
}

void UNetHistology::segment(const Mat& image, Mat& mask) {
    // Preprocessing
    Mat rgbImage;
    bgr2rgb(image, rgbImage);

    Mat paddedImage;
    padMinimum(rgbImage, 92, 92, paddedImage);

    Mat normalized;
    normalize(paddedImage, normalized);

    // Sanity checks before hardcoded dimensions usage
    static const int width = 1144;
    static const int height = 952;
    CV_CheckEQ(normalized.cols, width, "UNetHistology input width");
    CV_CheckEQ(normalized.rows, height, "UNetHistology input height");
    CV_CheckEQ(normalized.channels(), 3, "UNetHistology input channels");

    // Perform data permutation from 952x1144x3 to 3x952x1144
    Mat inp({1, 3, height, width, 1, 1}, CV_32F);
    std::vector<Mat> channels(3);
    for (int i = 0; i < 3; ++i)
        channels[i] = Mat(height, width, CV_32FC1, inp.ptr<float>(0, i));
    split(normalized, channels);

    Blob::Ptr inputBlob = wrapMatToBlob(inp);

    req.SetBlob("worker_0/validation/IteratorGetNext", inputBlob);

    req.Infer();
 
    const int outHeight = 772, outWidth = 964;
    Mat res = Mat::zeros(outHeight, outWidth, CV_8UC1);
    int* output = req.GetBlob(outputName)->buffer();
    for (int i = 0; i < outHeight; i++) {
        for (int j = 0; j < outWidth; j++) {
            res.at<uint8_t>(i, j) = output[i * outWidth + j];
        }
    }
    resize(res, mask, image.size());
}

int UNetHistology::countGlands(const cv::Mat& segm) {
    Mat gray, thresh, dist, sure_bg, sure_fg;
    threshold(segm, thresh, 0, 255, THRESH_BINARY_INV + THRESH_OTSU);
    Mat kernel = Mat::ones(3, 3, CV_8U);
    morphologyEx(thresh, thresh, MORPH_CLOSE, kernel, Point(-1, -1), 3);
    distanceTransform(thresh, dist, DIST_L2, 5);
    double maxVal = 0.0;
    minMaxLoc(dist, 0, &maxVal);
    threshold(dist, sure_fg, 0.45 * maxVal, 255, 0);

    cv::Mat sure_fg_8u;
    sure_fg.convertTo(sure_fg_8u, CV_8U);

    std::vector<std::vector<cv::Point> > contours;
    findContours(sure_fg_8u, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    return contours.size();

}

void UNetHistology::padMinimum(const Mat& src, int width, int height, Mat& dst) {
    Mat minRow, minCol, globalMin;

    reduce(src, minRow, 0, REDUCE_MIN);
    reduce(src, minCol, 1, REDUCE_MIN);
    reduce(minCol, globalMin, 0, REDUCE_MIN);
    minRow = repeat(minRow, height, 1);
    minCol = repeat(minCol, 1, width);

    dst = repeat(globalMin, src.rows + 2*height, src.cols + 2*width);
    minRow.copyTo(dst.colRange(width, dst.cols - width).rowRange(0, height));
    minRow.copyTo(dst.colRange(width, dst.cols - width).rowRange(dst.rows - height, dst.rows));
    minCol.copyTo(dst.colRange(0, width).rowRange(height, dst.rows - height));
    minCol.copyTo(dst.colRange(dst.cols - width, dst.cols).rowRange(height, dst.rows - height));
    src.copyTo(dst.colRange(width, dst.cols - width).rowRange(height, dst.rows - height));
}
