#include "yolo_engine.h"
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>

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
        memset(&cuda_opts, 0, sizeof(cuda_opts));
        cuda_opts.device_id = gpu_device_id;

        // Try to add CUDA provider
        try {
            session_options.AppendExecutionProvider_CUDA(cuda_opts);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to add CUDA provider: " + std::string(e.what()));
        }
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

    // Create input tensor with appropriate memory location
    Ort::MemoryInfo memory_info = use_gpu
        ? Ort::MemoryInfo::CreateCuda(OrtArenaAllocator, OrtMemTypeDefault, gpu_device_id)
        : Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

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
    if (!raw_data) {
        throw std::runtime_error("Failed to get output tensor data - tensor may be on GPU without proper access");
    }

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

    // Create input tensor with appropriate memory location
    Ort::MemoryInfo memory_info = use_gpu
        ? Ort::MemoryInfo::CreateCuda(OrtArenaAllocator, OrtMemTypeDefault, gpu_device_id)
        : Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, const_cast<float*>(tensor_data), tensor_size, part2_input_shape.data(), part2_input_shape.size());

    // Run inference for part 2
    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), 1);

    // Post-processing
    const float* raw_output = output_tensors[0].GetTensorData<float>();
    if (!raw_output) {
        throw std::runtime_error("Failed to get output tensor data");
    }

    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

    int num_detections = static_cast<int>(output_shape[2]);
    int num_components = static_cast<int>(output_shape[1]);

    // Copy GPU data to CPU if needed before using OpenCV
    std::vector<float> cpu_data;
    size_t total_floats = static_cast<size_t>(num_components) * num_detections;
    cpu_data.assign(raw_output, raw_output + total_floats);

    cv::Mat output_mat(num_components, num_detections, CV_32F, cpu_data.data());
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
    Ort::MemoryInfo memory_info = use_gpu
        ? Ort::MemoryInfo::CreateCuda(OrtArenaAllocator, OrtMemTypeDefault, gpu_device_id)
        : Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, blob.ptr<float>(), blob.total(),
        batch_shape.data(), batch_shape.size());

    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), 1);

    // --- Post-processing (per image) ---
    const float* raw_output = output_tensors[0].GetTensorData<float>();
    if (!raw_output) {
        throw std::runtime_error("Failed to get batch output tensor data");
    }

    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

    int num_components = static_cast<int>(output_shape[1]); // 84
    int num_detections = static_cast<int>(output_shape[2]); // 8400
    size_t per_image_floats = static_cast<size_t>(num_components) * num_detections;

    // Copy all GPU data to CPU at once if needed
    std::vector<float> cpu_output;
    size_t total_floats = N * per_image_floats;
    cpu_output.assign(raw_output, raw_output + total_floats);

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

    // Use appropriate memory allocation
    Ort::MemoryInfo memory_info = use_gpu
        ? Ort::MemoryInfo::CreateCuda(OrtArenaAllocator, OrtMemTypeDefault, gpu_device_id)
        : Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, blob.ptr<float>(), blob.total(), input_shape.data(), input_shape.size());

    // --- Inference ---
    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr},
        input_names_char.data(), &input_tensor, 1,
        output_names_char.data(), 1
    );

    // --- Post-processing ---
    std::vector<Detection> results;
    const float* raw_output = output_tensors[0].GetTensorData<float>();
    if (!raw_output) {
        throw std::runtime_error("Failed to get single-shot output tensor data");
    }

    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

    int num_detections = static_cast<int>(output_shape[2]);
    int num_components = static_cast<int>(output_shape[1]);

    // Copy GPU data to CPU if needed before using OpenCV
    std::vector<float> cpu_data;
    size_t total_floats = static_cast<size_t>(num_components) * num_detections;
    cpu_data.assign(raw_output, raw_output + total_floats);

    cv::Mat output_mat(num_components, num_detections, CV_32F, cpu_data.data());
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
