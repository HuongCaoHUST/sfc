#include "yolo_engine.h"
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <opencv2/opencv.hpp>

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

        OrtCUDAProviderOptions cuda_opts;
        cuda_opts.device_id = gpu_device_id;
        cuda_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
        cuda_opts.arena_extend_strategy = 0;

        // Removed cv::cuda::setDevice(gpu_device_id); to avoid OpenCV CUDA dependencies.
        // ONNXRuntime execution providers handle CUDA context natively.

        try {
            session_options.AppendExecutionProvider_CUDA(cuda_opts);

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
    if (input_shape.size() >= 4) {
        if (input_shape[2] < 1) input_shape[2] = 640;
        if (input_shape[3] < 1) input_shape[3] = 640;
    }
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

    int input_h = (int)input_shape[2];
    int input_w = (int)input_shape[3];

    // Pre-processing
    cv::Mat resized_frame, rgb_frame, float_frame;
    cv::resize(frame, resized_frame, cv::Size(input_w, input_h));
    cv::cvtColor(resized_frame, rgb_frame, cv::COLOR_BGR2RGB);
    rgb_frame.convertTo(float_frame, CV_32F, 1.0f / 255.0f);

    std::vector<cv::Mat> chw;
    cv::split(float_frame, chw);

    std::vector<float> input_tensor_values;
    input_tensor_values.reserve(3 * input_w * input_h);
    for (int i = 0; i < 3; ++i) {
        input_tensor_values.insert(input_tensor_values.end(), (float*)chw[i].datastart, (float*)chw[i].dataend);
    }

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

    // Run inference and let ONNXRuntime allocate the output
    auto output_tensors = session->Run(Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), 1);

    auto& output_tensor = output_tensors[0];
    float* out_data = output_tensor.GetTensorMutableData<float>();
    size_t out_size = output_tensor.GetTensorTypeAndShapeInfo().GetElementCount();

    // Copy to persistent float vector buffer
    return std::vector<float>(out_data, out_data + out_size);
}

