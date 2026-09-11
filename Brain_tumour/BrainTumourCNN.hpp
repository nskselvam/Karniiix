#ifndef KARNIX_BRAIN_TUMOUR_CNN_HPP
#define KARNIX_BRAIN_TUMOUR_CNN_HPP

//neural network and image data handling
#include "../neural_network/utils/Tensor.hpp"


#include "../neural_network/layers/ConvolutionLayer.hpp"
#include "../neural_network/layers/ActivationFunctions.hpp"
#include "../neural_network/layers/PoolingLayers.hpp"

#include "../neural_network/layers/FullyConnectedLayer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

//
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

struct RawImage {
    std::vector<double> pixels;
    int width = 0;
    int height = 0;
};

namespace BrainImageIO {

inline void skip_ws(std::istream& stream) {
    while (true) {
        int character = stream.peek();
        if (character == '#') {
            std::string line;
            std::getline(stream, line);
        } else if (character != EOF && std::isspace(character)) {
            stream.get();
        } else {
            return;
        }
    }
}

inline RawImage load_ppm(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) throw std::runtime_error("Cannot open image: " + path);
    std::string magic;

    stream >> magic;
    skip_ws(stream);




    int width = 0;
    int height = 0;
    int max_value = 0;
    stream >> width >> height;
    skip_ws(stream);
    stream >> max_value;
    stream.get();
    if (width <= 0 || height <= 0 || max_value <= 0) throw std::runtime_error("Invalid PPM: " + path);

    RawImage image;
    image.width = width;
    image.height = height;

    // if (magic == "P5") {
    //     std::vector<unsigned char> values(width * height);
    //     stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size()));
    //     if (!stream) throw std::runtime_error("Truncated PGM: " + path);
    //     for (int index = 0; index < width * height; ++index) image.pixels[index] = values[index] / static_cast<double>(max_value);
    // } else if (magic == "P6") {


    image.pixels.resize(width * height);
    if (magic == "P5") {
        std::vector<unsigned char> values(width * height);
        stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size()));
        if (!stream) throw std::runtime_error("Truncated PGM: " + path);
        for (int index = 0; index < width * height; ++index) image.pixels[index] = values[index] / static_cast<double>(max_value);
    } else if (magic == "P6") {
        std::vector<unsigned char> values(width * height * 3);
        stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size()));
        if (!stream) throw std::runtime_error("Truncated PPM: " + path);
        for (int index = 0; index < width * height; ++index) {
            double red = values[index * 3] / static_cast<double>(max_value);
            double green = values[index * 3 + 1] / static_cast<double>(max_value);
            double blue = values[index * 3 + 2] / static_cast<double>(max_value);
            image.pixels[index] = 0.299 * red + 0.587 * green + 0.114 * blue;
        }
    } else {
        throw std::runtime_error("Unsupported PPM format: " + magic);
    }
    return image;
}

inline unsigned int little_endian_u16(const unsigned char* bytes) {
    return static_cast<unsigned int>(bytes[0]) | (static_cast<unsigned int>(bytes[1]) << 8);
}

inline unsigned int little_endian_u32(const unsigned char* bytes) {
    return static_cast<unsigned int>(bytes[0]) |
           (static_cast<unsigned int>(bytes[1]) << 8) |
           (static_cast<unsigned int>(bytes[2]) << 16) |
           (static_cast<unsigned int>(bytes[3]) << 24);
         //   (static_cast<unsigned int>(bytes[3]) << 24);

}

inline RawImage load_bmp(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    unsigned char header[54] = {};
    stream.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!stream || header[0] != 'B' || header[1] != 'M') throw std::runtime_error("Invalid BMP: " + path);
    int width = static_cast<int>(little_endian_u32(header + 18));
    int signed_height = static_cast<int>(little_endian_u32(header + 22));
    bool top_down = signed_height < 0;
    int height = top_down ? -signed_height : signed_height;
    int bits = static_cast<int>(little_endian_u16(header + 28));
    unsigned int offset = little_endian_u32(header + 10);
    if (width <= 0 || height <= 0 || (bits != 24 && bits != 32)) throw std::runtime_error("Unsupported BMP: " + path);
    int bytes_per_pixel = bits / 8;
    int row_size = ((width * bytes_per_pixel + 3) / 4) * 4;
    std::vector<unsigned char> row(row_size);
    RawImage image{std::vector<double>(width * height), width, height};
    stream.seekg(offset, std::ios::beg);
    for (int source_y = 0; source_y < height; ++source_y) {
        stream.read(reinterpret_cast<char*>(row.data()), row_size);
        if (!stream) throw std::runtime_error("Truncated BMP: " + path);
        int target_y = top_down ? source_y : height - 1 - source_y;
        for (int x = 0; x < width; ++x) {
            unsigned char blue = row[x * bytes_per_pixel];
            unsigned char green = row[x * bytes_per_pixel + 1];
            unsigned char red = row[x * bytes_per_pixel + 2];
            image.pixels[target_y * width + x] = (0.299 * red + 0.587 * green + 0.114 * blue) / 255.0;
        }
    }
    return image;
}

