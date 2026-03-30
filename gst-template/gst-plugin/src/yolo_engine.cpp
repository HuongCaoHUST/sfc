#include "yolo_engine.h"
#include <numeric>
#include <algorithm>

YoloDetector::YoloDetector() : env(ORT_LOGGING_LEVEL_WARNING, "YOLO_Gst") {}

YoloDetector::~YoloDetector() {
    if (session) {
        delete session;
        session = nullptr;
    }
}

bool YoloDetector::load_model(const std::string& model_path, bool use_gpu) {
    Ort::SessionOptions session_options;

    if (use_gpu) {
        // This requires the onnxruntime-gpu package and a compatible CUDA setup.
        // OrtSessionOptionsAppendExecutionProvider_CUDA(session_options, 0);
    }
    
    try {
        // Using smart pointers for automatic memory management
        session = new Ort::Session(env, model_path.c_str(), session_options);
        
        Ort::AllocatorWithDefaultOptions allocator;

        // Get input and output names
        auto input_name_ptr = session->GetInputNameAllocated(0, allocator);
        input_names.push_back(input_name_ptr.get());

        auto output_name_ptr = session->GetOutputNameAllocated(0, allocator);
        output_names.push_back(output_name_ptr.get());
        
        // Get input shape
        Ort::TypeInfo input_type_info = session->GetInputTypeInfo(0);
        auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
        input_shape = tensor_info.GetShape();
        
        if (input_shape[0] == -1) input_shape[0] = 1; // Batch size
        if (input_shape[2] == -1) input_shape[2] = 640; // Height
        if (input_shape[3] == -1) input_shape[3] = 640; // Width

        return true;
    } catch (const Ort::Exception& e) {
        return false;
    }
}

std::vector<Detection> YoloDetector::detect(cv::Mat& frame, float conf_threshold) {
    std::vector<Detection> results;
    if (!session) return results;

    // Pre-processing
    int img_w = frame.cols;
    int img_h = frame.rows;
    int net_w = (int)input_shape[3];
    int net_h = (int)input_shape[2];

    float scale_x = (float)img_w / net_w;
    float scale_y = (float)img_h / net_h;

    // Convert image to blob
    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1.0 / 255.0, cv::Size(), cv::Scalar(), true, false);

    // Create input tensor
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, blob.ptr<float>(), blob.total(), input_shape.data(), input_shape.size());

    // Create temporary vectors
    std::vector<const char*> input_names_char;
    input_names_char.reserve(input_names.size());
    for (const auto& s : input_names) {
        input_names_char.push_back(s.c_str());
    }

    std::vector<const char*> output_names_char;
    output_names_char.reserve(output_names.size());
    for (const auto& s : output_names) {
        output_names_char.push_back(s.c_str());
    }

    // Inference
    auto output_tensors = session->Run(
        Ort::RunOptions{nullptr}, 
        input_names_char.data(), &input_tensor, 1, 
        output_names_char.data(), 1
    );

    // Post-processing
    const float* raw_data = output_tensors[0].GetTensorData<float>();
    auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
    
    int num_detections = (int)output_shape[2];
    int num_components = (int)output_shape[1];
    cv::Mat output_mat(num_components, num_detections, CV_32F, (float*)raw_data);
    output_mat = output_mat.t(); // Transpose to [num_detections, num_components]

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    for (int i = 0; i < output_mat.rows; i++) {
        float* row = output_mat.ptr<float>(i);
        
        cv::Mat scores(1, num_components - 4, CV_32F, row + 4);
        cv::Point class_id_point;
        double max_score;
        cv::minMaxLoc(scores, 0, &max_score, 0, &class_id_point);

        if (max_score >= conf_threshold) {
            confidences.push_back((float)max_score);
            class_ids.push_back(class_id_point.x);

            float cx = row[0];
            float cy = row[1];
            float w = row[2];
            float h = row[3];

            int left = (int)((cx - 0.5 * w) * scale_x);
            int top = (int)((cy - 0.5 * h) * scale_y);
            int width = (int)(w * scale_x);
            int height = (int)(h * scale_y);

            boxes.push_back(cv::Rect(left, top, width, height));
        }
    }

    // Non-Maximum Suppression
    std::vector<int> nms_indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold, 0.45f, nms_indices);

    for (int idx : nms_indices) {
        Detection det;
        det.box = boxes[idx];
        det.confidence = confidences[idx];
        det.class_id = class_ids[idx];
        results.push_back(det);
    }
    
    return results; 
}

void YoloDetector::preprocess(cv::Mat& frame, float* blob) {
}
