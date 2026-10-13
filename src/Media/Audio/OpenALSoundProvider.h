#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <al.h> // NOLINT: not a C system header.
#include <alc.h> // NOLINT: not a C system header.
#include <alext.h> // NOLINT: not a C system header.

#include "Library/Geometry/Vec.h"

#include "OpenALUpdateThread.h"

// Sound attenuation factors
constexpr float MAX_SOUND_DIST = 60000.0f;
constexpr float REFERENCE_DIST = 300.0f;
constexpr float ROLLOFF_FACTOR = 1.5f;

class OpenALSoundProvider {
 public:
    struct StreamingTrackBuffer {
        unsigned int source_id = 0;
        ALenum sample_format = 0;
        int sample_rate = 0;
    };

    OpenALSoundProvider();
    virtual ~OpenALSoundProvider();

    bool Initialize();
    void Release();

    void DeleteStreamingTrack(StreamingTrackBuffer **buffer);
    StreamingTrackBuffer *CreateStreamingTrack16(int num_channels,
                                                 int sample_rate,
                                                 int bytes_per_sample);
    void Stream16(StreamingTrackBuffer *buffer, int num_samples,
                  const void *samples, bool wait = false);

    /**
     * Places the listener in the world.
     *
     * @param position                  Listener position in world coordinates.
     * @param forward                   Direction the listener faces, unit length.
     * @param up                        Listener's up direction, unit length and perpendicular to `forward`.
     */
    void SetListener(const Vec3f &position, const Vec3f &forward, const Vec3f &up);

 protected:
    void DeleteBuffers(StreamingTrackBuffer *track, int type);

    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;

 private:
    static void ALC_APIENTRY deviceEvent(ALCenum eventType, ALCenum deviceType, ALCdevice *device, ALCsizei length,
                                         const ALCchar *message, void *userParam) noexcept;

    /**
     * Moves the open device to the system's default output device when the default changes or the current device is
     * gone, keeping the context with every source and buffer. Runs on `_deviceWatcher`'s thread.
     */
    void followDefaultDevice();

    /**
     * @param device                    Open device, or null for the system's default output device.
     * @return                          Full name of the device, as the device list gives it.
     */
    static std::string deviceName(ALCdevice *device);

    std::vector<ALCint> _attributes; // The context attributes, for reopening.
    LPALCREOPENDEVICESOFT _reopenDevice = nullptr; // Null without ALC_SOFT_reopen_device.
    LPALCEVENTCALLBACKSOFT _eventCallback = nullptr; // Null without ALC_SOFT_system_events.
    bool _deviceEvents = false; // Whether ALC_SOFT_system_events reports default device changes.
    std::atomic<bool> _devicesChanged = false; // Set by `deviceEvent`.
    std::chrono::steady_clock::time_point _nextDefaultCheck;
    bool _reopenFailed = false; // Whether the last reopen failed, so that the retries don't log it again.
    std::chrono::steady_clock::time_point _nextReopen; // Retries after a failed reopen wait until then.
    OpenALUpdateThread _deviceWatcher;
};

// TODO(pskelton): contain?
void setSourceDefaults(ALuint al_source);
bool checkOpenALError();