inline std::string extension(const std::string& path) {
    std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string value = path.substr(dot + 1);
    for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

inline RawImage load_image(const std::string& path) {
    std::string type = extension(path);
    if (type == "ppm" || type == "pgm") return load_ppm(path);
    if (type == "bmp") return load_bmp(path);
#ifdef __APPLE__
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), path.size(), false);
    if (!url) throw std::runtime_error("Cannot create image URL: " + path);
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source) throw std::runtime_error("Cannot decode image: " + path);
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image) throw std::runtime_error("Cannot decode image frame: " + path);
    int width = static_cast<int>(CGImageGetWidth(image));
    int height = static_cast<int>(CGImageGetHeight(image));



    RawImage result{std::vector<double>(width * height), width, height};
    std::vector<unsigned char> pixels(width * height, 0);
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceGray();
    CGContextRef context = CGBitmapContextCreate(
        pixels.data(), width, height, 8, width, color_space, kCGImageAlphaNone);
    if (!context) {




        CGColorSpaceRelease(color_space);
        CGImageRelease(image);
        throw std::runtime_error("Cannot create grayscale image context: " + path);
    }
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    CGColorSpaceRelease(color_space);
    CGImageRelease(image);
    for (std::size_t index = 0; index < pixels.size(); ++index) result.pixels[index] = pixels[index] / 255.0;
    return result;
#else
    std::string temporary = "/tmp/karnix_brain_" + std::to_string(reinterpret_cast<std::uintptr_t>(&path)) + ".bmp";
    std::remove(temporary.c_str());
    std::string command = "sips -s format bmp \"" + path + "\" --out \"" + temporary + "\" > /dev/null 2>&1";
    if (std::system(command.c_str()) != 0) {
        std::remove(temporary.c_str());
        throw std::runtime_error("Image conversion failed: " + path);
    }
    RawImage image = load_bmp(temporary);
    std::remove(temporary.c_str());
    return image;
#endif
}

} // namespace BrainImageIO

inline RawImage resize_bilinear(const RawImage& source, int target_height, int target_width) {
    RawImage result{std::vector<double>(target_height * target_width), target_width, target_height};
    double scale_y = static_cast<double>(source.height) / target_height;
    double scale_x = static_cast<double>(source.width) / target_width;
    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            double source_y = (y + 0.5) * scale_y - 0.5;
            double source_x = (x + 0.5) * scale_x - 0.5;
            int y0 = std::max(0, static_cast<int>(std::floor(source_y)));
            int x0 = std::max(0, static_cast<int>(std::floor(source_x)));
            int y1 = std::min(source.height - 1, y0 + 1);
            int x1 = std::min(source.width - 1, x0 + 1);
            double fy = source_y - y0;
            double fx = source_x - x0;
            result.pixels[y * target_width + x] =
                (1 - fy) * (1 - fx) * source.pixels[y0 * source.width + x0] +
                (1 - fy) * fx * source.pixels[y0 * source.width + x1] +
                fy * (1 - fx) * source.pixels[y1 * source.width + x0] +
                fy * fx * source.pixels[y1 * source.width + x1];
        }
    }
    return result;
}

class BrainTumourCNN {
public:
    static constexpr int INPUT_H = 64;
    static constexpr int INPUT_W = 64;
    static constexpr int FLAT_SIZE = 16 * 6 * 6;
    static constexpr int FC1_OUT = 32;
    static constexpr int NUM_CLASS = 4;

