#include "document/Upscale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace colorizing::document {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rgb {
    double r = 0;
    double g = 0;
    double b = 0;
};

Rgb expand(uint16_t color)
{
    const int r = (color >> 11) & 0x1f;
    const int g = (color >> 5) & 0x3f;
    const int b = color & 0x1f;
    return Rgb{static_cast<double>((r << 3) | (r >> 2)),
               static_cast<double>((g << 2) | (g >> 4)),
               static_cast<double>((b << 3) | (b >> 2))};
}

uint16_t pack(const Rgb& color)
{
    const auto channel = [](double value) {
        return static_cast<int>(std::clamp(std::lround(value), 0L, 255L));
    };
    return static_cast<uint16_t>(((channel(color.r) >> 3) << 11) |
                                 ((channel(color.g) >> 2) << 5) | (channel(color.b) >> 3));
}


// The weights of the taps around a source position, as OpenCV's resize computes them.
struct Kernel {
    int taps = 2;
    // The first tap is at floor(position) - (taps / 2 - 1).
    std::function<void(double fraction, double* weights)> weights;
};

Kernel bilinear()
{
    return Kernel{2, [](double f, double* w) {
                      w[0] = 1 - f;
                      w[1] = f;
                  }};
}

Kernel bicubic()
{
    return Kernel{4, [](double f, double* w) {
                      constexpr double a = -0.75;
                      w[0] = ((a * (f + 1) - 5 * a) * (f + 1) + 8 * a) * (f + 1) - 4 * a;
                      w[1] = ((a + 2) * f - (a + 3)) * f * f + 1;
                      w[2] = ((a + 2) * (1 - f) - (a + 3)) * (1 - f) * (1 - f) + 1;
                      w[3] = 1 - w[0] - w[1] - w[2];
                  }};
}

Kernel lanczos4()
{
    return Kernel{8, [](double f, double* w) {
                      double sum = 0;
                      for (int i = 0; i < 8; ++i) {
                          const double d = std::abs(f + 3 - i);
                          double value = 1;
                          if (d > 1e-9) {
                              value = 4 * std::sin(kPi * d) * std::sin(kPi * d / 4) /
                                      (kPi * kPi * d * d);
                          }
                          w[i] = value;
                          sum += value;
                      }
                      for (int i = 0; i < 8; ++i) {
                          w[i] /= sum;
                      }
                  }};
}

// One dimension at a time: rows, then columns.
Rgb565Frame interpolate(const Rgb565Frame& frame, const Kernel& kernel)
{
    const int width = frame.size.width;
    const int height = frame.size.height;
    std::vector<Rgb> source(frame.pixels.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        source[i] = expand(frame.pixels[i]);
    }
    std::vector<double> weights(static_cast<std::size_t>(kernel.taps));
    const auto pass = [&](const std::vector<Rgb>& input,
                          int inWidth,
                          int inHeight,
                          bool horizontal) {
        const int outWidth = horizontal ? inWidth * 2 : inWidth;
        const int outHeight = horizontal ? inHeight : inHeight * 2;
        const int length = horizontal ? inWidth : inHeight;
        std::vector<Rgb> output(static_cast<std::size_t>(outWidth) * outHeight);
        for (int o = 0; o < (horizontal ? outWidth : outHeight); ++o) {
            const double position = (o + 0.5) / 2 - 0.5;
            const int base = static_cast<int>(std::floor(position));
            kernel.weights(position - base, weights.data());
            const int first = base - (kernel.taps / 2 - 1);
            for (int other = 0; other < (horizontal ? outHeight : outWidth); ++other) {
                Rgb sum;
                for (int t = 0; t < kernel.taps; ++t) {
                    const int s = std::clamp(first + t, 0, length - 1);
                    const std::size_t index =
                        horizontal ? static_cast<std::size_t>(other) * inWidth + s
                                   : static_cast<std::size_t>(s) * inWidth + other;
                    const Rgb& in = input[index];
                    sum.r += in.r * weights[static_cast<std::size_t>(t)];
                    sum.g += in.g * weights[static_cast<std::size_t>(t)];
                    sum.b += in.b * weights[static_cast<std::size_t>(t)];
                }
                Rgb& out = horizontal ? output[static_cast<std::size_t>(other) * outWidth + o]
                                      : output[static_cast<std::size_t>(o) * outWidth + other];
                out = sum;
            }
        }
        return output;
    };
    const std::vector<Rgb> wide = pass(source, width, height, true);
    const std::vector<Rgb> both = pass(wide, width * 2, height, false);
    Rgb565Frame result = makeRgb565Frame(Size{width * 2, height * 2}, 0);
    for (std::size_t i = 0; i < both.size(); ++i) {
        result.pixels[i] = pack(both[i]);
    }
    return result;
}

Rgb565Frame scale2x(const Rgb565Frame& frame)
{
    const int width = frame.size.width;
    const int height = frame.size.height;
    const auto at = [&](int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return frame.pixels[static_cast<std::size_t>(y) * width + x];
    };
    Rgb565Frame result = makeRgb565Frame(Size{width * 2, height * 2}, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint16_t b = at(x, y - 1);
            const uint16_t d = at(x - 1, y);
            const uint16_t e = at(x, y);
            const uint16_t f = at(x + 1, y);
            const uint16_t h = at(x, y + 1);
            uint16_t e0 = e, e1 = e, e2 = e, e3 = e;
            if (b != h && d != f) {
                e0 = d == b ? d : e;
                e1 = b == f ? f : e;
                e2 = d == h ? d : e;
                e3 = h == f ? f : e;
            }
            const std::size_t top = static_cast<std::size_t>(2 * y) * (2 * width) + 2 * x;
            const std::size_t bottom = top + 2 * width;
            result.pixels[top] = e0;
            result.pixels[top + 1] = e1;
            result.pixels[bottom] = e2;
            result.pixels[bottom + 1] = e3;
        }
    }
    return result;
}

}  // namespace

Rgb565Frame upscale2x(const Rgb565Frame& frame, UpscaleFilter filter)
{
    switch (filter) {
        case UpscaleFilter::Nearest:
            break;
        case UpscaleFilter::Bilinear:
            return interpolate(frame, bilinear());
        case UpscaleFilter::Bicubic:
            return interpolate(frame, bicubic());
        case UpscaleFilter::Lanczos4:
            return interpolate(frame, lanczos4());
        case UpscaleFilter::Scale2x:
            return scale2x(frame);
    }
    const int width = frame.size.width;
    Rgb565Frame result = makeRgb565Frame(Size{width * 2, frame.size.height * 2}, 0);
    for (int y = 0; y < frame.size.height * 2; ++y) {
        for (int x = 0; x < width * 2; ++x) {
            result.pixels[static_cast<std::size_t>(y) * width * 2 + x] =
                frame.pixels[static_cast<std::size_t>(y / 2) * width + x / 2];
        }
    }
    return result;
}

std::string upscaleFilterName(UpscaleFilter filter)
{
    switch (filter) {
        case UpscaleFilter::Nearest:
            return "Nearest";
        case UpscaleFilter::Bilinear:
            return "Bilinear";
        case UpscaleFilter::Bicubic:
            return "Bicubic";
        case UpscaleFilter::Lanczos4:
            return "Lanczos4";
        case UpscaleFilter::Scale2x:
            return "Scale2x";
    }
    return {};
}

}  // namespace colorizing::document
