#pragma once

#include <cstddef>

#include <al.h> // NOLINT: not a C system header.
#include <alc.h> // NOLINT: not a C system header.

#include "Library/Geometry/Vec.h"

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
};

// TODO(pskelton): contain?
void setSourceDefaults(ALuint al_source);
bool checkOpenALError();