    struct ForwardTrace {
        Tensor probabilities;
    };

private:
    ConvolutionLayer conv1;
    ConvolutionLayer conv2;
    ConvolutionLayer conv3;
    ActivationFunctions::ReLU relu1, relu2, relu3, relu_fc;
    MaxPoolingLayer pool1, pool2, pool3;
    FullyConnectedLayer fc1;
    FullyConnectedLayer fc2;
    ActivationFunctions::Softmax softmax;
    bool weights_loaded = false;

    static Tensor flatten(const Tensor& tensor) {
        Tensor output({tensor.get_total_size()}, false);
        output.get_data() = tensor.get_data();
        return output;
    }

    static Tensor unflatten(const Tensor& tensor, int channels, int height, int width) {
        Tensor output({channels, height, width}, false);
        output.get_data() = tensor.get_data();
        return output;
    }

    static void sgd_conv(ConvolutionLayer& layer, double learning_rate) {
        auto& weights = layer.get_weights().get_data();
        auto& bias = layer.get_bias().get_data();
        const auto& weight_grad = layer.get_weight_grad().get_data();
        const auto& bias_grad = layer.get_bias_grad().get_data();
        for (std::size_t i = 0; i < weights.size(); ++i) weights[i] -= learning_rate * weight_grad[i];
        for (std::size_t i = 0; i < bias.size(); ++i) bias[i] -= learning_rate * bias_grad[i];
    }

    static void sgd_fc(FullyConnectedLayer& layer, double learning_rate) {
        auto& weights = layer.get_weights().get_data();
        auto& bias = layer.get_bias().get_data();
        const auto& weight_grad = layer.get_weight_grad().get_data();
        const auto& bias_grad = layer.get_bias_grad().get_data();
        for (std::size_t i = 0; i < weights.size(); ++i) weights[i] -= learning_rate * weight_grad[i];
        for (std::size_t i = 0; i < bias.size(); ++i) bias[i] -= learning_rate * bias_grad[i];
    }

    static void write_layer(std::ostream& stream, ConvolutionLayer& layer) {
        const auto& weights = layer.get_weights().get_data();
        const auto& bias = layer.get_bias().get_data();
        std::size_t weight_count = weights.size();
        std::size_t bias_count = bias.size();
        stream.write(reinterpret_cast<const char*>(&weight_count), sizeof(weight_count));
        stream.write(reinterpret_cast<const char*>(weights.data()), weight_count * sizeof(double));
        stream.write(reinterpret_cast<const char*>(&bias_count), sizeof(bias_count));
        stream.write(reinterpret_cast<const char*>(bias.data()), bias_count * sizeof(double));
    }

    static void write_layer(std::ostream& stream, FullyConnectedLayer& layer) {
        const auto& weights = layer.get_weights().get_data();
        const auto& bias = layer.get_bias().get_data();
        std::size_t weight_count = weights.size();
        std::size_t bias_count = bias.size();
        stream.write(reinterpret_cast<const char*>(&weight_count), sizeof(weight_count));
        stream.write(reinterpret_cast<const char*>(weights.data()), weight_count * sizeof(double));
        stream.write(reinterpret_cast<const char*>(&bias_count), sizeof(bias_count));
        stream.write(reinterpret_cast<const char*>(bias.data()), bias_count * sizeof(double));
    }

    template <typename Layer>
    static void read_layer(std::istream& stream, Layer& layer, const std::string& name) {
        std::size_t weight_count = 0;
        stream.read(reinterpret_cast<char*>(&weight_count), sizeof(weight_count));
        auto& weights = layer.get_weights().get_data();
        if (!stream || weight_count != weights.size()) throw std::runtime_error("Invalid weights: " + name);
        stream.read(reinterpret_cast<char*>(weights.data()), weight_count * sizeof(double));
        std::size_t bias_count = 0;
        stream.read(reinterpret_cast<char*>(&bias_count), sizeof(bias_count));
        auto& bias = layer.get_bias().get_data();
        if (!stream || bias_count != bias.size()) throw std::runtime_error("Invalid bias: " + name);
        stream.read(reinterpret_cast<char*>(bias.data()), bias_count * sizeof(double));
        if (!stream) throw std::runtime_error("Truncated layer: " + name);
    }

public:
    BrainTumourCNN()
        : conv1(1, 4, 3, 1, 0, true),
          conv2(4, 8, 3, 1, 0, true),
          conv3(8, 16, 3, 1, 0, true),
          pool1(2), pool2(2), pool3(2),
          fc1(FLAT_SIZE, FC1_OUT, true),
          fc2(FC1_OUT, NUM_CLASS, true) {}

