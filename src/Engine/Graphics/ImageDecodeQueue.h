#pragma once

#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Library/Image/Image.h"

class GraphicsImage;

/**
 * Texture pack replacement of a `GraphicsImage`, decoded off the game thread for `GraphicsImage::installReplacement`.
 */
struct DecodedReplacement {
    GraphicsImage *image = nullptr;
    RgbaImage texture; // Empty when the image has no replacement.
    RgbaImage material; // Empty when the replacement has no material.
    std::vector<RgbaImage> mips; // The levels below `texture` down to 1x1, when `texture` is scaled up from the image.
    float decodeMs = 0; // Time a worker took to decode it.
};

/**
 * Decodes the texture pack replacements of images on worker threads, the most recently requested first, since what's
 * requested again every frame is what's on screen. Textures can only be made on the thread that owns the GL context,
 * so decoded images wait here until that thread takes them.
 */
class ImageDecodeQueue {
 public:
    /**
     * @param workerCount               Number of worker threads.
     */
    explicit ImageDecodeQueue(int workerCount);

    /**
     * Drops the queued images and waits for the ones being decoded.
     */
    ~ImageDecodeQueue();

    /**
     * Queues an image for decoding, or moves it to the front of the queue if it's queued already. Does nothing if it's
     * being decoded, or decoded and not taken yet.
     *
     * @param image                     Image with a loader that's safe to call from another thread.
     */
    void request(GraphicsImage *image);

    /**
     * Queues an image for decoding after everything that's queued, unless it's queued already, being decoded, or
     * decoded and not taken yet.
     *
     * @param image                     Image that's likely to be requested soon.
     */
    void prefetch(GraphicsImage *image);

    /**
     * @return                          The decoded image that was requested most recently, or nothing when none is
     *                                  ready. Prefetched images that weren't requested come last.
     */
    [[nodiscard]] std::optional<DecodedReplacement> take();

    /**
     * Drops the queued and the decoded images and waits for the ones being decoded, so that no worker touches any
     * image once it returns. Call it before freeing images that may have been requested.
     */
    void flush();


 private:
    void work();

 private:
    std::mutex _mutex;
    std::condition_variable _wake; // Workers wait on it for queued images.
    std::condition_variable _idle; // `flush` waits on it for the images being decoded.
    std::list<GraphicsImage *> _queued; // The next to decode at the back.
    std::unordered_map<GraphicsImage *, std::list<GraphicsImage *>::iterator> _queuedPositions;
    std::unordered_set<GraphicsImage *> _requested; // Being decoded, or decoded and not taken.
    std::unordered_map<GraphicsImage *, uint64_t> _lastRequests; // Value of `_requestCount` at the last request.
    uint64_t _requestCount = 0;
    std::vector<DecodedReplacement> _decoded;
    int _decoding = 0;
    bool _stopping = false;
    std::vector<std::thread> _workers;
};
