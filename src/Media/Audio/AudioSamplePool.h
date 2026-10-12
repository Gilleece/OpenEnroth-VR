#pragma once

#include <functional>
#include <list>
#include <optional>

#include "Engine/Data/SoundEnums.h"
#include "Engine/Pid.h"

#include "Library/Geometry/Vec.h"

#include "Media/AudioSample.h"


struct AudioSamplePoolEntry {
    AudioSamplePoolEntry(PAudioSample samplePtr, SoundId id, Pid pid, bool positional) : samplePtr(samplePtr), id(id), pid(pid), positional(positional) {}

    PAudioSample samplePtr;
    SoundId id = SOUND_Invalid;
    Pid pid; // Object that plays the sound, if any.
    bool positional = false;
};

class AudioSamplePool {
 public:
    explicit AudioSamplePool(bool looping):_looping(looping) {}

    SoundPlaybackResult playNew(PAudioSample sample, PAudioDataSource source, bool positional = false, Pid pid = Pid());
    SoundPlaybackResult playUniqueSoundId(PAudioSample sample, PAudioDataSource source, SoundId id, bool positional = false, Pid pid = Pid());
    SoundPlaybackResult playUniquePid(PAudioSample sample, PAudioDataSource source, Pid pid, bool positional = false);
    void pause();
    void resume();
    void stop();
    void stopSoundId(SoundId soundId);
    void stopPid(Pid pid);
    void update();

    /**
     * Moves positional samples to where the objects that play them are now.
     *
     * @param positionOf                Returns the current position of the object with the given `Pid`, or
     *                                  `std::nullopt` to leave the sample where it is.
     */
    void updatePositions(const std::function<std::optional<Vec3f>(Pid)> &positionOf);
    void setVolume(float value);
    bool hasPlaying();

 private:
    std::list<AudioSamplePoolEntry> _samplePool;
    bool _looping = false;
};
