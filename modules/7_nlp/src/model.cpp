#include "model.hpp"

#include <fstream>
#include <sstream>

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <inference_engine.hpp>

using namespace InferenceEngine;
using namespace cv;
using namespace cv::utils::fs;

Blob::Ptr wrapVecToBlob(std::vector<int> v) {
    std::vector<size_t> dims = {1, v.size()};
    return make_shared_blob<int32_t>(TensorDesc(Precision::I32, dims, Layout::NC), (int*)v.data());
}

SQuADModel::SQuADModel() : tokenizer(join(DATA_FOLDER, "bert-large-uncased-vocab.txt")) {
    Core ie;

    // Load deep learning network into memory
    CNNNetwork net = ie.ReadNetwork(join(DATA_FOLDER, "distilbert.xml"),
                                    join(DATA_FOLDER, "distilbert.bin"));
    InputInfo::Ptr inputInfo = net.getInputsInfo()["input.1"];
   // inputInfo->setLayout(Layout::HW);
    inputInfo->setPrecision(Precision::I32);
    outputName = net.getOutputsInfo().begin()->first;
    // Initialize runnable object on CPU device
    ExecutableNetwork execNet = ie.LoadNetwork(net, "CPU");
    // Create a single processing thread
    req = execNet.CreateInferRequest();
}

std::string SQuADModel::getAnswer(const std::string& question, const std::string& source) {
    std::vector<std::string> questionTokens = tokenizer.tokenize(question);
    std::vector<std::string> sourceTokens = tokenizer.tokenize(source);
    std::vector<std::string> tokens;
    tokens.push_back("[CLS]");
    tokens.reserve(tokens.size() + questionTokens.size());
    tokens.insert(tokens.end(), questionTokens.begin(), questionTokens.end());

    tokens.push_back("[SEP]");
    tokens.reserve(tokens.size() + sourceTokens.size());
    tokens.insert(tokens.end(), sourceTokens.begin(), sourceTokens.end());
    tokens.push_back("[SEP]");

    std::vector<int> indices = tokenizer.tokensToIndices(tokens);
    Blob::Ptr q = wrapVecToBlob(indices);
    req.SetBlob("input.1", q);
    req.Infer(); 
    int* output1 = req.GetBlob(outputName)->buffer();
    int* output2 = req.GetBlob("Squeeze_438")->buffer();
    return "";
}
