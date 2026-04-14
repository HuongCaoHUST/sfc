#include "yolo_engine.h"
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <opencv2/core/cuda.hpp>

// Constructor: Loads the model and initializes the session.
YoloEngine::YoloEngine(const std::string& model_path, bool use_gpu, int gpu_device_id) : env(ORT_LOGGING_LEVEL_WARNING, "YOLO_Engine") {
    this->use_gpu = use_gpu;
    this->gpu_device_id = gpu_device_id;

    Ort::SessionOptions session_options;

    if (use_gpu) {
        if (gpu_device_id < 0 || gpu_device_id > 15) {
            throw std::runtime_error("Invalid GPU device ID: " + std::to_string(gpu_device_id) +
                                   " (valid range: 0-15)");
        }

        OrtCUDAProviderOptions cuda_opts = {};  // Proper initialization instead of memset
        cuda_opts.device_id = gpu_device_id;
        cuda_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
        cuda_opts.arena_extend_strategy = 0;

        // Lỗi 4: Missing CUDA context initialization
        cv::cuda::setDevice(gpu_device_id);

        // Try to add CUDA provider
        try {
            session_options.AppendExecutionProvider_CUDA(cuda_opts);

            // Lỗi 3: Không kiểm tra CUDA provider có được load thành công không
            std::vector<std::string> available_providers = Ort::GetAvailableProviders();
            auto cuda_it = std::find(available_providers.begin(), available_providers.end(), "CUDAExecutionProvider");
            if (cuda_it == available_providers.end()) {
                throw std::runtime_error("CUDA Execution Provider is not available in your ONNX Runtime build.");
            }
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to add CUDA provider: " + std::string(e.what()));
        }
    }

    session = new Ort::Session(env, model_path.c_str(), session_options);
    
    Ort::AllocatorWithDefaultOptions allocator;

    // Get input and output names
    // Note: This assumes single input/output models.
    // Lỗi 5: Fix memory leak (don't use strdup, use std::string vector)
    auto input_name = session->GetInputNameAllocated(0, allocator);
    input_names_str.push_back(std::string(input_name.get()));
    input_names_char.push_back(input_names_str.back().c_str());
    
    auto output_name = session->GetOutputNameAllocated(0, allocator);
    output_names_str.push_back(std::string(output_name.get()));
    output_names_char.push_back(output_names_str.back().c_str());
    
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
    input_names_char.clear();
    output_names_char.clear();
    input_names_str.clear();
    output_names_str.clear();

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
    // Lỗi 2: Fix data lifetime issue with blob.ptr<float>() by buffering
    std::vector<float> input_tensor_values(blob.ptr<float>(), blob.ptr<float>() + blob.total());
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

    // Get expected output shape from input info
    Ort::TypeInfo output_type_info = session->GetOutputTypeInfo(0);
    auto output_tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
    auto output_shape = output_tensor_info.GetShape();

    // Calculate output size, handling dynamic dimensions
    size_t output_size = 1;
    for (int64_t dim : output_shape) {
        if (dim > 0) {
            output_size *= dim;
        } else {
            // If dynamic dimension, estimate based on YOLO structure
            output_size = 1 * 84 * 8400;  // Single image YOLO output
            break;
        }
    }

    // Pre-allocate CPU output tensor
    std::vector<float> cpu_output;
    try {
        cpu_output.resize(output_size);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to allocate part1 output: " + std::string(e.what()));
    }

    Ort::Value output_tensor = Ort::Value::CreateTensor<float>(
        memory_info, cpu_output.data(), cpu_output.size(),
        output_shape.data(), output_shape.size());

    // Run inference with pre-allocated CPU output
    session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), &output_tensor, 1
    );

    // Output is now safely in CPU memory
    return cpu_output;
}

