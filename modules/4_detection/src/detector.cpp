#include "detector.hpp"

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <inference_engine.hpp>

using namespace cv;
using namespace InferenceEngine;
using namespace cv::utils::fs;

Blob::Ptr wrapMatToBlob(const Mat& m) {
    CV_Assert(m.depth() == CV_8U);
    std::vector<size_t> dims = { 1, (size_t)m.channels(), (size_t)m.rows, (size_t)m.cols };
    return make_shared_blob<uint8_t>(TensorDesc(Precision::U8, dims, Layout::NHWC),
        m.data);
}

Detector::Detector() {

    Core ie;

    // Load deep learning network into memory
    auto net = ie.ReadNetwork(utils::fs::join(DATA_FOLDER, "face-detection-0104.xml"),
        utils::fs::join(DATA_FOLDER, "face-detection-0104.bin"));
    InputInfo::Ptr inputInfo = net.getInputsInfo()["image"];
    inputInfo->getPreProcess().setResizeAlgorithm(ResizeAlgorithm::RESIZE_BILINEAR);
    inputInfo->setLayout(Layout::NHWC);
    inputInfo->setPrecision(Precision::U8);

    ExecutableNetwork execNet = ie.LoadNetwork(net, "CPU");
    req = execNet.CreateInferRequest();
    outputName = net.getOutputsInfo().begin()->first;
}


void Detector::detect(const cv::Mat& image,
                      float nmsThreshold,
                      float probThreshold,
                      std::vector<cv::Rect>& boxes,
                      std::vector<float>& probabilities,
                      std::vector<unsigned>& classes) {
    Blob::Ptr input = wrapMatToBlob(image);
    req.SetBlob("image", input);
    req.Infer();
    float* output = req.GetBlob(outputName)->buffer();

    size_t n = req.GetBlob(outputName)->size();
    const int imageWidth = image.cols, imageHeight = image.rows;
    std::vector<cv::Rect> preboxes;
    std::vector<float> preprobabilities;
    std::vector<unsigned> preclasses;

    for (int i = 0; i < n / 7; i++) {
        if (output[i * 7 + 2] < probThreshold) {
            continue;
        } 
        int xmin = (int)(output[i * 7 + 3] * imageWidth);
        int xmax = (int)(output[i * 7 + 5] * imageWidth);
        int ymin = (int)(output[i * 7 + 4] * imageHeight);
        int ymax = (int)(output[i * 7 + 6] * imageHeight);
        int width = xmax - xmin + 1;
        int height = ymax - ymin + 1;
        preboxes.push_back(Rect(xmin, ymin, width, height));
        preprobabilities.push_back(output[i * 7 + 2]);
        preclasses.push_back(output[i * 7 + 1]);
    }

    std::vector<unsigned> indices;
    nms(preboxes, preprobabilities, nmsThreshold, indices);

    n = indices.size();
    boxes = std::vector<cv::Rect>(n);
    probabilities = std::vector<float>(n);
    classes = std::vector<unsigned>(n);
    for (int i = 0; i < n; i++) {
        boxes[i] = preboxes[indices[i]];
        probabilities[i] = preprobabilities[indices[i]];
        classes[i] = preclasses[indices[i]];
    }
}


void nms(const std::vector<cv::Rect>& boxes, const std::vector<float>& probabilities,
         float threshold, std::vector<unsigned>& indices) {
    size_t n = boxes.size();
    std::set<size_t> remainingIndices;
    for (size_t i = 0; i < n; i++) {
        remainingIndices.insert(i);
    }
    while (!remainingIndices.empty()) {
        size_t indMaxProb = 0;
        float maxProb = 0.0f;
        for (auto i : remainingIndices) {
            if (probabilities[i] > maxProb) {
                maxProb = probabilities[i];
                indMaxProb = i;
            }
        }

        remainingIndices.erase(indMaxProb);
        indices.push_back(indMaxProb);

        for (auto i : remainingIndices) {
            if (iou(boxes[indMaxProb], boxes[i]) > threshold) {
                remainingIndices.erase(i);
            }
        }
    }
}

float iou(const cv::Rect& a, const cv::Rect& b) {
    return (float)(a & b).area() / (float)(a.area() + b.area() - (a & b).area());
}
