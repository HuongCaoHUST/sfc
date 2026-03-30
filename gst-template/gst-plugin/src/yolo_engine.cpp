#include "yolo_engine.h"
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>

// Constructor: Loads the model and initializes the session.
YoloEngine::YoloEngine(const std::string& model_path, bool use_gpu) : env(ORT_LOGGING_LEVEL_WARNING, "YOLO_Engine") {
    Ort::SessionOptions session_options;

    if (use_gpu) {
        // OrtSessionOptionsAppendExecutionProvider_CUDA(session_options, 0);
    }
    
    session = new Ort::Session(env, model_path.c_str(), session_options);
    
    Ort::AllocatorWithDefaultOptions allocator;

    // Get input and output names
    // Note: This assumes single input/output models.
    auto input_name = session->GetInputNameAllocated(0, allocator);
    input_names_char.push_back(strdup(input_name.get()));
    auto output_name = session->GetOutputNameAllocated(0, allocator);
    output_names_char.push_back(strdup(output_name.get()));
    
    // Get input shape
    Ort::TypeInfo input_type_info = session->GetInputTypeInfo(0);
    auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
    input_shape = tensor_info.GetShape();
    
    // Allow for dynamic batch and spatial sizes
    if (input_shape[0] < 1) input_shape[0] = 1; 
    if (input_shape[2] < 1) input_shape[2] = 640;
    if (input_shape[3] < 1) input_shape[3] = 640;
}

// Destructor
YoloEngine::~YoloEngine() {
    for (auto p : input_names_char) {
        free((void*)p);
    }
    input_names_char.clear();
    for (auto p : output_names_char) {
        free((void*)p);
    }
    output_names_char.clear();

    if (session) {
        delete session;
        session = nullptr;
    }
}

// Part 1: Pre-process frame and run the first stage of the model
std::vector<float> YoloEngine::run_part1(cv::Mat& frame) {
    if (!session) {
        throw std::runtime_error("Session is not initialized.");
    }

    // Pre-processing
    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1.0 / 255.0, cv::Size((int)input_shape[3], (int)input_shape[2]), cv::Scalar(), true, false);

    // Create input tensor
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, blob.ptr<float>(), blob.total(), input_shape.data(), input_shape.size());

    // Run inference
    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1
    );

    // Get output
    const float* raw_data = output_tensors[0].GetTensorData<float>();
    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    size_t output_size = std::accumulate(output_shape.begin(), output_shape.end(), 1, std::multiplies<int64_t>());
    
    return std::vector<float>(raw_data, raw_data + output_size);
}

// Part 2: Run second stage and perform post-processing (NMS)
std::vector<Detection> YoloEngine::run_part2_and_postprocess(const float* tensor_data, size_t tensor_size, 
    int frame_width, int frame_height, float conf_threshold, float nms_threshold) {
    std::vector<Detection> results;
    if (!session) {
        throw std::runtime_error("Session is not initialized.");
    }

    // Wrap the input data in an Ort::Value without copying
    // This assumes the input to part2 has a known shape. We'll need to get it from the model itself.
    Ort::TypeInfo input_type_info = session->GetInputTypeInfo(0);
    auto input_tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
    auto part2_input_shape = input_tensor_info.GetShape();
    if (part2_input_shape.empty() || part2_input_shape[0] < 1) { // Dynamic shape
        part2_input_shape = {1, 84, 8400}; // A common YOLOv8 output shape, may need adjustment
    }
    size_t expected_size = std::accumulate(part2_input_shape.begin(), part2_input_shape.end(), 1, std::multiplies<int64_t>());
    if (tensor_size != expected_size) {
         // Potentially handle dynamic shapes more gracefully here
         part2_input_shape[2] = tensor_size / (part2_input_shape[0] * part2_input_shape[1]);
    }
    

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, const_cast<float*>(tensor_data), tensor_size, part2_input_shape.data(), part2_input_shape.size());

    // Run inference for part 2
    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1);

    // Post-processing
    const float* raw_output = output_tensors[0].GetTensorData<float>();
    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    
    int num_detections = static_cast<int>(output_shape[2]);
    int num_components = static_cast<int>(output_shape[1]);

    cv::Mat output_mat(num_components, num_detections, CV_32F, (float*)raw_output);
    output_mat = output_mat.t();

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    float scale_x = (float)frame_width / input_shape[3];
    float scale_y = (float)frame_height / input_shape[2];

    for (int i = 0; i < output_mat.rows; i++) {
        float* row = output_mat.ptr<float>(i);
        cv::Mat scores(1, num_components - 4, CV_32F, row + 4);
        cv::Point class_id_point;
        double max_score;
        cv::minMaxLoc(scores, nullptr, &max_score, nullptr, &class_id_point);

        if (max_score >= conf_threshold) {
            confidences.push_back(static_cast<float>(max_score));
            class_ids.push_back(class_id_point.x);

            float cx = row[0];
            float cy = row[1];
            float w = row[2];
            float h = row[3];

            int left = static_cast<int>((cx - 0.5 * w) * scale_x);
            int top = static_cast<int>((cy - 0.5 * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);

            boxes.push_back(cv::Rect(left, top, width, height));
        }
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold, nms_threshold, nms_indices);

    for (int idx : nms_indices) {
        results.push_back({boxes[idx], confidences[idx], class_ids[idx]});
    }
    
    return results; 
}

// For single-shot models (legacy plugins)
std::vector<Detection> YoloEngine::detect(cv::Mat& frame, float conf_threshold, float nms_threshold) {
    if (!session) {
        throw std::runtime_error("Session is not initialized for single-shot detection.");
    }

    // --- Pre-processing ---
    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1.0 / 255.0, cv::Size((int)input_shape[3], (int)input_shape[2]), cv::Scalar(), true, false);

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, blob.ptr<float>(), blob.total(), input_shape.data(), input_shape.size());

    // --- Inference ---
    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1
    );

    // --- Post-processing ---
    // This is essentially the same as run_part2_and_postprocess, but uses the output_tensors from this function's inference run
    std::vector<Detection> results;
    const float* raw_output = output_tensors[0].GetTensorData<float>();
    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    
    int num_detections = static_cast<int>(output_shape[2]);
    int num_components = static_cast<int>(output_shape[1]);

    cv::Mat output_mat(num_components, num_detections, CV_32F, (float*)raw_output);
    output_mat = output_mat.t();

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    float scale_x = (float)frame.cols / input_shape[3];
    float scale_y = (float)frame.rows / input_shape[2];

    for (int i = 0; i < output_mat.rows; i++) {
        float* row = output_mat.ptr<float>(i);
        cv::Mat scores(1, num_components - 4, CV_32F, row + 4);
        cv::Point class_id_point;
        double max_score;
        cv::minMaxLoc(scores, nullptr, &max_score, nullptr, &class_id_point);

        if (max_score >= conf_threshold) {
            confidences.push_back(static_cast<float>(max_score));
            class_ids.push_back(class_id_point.x);

            float cx = row[0];
            float cy = row[1];
            float w = row[2];
            float h = row[3];

            int left = static_cast<int>((cx - 0.5 * w) * scale_x);
            int top = static_cast<int>((cy - 0.5 * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);

            boxes.push_back(cv::Rect(left, top, width, height));
        }
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold, nms_threshold, nms_indices);

    for (int idx : nms_indices) {
        results.push_back({boxes[idx], confidences[idx], class_ids[idx]});
    }
    
    return results; 
}