// Part 2: Run second stage and perform post-processing (NMS)
std::vector<Detection> YoloEngine::run_part2_and_postprocess(const float* tensor_data, size_t tensor_size,
    int frame_width, int frame_height, float conf_threshold, float nms_threshold) {
    std::vector<Detection> results;
    if (!session) {
        throw std::runtime_error("Session is not initialized.");
    }

    // Get shape from model
    Ort::TypeInfo input_type_info = session->GetInputTypeInfo(0);
    auto input_tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
    auto part2_input_shape = input_tensor_info.GetShape();
    if (part2_input_shape.empty() || part2_input_shape[0] < 1) {
        part2_input_shape = {1, 84, 8400};
    }
    size_t expected_size = std::accumulate(part2_input_shape.begin(), part2_input_shape.end(), 1, std::multiplies<int64_t>());
    if (tensor_size != expected_size) {
        part2_input_shape[2] = tensor_size / (part2_input_shape[0] * part2_input_shape[1]);
    }

    // Create CPU memory for input
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Note: tensor_data is const, but ONNX Runtime input expects non-const
    // This is safe as we're just reading during inference
    std::vector<float> input_copy(tensor_data, tensor_data + tensor_size);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_copy.data(), input_copy.size(),
        part2_input_shape.data(), part2_input_shape.size());

    // Get expected output shape
    Ort::TypeInfo output_type_info = session->GetOutputTypeInfo(0);
    auto output_tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
    auto output_shape = output_tensor_info.GetShape();

    // Calculate output size, handling dynamic dimensions
    size_t output_size = 1;
    for (int64_t dim : output_shape) {
        if (dim > 0) {
            output_size *= dim;
        } else {
            // If dynamic dimension, estimate
            output_size = 1 * 84 * 8400;
            break;
        }
    }

    // Pre-allocate CPU output tensor
    std::vector<float> cpu_output;
    try {
        cpu_output.resize(output_size);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to allocate part2 output: " + std::string(e.what()));
    }

    Ort::Value output_tensor = Ort::Value::CreateTensor<float>(
        memory_info, cpu_output.data(), cpu_output.size(),
        output_shape.data(), output_shape.size());

    // Run inference with pre-allocated CPU output
    session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), &output_tensor, 1);

    // Post-processing with guaranteed CPU data
    int num_detections = static_cast<int>(output_shape[2]);
    int num_components = static_cast<int>(output_shape[1]);

    cv::Mat output_mat(num_components, num_detections, CV_32F, cpu_output.data());
    output_mat = output_mat.t();

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    const float MODEL_BASE_SIZE = 640.0f;
    float scale_x = (float)frame_width / MODEL_BASE_SIZE;
    float scale_y = (float)frame_height / MODEL_BASE_SIZE;

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

