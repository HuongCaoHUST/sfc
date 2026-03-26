#ifndef __YOLO_ENGINE_H__
#define __YOLO_ENGINE_H__

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>

// Struct for detection results
struct Detection {
    cv::Rect box;
    float confidence;
    int class_id;
};

// Struct for classification results
struct Classification {
    float confidence;
    int class_id;
};

// Base class for YOLO models
class YoloEngine {
public:
    YoloEngine();
    virtual ~YoloEngine();

    bool load_model(const std::string& model_path, bool use_gpu = false);

protected:
    Ort::Env env;
    Ort::Session* session = nullptr;
    Ort::RunOptions run_options;
    
    std::vector<int64_t> input_shape;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;

    void preprocess(cv::Mat& frame, float* blob);
};

// Derived class for YOLO detection
class YoloDetector : public YoloEngine {
public:
    std::vector<Detection> detect(cv::Mat& frame, float conf_threshold = 0.5f);
};

// Derived class for YOLO classification
class YoloClassifier : public YoloEngine {
public:
    std::vector<Classification> classify(cv::Mat& frame, float conf_threshold = 0.5f);
};

#endif