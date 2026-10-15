#include "MovieImport.h"

namespace colorizing::app {

std::vector<double> movieFrameTimes(double startMs,
                                    const std::vector<uint32_t>& durationsMs,
                                    int intervalMs)
{
    std::vector<double> times;
    times.reserve(durationsMs.size());
    double time = startMs;
    for (std::size_t index = 0; index < durationsMs.size(); ++index) {
        times.push_back(time);
        time += intervalMs > 0 ? static_cast<double>(intervalMs)
                               : static_cast<double>(durationsMs[index]);
    }
    return times;
}

QImage croppedMoviePicture(const QImage& picture, const QRect& crop)
{
    const QRect zone = crop.intersected(picture.rect());
    return zone.isEmpty() ? picture : picture.copy(zone);
}

}  // namespace colorizing::app