// For batched inference (N frames at once)
std::vector<std::vector<Detection>> YoloEngine::detect_batch(
    std::vector<cv::Mat>& frames, float conf_threshold, float nms_threshold) {

    if (!session || frames.empty()) {
        return {};
    }

    int N = static_cast<int>(frames.size());

    // --- Pre-processing (batch) ---
    cv::Mat blob;
    cv::dnn::blobFromImages(frames, blob, 1.0 / 255.0,
        cv::Size((int)input_shape[3], (int)input_shape[2]),
        cv::Scalar(), true, false);

    // Local copy of shape with batch dimension set to N
    std::vector<int64_t> batch_shape = input_shape;
    batch_shape[0] = static_cast<int64_t>(N);

    // --- Inference ---
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    // Lỗi 2: Fix data lifetime issue with blob.ptr<float>() by buffering
    std::vector<float> input_tensor_values(blob.ptr<float>(), blob.ptr<float>() + blob.total());
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(),
        batch_shape.data(), batch_shape.size());

    // Get expected output shape
    Ort::TypeInfo output_type_info = session->GetOutputTypeInfo(0);
    auto output_tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
    auto output_shape = output_tensor_info.GetShape();

    // Handle dynamic dimensions in output shape
    // YOLO output is typically [batch_size, 84, 8400]
    // If batch dimension is dynamic (-1), replace with actual N
    std::vector<int64_t> batch_output_shape = output_shape;
    if (batch_output_shape[0] <= 0) {
        batch_output_shape[0] = N;  // Fix dynamic batch dimension
    }

    // Calculate output size, handling dynamic dimensions
    // For YOLO: guaranteed to be [N, 84, 8400]
    size_t output_size = 1;
    for (int64_t dim : batch_output_shape) {
        if (dim > 0) {
            output_size *= dim;
        } else {
            // If still has negative dimension, estimate based on YOLO structure
            // Assume 84 classes+coords, 8400 predictions per image
            output_size = N * 84 * 8400;
            break;
        }
    }

    // Pre-allocate CPU output tensor - ONNX Runtime will copy GPU output here
    std::vector<float> cpu_output;
    try {
        cpu_output.resize(output_size);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to allocate output buffer: " + std::string(e.what()) +
                               " (size: " + std::to_string(output_size) + ")");
    }

    Ort::Value output_tensor = Ort::Value::CreateTensor<float>(
        memory_info, cpu_output.data(), cpu_output.size(),
        batch_output_shape.data(), batch_output_shape.size());

    // Run batch inference with pre-allocated CPU output
    session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), &output_tensor, 1);

    // --- Post-processing (per image) with guaranteed CPU data ---
    int num_components = static_cast<int>(batch_output_shape[1]);
    int num_detections = static_cast<int>(batch_output_shape[2]);
    size_t per_image_floats = static_cast<size_t>(num_components) * num_detections;

    std::vector<std::vector<Detection>> all_results(N);

    for (int img = 0; img < N; img++) {
        float* img_data = cpu_output.data() + img * per_image_floats;

        cv::Mat output_mat(num_components, num_detections, CV_32F, img_data);
        output_mat = output_mat.t();

        float scale_x = (float)frames[img].cols / input_shape[3];
        float scale_y = (float)frames[img].rows / input_shape[2];

        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> class_ids;

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
            all_results[img].push_back({boxes[idx], confidences[idx], class_ids[idx]});
        }
    }

    return all_results;
}

// For single-shot models (legacy plugins)
std::vector<Detection> YoloEngine::detect(cv::Mat& frame, float conf_threshold, float nms_threshold) {
    if (!session) {
        throw std::runtime_error("Session is not initialized for single-shot detection.");
    }

    // --- Pre-processing ---
    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1.0 / 255.0, cv::Size((int)input_shape[3], (int)input_shape[2]), cv::Scalar(), true, false);

    // Create input tensor
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    // Lỗi 2: Fix data lifetime issue with blob.ptr<float>() by buffering
    std::vector<float> input_tensor_values(blob.ptr<float>(), blob.ptr<float>() + blob.total());
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

    // Get expected output shape
    Ort::TypeInfo output_type_info = session->GetOutputTypeInfo(0);
    auto output_tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
    auto output_shape = output_tensor_info.GetShape();

    // Calculate output size, handling dynamic dimensions
    size_t output_size = 1;
    for (int64_t dim : output_shape) {
        if (dim > 0) {
            output_size *= dim;
        } else {
            // If dynamic dimension, estimate
            output_size = 1 * 84 * 8400;
            break;
        }
    }

    // Pre-allocate CPU output tensor
    std::vector<float> cpu_output;
    try {
        cpu_output.resize(output_size);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to allocate detect output: " + std::string(e.what()));
    }

    Ort::Value output_tensor = Ort::Value::CreateTensor<float>(
        memory_info, cpu_output.data(), cpu_output.size(),
        output_shape.data(), output_shape.size());

    // --- Inference with pre-allocated CPU output ---
    session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), &output_tensor, 1
    );

    // --- Post-processing with guaranteed CPU data ---
    std::vector<Detection> results;

    int num_detections = static_cast<int>(output_shape[2]);
    int num_components = static_cast<int>(output_shape[1]);

    cv::Mat output_mat(num_components, num_detections, CV_32F, cpu_output.data());
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
