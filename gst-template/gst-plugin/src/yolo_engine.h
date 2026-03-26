#ifndef __YOLO_DETECTION_H__
#define __YOLO_DETECTION_H__

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>

struct Detection {
    cv::Rect box;
    float confidence;
    int class_id;
};

class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();

    bool load_model(const std::string& model_path, bool use_gpu = false);
    std::vector<Detection> detect(cv::Mat& frame, float conf_threshold = 0.5f);

private:
    Ort::Env env;
    Ort::Session* session = nullptr;
    Ort::RunOptions run_options;
    
    std::vector<int64_t> input_shape;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;

    void preprocess(cv::Mat& frame, float* blob);
};

#endif