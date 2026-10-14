#include "ImageDecodeQueue.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "Engine/Graphics/Image.h"

ImageDecodeQueue::ImageDecodeQueue(int workerCount) {
    for (int i = 0; i < workerCount; i++)
        _workers.emplace_back([this] { work(); });
}

ImageDecodeQueue::~ImageDecodeQueue() {
    {
        std::lock_guard lock(_mutex);
        _stopping = true;
        _queued.clear();
        _queuedPositions.clear();
    }
    _wake.notify_all();
    for (std::thread &worker : _workers)
        worker.join();
}

void ImageDecodeQueue::request(GraphicsImage *image) {
    {
        std::lock_guard lock(_mutex);
        _lastRequests[image] = ++_requestCount;
        if (_requested.contains(image))
            return;
        if (auto pos = _queuedPositions.find(image); pos != _queuedPositions.end()) {
            _queued.splice(_queued.end(), _queued, pos->second);
            return;
        }
        _queuedPositions.emplace(image, _queued.insert(_queued.end(), image));
    }
    _wake.notify_one();
}

void ImageDecodeQueue::prefetch(GraphicsImage *image) {
    {
        std::lock_guard lock(_mutex);
        if (_requested.contains(image) || _queuedPositions.contains(image))
            return;
        _queuedPositions.emplace(image, _queued.insert(_queued.begin(), image));
    }
    _wake.notify_one();
}

std::optional<DecodedReplacement> ImageDecodeQueue::take() {
    std::lock_guard lock(_mutex);
    if (_decoded.empty())
        return std::nullopt;

    auto lastRequest = [this](const DecodedReplacement &decoded) {
        auto pos = _lastRequests.find(decoded.image);
        return pos == _lastRequests.end() ? 0 : pos->second;
    };
    auto newest = std::ranges::max_element(_decoded, {}, lastRequest);
    std::swap(*newest, _decoded.back());
    DecodedReplacement result = std::move(_decoded.back());
    _decoded.pop_back();
    _requested.erase(result.image);
    _lastRequests.erase(result.image);
    return result;
}

void ImageDecodeQueue::flush() {
    std::unique_lock lock(_mutex);
    _queued.clear();
    _queuedPositions.clear();
    _idle.wait(lock, [this] { return _decoding == 0; });
    _decoded.clear();
    _requested.clear();
    _lastRequests.clear();
}

void ImageDecodeQueue::work() {
    std::unique_lock lock(_mutex);
    while (true) {
        _wake.wait(lock, [this] { return _stopping || !_queued.empty(); });
        if (_stopping)
            return;

        GraphicsImage *image = _queued.back();
        _queued.pop_back();
        _queuedPositions.erase(image);
        _requested.insert(image);
        _decoding++;
        lock.unlock();

        auto start = std::chrono::steady_clock::now();
        DecodedReplacement result = image->decodeReplacement();
        result.image = image;
        result.decodeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();

        lock.lock();
        _decoding--;
        _decoded.push_back(std::move(result));
        if (_decoding == 0)
            _idle.notify_all();
    }
}
