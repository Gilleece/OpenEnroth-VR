#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "Testing/Unit/UnitTest.h"

#include "Engine/Graphics/Image.h"
#include "Engine/Graphics/ImageDecodeQueue.h"
#include "Engine/Graphics/ImageLoader.h"

struct LoaderLog {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true; // Loads wait while it's closed.
    std::vector<std::string> started;
    std::vector<std::string> finished;
    int alive = 0;
};

class FakeLoader : public ImageLoader {
 public:
    FakeLoader(std::shared_ptr<LoaderLog> log, std::string_view name) : ImageLoader(nullptr, name), _log(std::move(log)) {
        std::lock_guard lock(_log->mutex);
        _log->alive++;
    }

    virtual ~FakeLoader() override {
        std::lock_guard lock(_log->mutex);
        _log->alive--;
    }

    virtual RgbaImage loadRgba() override {
        return RgbaImage::solid(Color(1, 2, 3, 4), 2, 2);
    }

    virtual Sizei loadSize() override {
        return Sizei(2, 2);
    }

    virtual RgbaImage loadTextureRgba() override {
        std::unique_lock lock(_log->mutex);
        _log->started.push_back(name());
        _log->changed.notify_all();
        _log->changed.wait(lock, [this] { return _log->open; });
        _log->finished.push_back(name());
        return RgbaImage::solid(Color(10, 20, 30, 40), 8, 8);
    }

 private:
    std::shared_ptr<LoaderLog> _log;
};

static std::unique_ptr<GraphicsImage> fakeImage(const std::shared_ptr<LoaderLog> &log, std::string_view name) {
    return GraphicsImage::Create(std::make_unique<FakeLoader>(log, name));
}