    Tensor preprocess(const std::string& image_path) {
        RawImage image = BrainImageIO::load_image(image_path);
        image = resize_bilinear(image, INPUT_H, INPUT_W);
        Tensor input({1, INPUT_H, INPUT_W}, false);
        for (int y = 0; y < INPUT_H; ++y) {
            for (int x = 0; x < INPUT_W; ++x) {
                input(0, y, x) = (image.pixels[y * INPUT_W + x] - 0.5) / 0.5;
            }
        }
        return input;
    }

    Tensor forward(const Tensor& input) {
        Tensor p1 = pool1.forward(relu1.forward(conv1.forward(input)));
        Tensor p2 = pool2.forward(relu2.forward(conv2.forward(p1)));
        Tensor p3 = pool3.forward(relu3.forward(conv3.forward(p2)));
        Tensor hidden = relu_fc.forward(fc1.forward(flatten(p3)));
        return softmax.forward(fc2.forward(hidden));
    }

    double train_step(const Tensor& input, int label, double learning_rate) {
        Tensor r1 = relu1.forward(conv1.forward(input));
        Tensor p1 = pool1.forward(r1);
        Tensor r2 = relu2.forward(conv2.forward(p1));
        Tensor p2 = pool2.forward(r2);
        Tensor r3 = relu3.forward(conv3.forward(p2));
        Tensor p3 = pool3.forward(r3);
        Tensor hidden_pre = fc1.forward(flatten(p3));
        Tensor hidden = relu_fc.forward(hidden_pre);
        Tensor probabilities = softmax.forward(fc2.forward(hidden));
        const auto& values = probabilities.get_data();
        double loss = -std::log(std::max(values[label], 1e-10));

        Tensor gradient({NUM_CLASS}, false);
        auto& gradient_values = gradient.get_data();
        for (int i = 0; i < NUM_CLASS; ++i) gradient_values[i] = values[i] - (i == label ? 1.0 : 0.0);
        gradient = fc2.backward(gradient);
        gradient = relu_fc.backward(gradient);
        gradient = fc1.backward(gradient);
        Tensor gradient_3d = unflatten(gradient, 16, 6, 6);
        gradient_3d = conv3.backward(relu3.backward(pool3.backward(gradient_3d)));
        gradient_3d = conv2.backward(relu2.backward(pool2.backward(gradient_3d)));
        conv1.backward(relu1.backward(pool1.backward(gradient_3d)));

        sgd_conv(conv1, learning_rate);
        sgd_conv(conv2, learning_rate);
        sgd_conv(conv3, learning_rate);
        sgd_fc(fc1, learning_rate);
        sgd_fc(fc2, learning_rate);
        return loss;
    }

    void save_weights(const std::string& path) const {
        std::ofstream stream(path, std::ios::binary);
        if (!stream.is_open()) throw std::runtime_error("Cannot write: " + path);
        const char magic[8] = {'K', 'B', 'T', '0', '0', '0', '0', '1'};
        stream.write(magic, sizeof(magic));
        write_layer(stream, const_cast<ConvolutionLayer&>(conv1));
        write_layer(stream, const_cast<ConvolutionLayer&>(conv2));
        write_layer(stream, const_cast<ConvolutionLayer&>(conv3));
        write_layer(stream, const_cast<FullyConnectedLayer&>(fc1));
        write_layer(stream, const_cast<FullyConnectedLayer&>(fc2));
    }

    void load_weights(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) return;
        const char expected[8] = {'K', 'B', 'T', '0', '0', '0', '0', '1'};
        char actual[8] = {};
        stream.read(actual, sizeof(actual));
        if (!stream || std::memcmp(actual, expected, sizeof(expected)) != 0) return;
        read_layer(stream, conv1, "conv1");
        read_layer(stream, conv2, "conv2");
        read_layer(stream, conv3, "conv3");
        read_layer(stream, fc1, "fc1");
        read_layer(stream, fc2, "fc2");
        std::streampos parsed = stream.tellg();
        stream.seekg(0, std::ios::end);
        if (!stream || stream.tellg() != parsed) throw std::runtime_error("Unexpected trailing model data: " + path);
        weights_loaded = true;
    }

    bool are_weights_loaded() const { return weights_loaded; }
    int get_param_count() const { return 40 + 296 + 1168 + 18464 + 132; }
};

#endif