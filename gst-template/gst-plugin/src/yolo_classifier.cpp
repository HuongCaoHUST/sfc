#include "yolo_engine.h"
#include <numeric>
#include <algorithm>
#include <vector>

// YoloClassifier implementation
void softmax(float* data, int size) {
    if (size <= 0) return;
    float max_val = *std::max_element(data, data + size);
    float sum = 0.0;
    std::vector<float> exp_data(size);
    for (int i = 0; i < size; ++i) {
        exp_data[i] = exp(data[i] - max_val);
        sum += exp_data[i];
    }
    if (sum == 0) return;
    for (int i = 0; i < size; ++i) {
        data[i] = exp_data[i] / sum;
    }
}

std::vector<Classification> YoloClassifier::classify(cv::Mat& frame, float conf_threshold) {
    std::vector<Classification> results;
    if (!session) return results;

    int net_w = (int)input_shape[3];
    int net_h = (int)input_shape[2];

    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1.0 / 255.0, cv::Size(net_w, net_h), cv::Scalar(), true, false);

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, blob.ptr<float>(), blob.total(), input_shape.data(), input_shape.size());

    std::vector<const char*> input_names_char;
    for (const auto& s : input_names) {
        input_names_char.push_back(s.c_str());
    }

    std::vector<const char*> output_names_char;
    for (const auto& s : output_names) {
        output_names_char.push_back(s.c_str());
    }

    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1
    );

    float* raw_data = output_tensors[0].GetTensorMutableData<float>();
    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    
    int num_classes = (int)output_shape[1];

    softmax(raw_data, num_classes);
    
    for (int i = 0; i < num_classes; ++i) {
        if (raw_data[i] > conf_threshold) {
            Classification cls;
            cls.confidence = raw_data[i];
            cls.class_id = i;
            results.push_back(cls);
        }
    }
    
    // Sort by confidence
    std::sort(results.begin(), results.end(), [](const Classification& a, const Classification& b) {
        return a.confidence > b.confidence;
    });

    if (results.size() > 3) {
        results.resize(3);
    }

    return results; 
}