// Part 2: Run second stage and perform post-processing (NMS)
std::vector<Detection> YoloEngine::run_part2_and_postprocess(const float* tensor_data, size_t tensor_size,
    int frame_width, int frame_height, float conf_threshold, float nms_threshold) {
    
    if (!session) {
        throw std::runtime_error("Session is not initialized.");
    }

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

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<float> input_copy(tensor_data, tensor_data + tensor_size);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_copy.data(), input_copy.size(),
        part2_input_shape.data(), part2_input_shape.size());

    auto output_tensors = session->Run(Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), 1);

    auto& output_tensor = output_tensors[0];
    auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
    float* output_data = output_tensor.GetTensorMutableData<float>();

    int num_components = static_cast<int>(output_shape[1]);
    int num_detections = static_cast<int>(output_shape[2]);

    std::vector<Detection> results;
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    const float MODEL_BASE_SIZE = 640.0f;
    float scale_x = (float)frame_width / MODEL_BASE_SIZE;
    float scale_y = (float)frame_height / MODEL_BASE_SIZE;

    for (int i = 0; i < num_detections; ++i) {
        float max_conf = 0.0f;
        int best_class_id = -1;

        for (int c = 4; c < num_components; ++c) {
            float conf = output_data[c * num_detections + i];
            if (conf > max_conf) {
                max_conf = conf;
                best_class_id = c - 4;
            }
        }

        if (max_conf >= conf_threshold) {
            float cx = output_data[0 * num_detections + i];
            float cy = output_data[1 * num_detections + i];
            float w = output_data[2 * num_detections + i];
            float h = output_data[3 * num_detections + i];

            int left = static_cast<int>((cx - 0.5f * w) * scale_x);
            int top = static_cast<int>((cy - 0.5f * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);

            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(max_conf);
            class_ids.push_back(best_class_id);
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
    int input_h = (int)input_shape[2];
    int input_w = (int)input_shape[3];

    // --- Pre-processing (batch) ---
    std::vector<float> input_tensor_values;
    input_tensor_values.reserve(N * 3 * input_h * input_w);

    for (int n = 0; n < N; ++n) {
        cv::Mat resized_frame, rgb_frame, float_frame;
        cv::resize(frames[n], resized_frame, cv::Size(input_w, input_h));
        cv::cvtColor(resized_frame, rgb_frame, cv::COLOR_BGR2RGB);
        rgb_frame.convertTo(float_frame, CV_32F, 1.0f / 255.0f);

        std::vector<cv::Mat> chw;
        cv::split(float_frame, chw);
        for (int i = 0; i < 3; ++i) {
            input_tensor_values.insert(input_tensor_values.end(), 
                (float*)chw[i].datastart, (float*)chw[i].dataend);
        }
    }

    std::vector<int64_t> batch_shape = input_shape;
    batch_shape[0] = static_cast<int64_t>(N);

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(),
        batch_shape.data(), batch_shape.size());

    // --- Inference ---
    auto output_tensors = session->Run(Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1);

    Ort::Value& output_tensor = output_tensors[0];
    auto output_tensor_info = output_tensor.GetTensorTypeAndShapeInfo();
    auto batch_output_shape = output_tensor_info.GetShape();
    float* output_data_ptr = output_tensor.GetTensorMutableData<float>();

    // --- Post-processing ---
    int num_components = static_cast<int>(batch_output_shape[1]);
    int num_detections = static_cast<int>(batch_output_shape[2]);
    size_t per_image_floats = static_cast<size_t>(num_components) * num_detections;

    std::vector<std::vector<Detection>> all_results(N);

    for (int img = 0; img < N; img++) {
        float* img_data = output_data_ptr + img * per_image_floats;
        
        float scale_x = (float)frames[img].cols / input_w;
        float scale_y = (float)frames[img].rows / input_h;

        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> class_ids;

        for (int i = 0; i < num_detections; ++i) {
            float max_conf = 0.0f;
            int best_class_id = -1;

            for (int c = 4; c < num_components; ++c) {
                float conf = img_data[c * num_detections + i];
                if (conf > max_conf) {
                    max_conf = conf;
                    best_class_id = c - 4;
                }
            }

            if (max_conf >= conf_threshold) {
                float cx = img_data[0 * num_detections + i];
                float cy = img_data[1 * num_detections + i];
                float w = img_data[2 * num_detections + i];
                float h = img_data[3 * num_detections + i];

                int left = static_cast<int>((cx - 0.5f * w) * scale_x);
                int top = static_cast<int>((cy - 0.5f * h) * scale_y);
                int width = static_cast<int>(w * scale_x);
                int height = static_cast<int>(h * scale_y);

                boxes.push_back(cv::Rect(left, top, width, height));
                confidences.push_back(max_conf);
                class_ids.push_back(best_class_id);
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

    int input_h = (int)input_shape[2];
    int input_w = (int)input_shape[3];

    // --- Pre-processing ---
    cv::Mat resized_frame, rgb_frame, float_frame;
    cv::resize(frame, resized_frame, cv::Size(input_w, input_h));
    cv::cvtColor(resized_frame, rgb_frame, cv::COLOR_BGR2RGB);
    rgb_frame.convertTo(float_frame, CV_32F, 1.0f / 255.0f);

    std::vector<cv::Mat> chw;
    cv::split(float_frame, chw);

    std::vector<float> input_tensor_values;
    input_tensor_values.reserve(3 * input_h * input_w);
    for (int i = 0; i < 3; ++i) {
        input_tensor_values.insert(input_tensor_values.end(), (float*)chw[i].datastart, (float*)chw[i].dataend);
    }

    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

    // --- Inference ---
    auto output_tensors = session->Run(Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1);

    Ort::Value& output_tensor = output_tensors[0];
    auto output_tensor_info = output_tensor.GetTensorTypeAndShapeInfo();
    auto output_shape = output_tensor_info.GetShape();
    float* output_data = output_tensor.GetTensorMutableData<float>();

    // --- Post-processing ---
    std::vector<Detection> results;

    int num_components = static_cast<int>(output_shape[1]);
    int num_detections = static_cast<int>(output_shape[2]);

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    float scale_x = (float)frame.cols / input_w;
    float scale_y = (float)frame.rows / input_h;

    for (int i = 0; i < num_detections; ++i) {
        float max_conf = 0.0f;
        int best_class_id = -1;

        for (int c = 4; c < num_components; ++c) {
            float conf = output_data[c * num_detections + i];
            if (conf > max_conf) {
                max_conf = conf;
                best_class_id = c - 4;
            }
        }

        if (max_conf >= conf_threshold) {
            float cx = output_data[0 * num_detections + i];
            float cy = output_data[1 * num_detections + i];
            float w = output_data[2 * num_detections + i];
            float h = output_data[3 * num_detections + i];

            int left = static_cast<int>((cx - 0.5f * w) * scale_x);
            int top = static_cast<int>((cy - 0.5f * h) * scale_y);
            int width = static_cast<int>(w * scale_x);
            int height = static_cast<int>(h * scale_y);

            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(max_conf);
            class_ids.push_back(best_class_id);
        }
    }

    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold, nms_threshold, nms_indices);

    for (int idx : nms_indices) {
        results.push_back({boxes[idx], confidences[idx], class_ids[idx]});
    }

    return results;
}
