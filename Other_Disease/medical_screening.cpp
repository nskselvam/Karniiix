#include "../Brain_tumour/BrainTumourCNN.hpp"

#define KARNIX_SPINE_CLI_RAW_IMAGE_DEFINED
#include "../karnix_spine_edge/medical/SpineCNN.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string option_value(int argc, char** argv, const std::string& name) {
    for (int index = 2; index + 1 < argc; ++index) {
        if (argv[index] == name) return argv[index + 1];
    }
    return {};
}

static std::string uppercase(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }
    return value;
}

static std::string normalize_disease(const std::string& value) {
    const std::string disease = uppercase(value);
    if (disease == "BRAIN_TUMOR" || disease == "BRAIN-TUMOR" ||
        disease == "BRAIN_TUMOUR" || disease == "BRAIN-TUMOUR") {
        return "BRAIN_TUMOR";
    }
    if (disease == "OSTEOARTHRITIS") return disease;
    if (disease == "OSTEOPOROSIS") return disease;
    throw std::runtime_error(
        "Unsupported disease. Choose brain_tumor, osteoarthritis, or osteoporosis.");
}

static const char* required_modality(const std::string& disease) {
    if (disease == "BRAIN_TUMOR") return "MRI";
    if (disease == "OSTEOARTHRITIS") return "XRAY";
    return "DEXA";
}

static void predict_brain_tumor(const std::string& model_path,
                                const std::string& image_path) {
    const std::vector<std::string> labels = {
        "glioma_tumor", "meningioma_tumor", "no_tumor", "pituitary_tumor"
    };

    BrainTumourCNN model;
    model.load_weights(model_path);
    if (!model.are_weights_loaded()) {
        throw std::runtime_error("Could not load brain-tumor model: " + model_path);
    }

    const Tensor prediction = model.forward(model.preprocess(image_path));
    const auto& probabilities = prediction.get_data();
    if (probabilities.size() != labels.size()) {
        throw std::runtime_error("Unexpected brain-tumor model output size");
    }
    const auto best = std::max_element(probabilities.begin(), probabilities.end());
    const std::size_t predicted = static_cast<std::size_t>(
        std::distance(probabilities.begin(), best));

    std::cout << std::fixed << std::setprecision(6)
              << "{\"disease\":\"brain_tumor\",\"modality\":\"MRI\",\"class\":\""
              << labels[predicted] << "\",\"probability\":" << *best
              << ",\"probabilities\":{";
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (index != 0) std::cout << ",";
        std::cout << "\"" << labels[index] << "\":" << probabilities[index];
    }
    std::cout << "},\"research_only\":true,\"diagnosis\":false}\n";
}

static void predict_binary(const std::string& disease,
                           const std::string& modality,
                           const std::string& model_path,
                           const std::string& image_path) {
    const std::string negative_label =
        disease == "OSTEOARTHRITIS" ? "no_osteoarthritis" : "normal_bmd";
    const std::string positive_label =
        disease == "OSTEOARTHRITIS" ? "osteoarthritis" : "osteoporosis";

    SpineCNN model;
    model.load_weights(model_path);
    if (!model.are_weights_loaded()) {
        throw std::runtime_error("Could not load " + disease + " model: " + model_path);
    }

    const Tensor prediction = model.forward(model.preprocess(image_path));
    const auto& probabilities = prediction.get_data();
    if (probabilities.size() != 2) {
        throw std::runtime_error("Unexpected " + disease + " model output size");
    }
    const std::size_t predicted = probabilities[1] > probabilities[0] ? 1 : 0;
    const std::string& predicted_label = predicted == 1 ? positive_label : negative_label;

    std::cout << std::fixed << std::setprecision(6)
              << "{\"disease\":\""
              << (disease == "OSTEOARTHRITIS" ? "osteoarthritis" : "osteoporosis")
              << "\",\"modality\":\"" << modality
              << "\",\"class\":\"" << predicted_label
              << "\",\"probability\":" << probabilities[predicted]
              << ",\"positive_class_probability\":" << probabilities[1]
              << ",\"research_only\":true,\"diagnosis\":false}\n";
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) != "predict") {
            std::cerr << "Usage: medical_screening predict --disease "
                         "brain_tumor|osteoarthritis|osteoporosis "
                         "--model MODEL --image IMAGE [--modality MRI|XRAY|DEXA]\n";
            return 2;
        }

        const std::string disease = normalize_disease(
            option_value(argc, argv, "--disease"));
        const std::string model_path = option_value(argc, argv, "--model");
        const std::string image_path = option_value(argc, argv, "--image");
        const std::string expected_modality = required_modality(disease);
        const std::string supplied_modality = option_value(argc, argv, "--modality");
        const std::string modality = supplied_modality.empty()
            ? expected_modality : uppercase(supplied_modality);

        if (model_path.empty() || image_path.empty()) {
            throw std::runtime_error("--model and --image are required");
        }
        if (modality != expected_modality) {
            throw std::runtime_error(
                disease + " requires " + expected_modality + " input; received " + modality);
        }
        if (!fs::is_regular_file(model_path)) {
            throw std::runtime_error("Model file does not exist: " + model_path);
        }
        if (!fs::is_regular_file(image_path)) {
            throw std::runtime_error("Image file does not exist: " + image_path);
        }

        if (disease == "BRAIN_TUMOR") {
            predict_brain_tumor(model_path, image_path);
        } else {
            predict_binary(disease, modality, model_path, image_path);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error=" << error.what() << "\n";
        return 1;
    }
}