static std::vector<DecodedReplacement> takeAll(ImageDecodeQueue *queue, size_t count) {
    std::vector<DecodedReplacement> result;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (result.size() < count && std::chrono::steady_clock::now() < deadline) {
        if (std::optional<DecodedReplacement> decoded = queue->take()) {
            result.push_back(std::move(*decoded));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return result;
}

UNIT_TEST(ImageDecodeQueue, Decodes) {
    // Each requested image is decoded once, with the mip levels of its scaled up replacement.
    auto log = std::make_shared<LoaderLog>();
    std::unique_ptr<GraphicsImage> a = fakeImage(log, "a");
    std::unique_ptr<GraphicsImage> b = fakeImage(log, "b");
    ImageDecodeQueue queue(2);
    queue.request(a.get());
    queue.request(b.get());
    queue.request(a.get());

    std::vector<DecodedReplacement> decoded = takeAll(&queue, 2);
    ASSERT_EQ(decoded.size(), 2u);
    for (const DecodedReplacement &result : decoded) {
        EXPECT_TRUE(result.image == a.get() || result.image == b.get());
        EXPECT_EQ(result.texture.size(), Sizei(8, 8));
        ASSERT_EQ(result.mips.size(), 3u);
        EXPECT_EQ(result.mips[0].size(), Sizei(4, 4));
        EXPECT_EQ(result.mips[2].size(), Sizei(1, 1));
        EXPECT_EQ(result.mips[2][0][0], Color(10, 20, 30, 40));
    }
    EXPECT_NE(decoded[0].image, decoded[1].image);
    EXPECT_FALSE(queue.take());

    std::lock_guard lock(log->mutex);
    EXPECT_EQ(log->started.size(), 2u);
}

UNIT_TEST(ImageDecodeQueue, MostRecentFirst) {
    // Queued images are decoded the most recently requested first, and requesting a queued image again moves it up.
    auto log = std::make_shared<LoaderLog>();
    log->open = false;
    std::vector<std::unique_ptr<GraphicsImage>> images;
    for (std::string_view name : {"a", "b", "c", "d"})
        images.push_back(fakeImage(log, name));

    ImageDecodeQueue queue(1);
    queue.request(images[0].get());
    {
        std::unique_lock lock(log->mutex);
        ASSERT_TRUE(log->changed.wait_for(lock, std::chrono::seconds(10), [&] { return log->started.size() == 1; }));
    }
    queue.request(images[1].get());
    queue.request(images[2].get());
    queue.request(images[3].get());
    queue.request(images[1].get());
    {
        std::lock_guard lock(log->mutex);
        log->open = true;
        log->changed.notify_all();
    }

    ASSERT_EQ(takeAll(&queue, 4).size(), 4u);
    std::lock_guard lock(log->mutex);
    EXPECT_EQ(log->started, std::vector<std::string>({"a", "b", "d", "c"}));
}

UNIT_TEST(ImageDecodeQueue, PrefetchLast) {
    // A prefetched image is decoded after everything queued, and prefetching doesn't move a queued image.
    auto log = std::make_shared<LoaderLog>();
    log->open = false;
    std::vector<std::unique_ptr<GraphicsImage>> images;
    for (std::string_view name : {"a", "b", "c", "d"})
        images.push_back(fakeImage(log, name));

    ImageDecodeQueue queue(1);
    queue.request(images[0].get());
    {
        std::unique_lock lock(log->mutex);
        ASSERT_TRUE(log->changed.wait_for(lock, std::chrono::seconds(10), [&] { return log->started.size() == 1; }));
    }
    queue.prefetch(images[2].get());
    queue.request(images[1].get());
    queue.request(images[3].get());
    queue.prefetch(images[1].get());
    {
        std::lock_guard lock(log->mutex);
        log->open = true;
        log->changed.notify_all();
    }

    ASSERT_EQ(takeAll(&queue, 4).size(), 4u);
    std::lock_guard lock(log->mutex);
    EXPECT_EQ(log->started, std::vector<std::string>({"a", "d", "b", "c"}));
}

UNIT_TEST(ImageDecodeQueue, FlushLeavesFreedImagesAlone) {
    // Flush drops the queued image and waits for the two being decoded, so the images can be freed right after.
    auto log = std::make_shared<LoaderLog>();
    log->open = false;
    std::vector<std::unique_ptr<GraphicsImage>> images;
    for (std::string_view name : {"a", "b", "c"})
        images.push_back(fakeImage(log, name));

    ImageDecodeQueue queue(2);
    for (const std::unique_ptr<GraphicsImage> &image : images)
        queue.request(image.get());
    std::vector<std::string> startedBeforeFlush;
    {
        std::unique_lock lock(log->mutex);
        ASSERT_TRUE(log->changed.wait_for(lock, std::chrono::seconds(10), [&] { return log->started.size() == 2; }));
        startedBeforeFlush = log->started;
    }

    std::thread opener([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard lock(log->mutex);
        log->open = true;
        log->changed.notify_all();
    });
    queue.flush();
    {
        std::lock_guard lock(log->mutex);
        EXPECT_EQ(log->finished.size(), 2u); // The decodes that had started were done when flush returned.
    }
    opener.join();

    images.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(queue.take());
    {
        std::lock_guard lock(log->mutex);
        EXPECT_EQ(log->alive, 0);
        EXPECT_EQ(log->started, startedBeforeFlush); // Nothing started on the freed images after flush.
    }

    // The queue keeps working after a flush.
    std::unique_ptr<GraphicsImage> d = fakeImage(log, "d");
    queue.request(d.get());
    std::vector<DecodedReplacement> decoded = takeAll(&queue, 1);
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].image, d.get());
}

UNIT_TEST(ImageDecodeQueue, DestructorWaitsForDecodes) {
    // Destroying the queue drops what's queued and waits for what's being decoded.
    auto log = std::make_shared<LoaderLog>();
    log->open = false;
    std::unique_ptr<GraphicsImage> a = fakeImage(log, "a");
    std::unique_ptr<GraphicsImage> b = fakeImage(log, "b");
    std::thread opener;
    {
        ImageDecodeQueue queue(1);
        queue.request(a.get());
        queue.request(b.get());
        {
            std::unique_lock lock(log->mutex);
            ASSERT_TRUE(log->changed.wait_for(lock, std::chrono::seconds(10), [&] { return log->started.size() == 1; }));
        }
        opener = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::lock_guard lock(log->mutex);
            log->open = true;
            log->changed.notify_all();
        });
    }
    opener.join();

    std::lock_guard lock(log->mutex);
    EXPECT_EQ(log->finished.size(), 1u);
    EXPECT_EQ(log->started.size(), 1u);
}
