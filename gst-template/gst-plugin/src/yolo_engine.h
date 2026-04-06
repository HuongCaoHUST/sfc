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

class YoloEngine {
public:
    YoloEngine(const std::string& model_path, bool use_gpu = false, int gpu_device_id = 0);
    ~YoloEngine();

    // For 2-part models
    std::vector<float> run_part1(cv::Mat& frame);
    std::vector<Detection> run_part2_and_postprocess(const float* tensor_data, size_t tensor_size,
        int frame_width, int frame_height, float conf_threshold = 0.5f, float nms_threshold = 0.45f);

    // For batched inference (N frames at once)
    std::vector<std::vector<Detection>> detect_batch(std::vector<cv::Mat>& frames,
        float conf_threshold = 0.5f, float nms_threshold = 0.45f);

    // For single-shot models (legacy plugins)
    std::vector<Detection> detect(cv::Mat& frame, float conf_threshold = 0.5f, float nms_threshold = 0.45f);

private:
    Ort::Env env;
    Ort::Session* session = nullptr;
    
    std::vector<const char*> input_names_char;
    std::vector<const char*> output_names_char;
    std::vector<int64_t> input_shape;
};

#endif