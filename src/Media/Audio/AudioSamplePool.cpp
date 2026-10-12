#include "AudioSamplePool.h"

#include "Media/Audio/OpenALSoundProvider.h"

SoundPlaybackResult AudioSamplePool::playNew(PAudioSample sample, PAudioDataSource source, bool positional, Pid pid) {
    update();
    if (!sample->Open(source)) {
        return SOUND_PLAYBACK_FAILED;
    }
    sample->Play(_looping, positional);
    _samplePool.push_back(AudioSamplePoolEntry(sample, SOUND_Invalid, pid, positional));
    return SOUND_PLAYBACK_SUCCEEDED;
}

SoundPlaybackResult AudioSamplePool::playUniqueSoundId(PAudioSample sample, PAudioDataSource source, SoundId id, bool positional, Pid pid) {
    update();
    for (AudioSamplePoolEntry &entry : _samplePool) {
        if (entry.id == id) {
            return SOUND_PLAYBACK_SKIPPED;
        }
    }
    if (!sample->Open(source)) {
        return SOUND_PLAYBACK_FAILED;
    }
    sample->Play(_looping, positional);
    _samplePool.push_back(AudioSamplePoolEntry(sample, id, pid, positional));
    return SOUND_PLAYBACK_SUCCEEDED;
}

SoundPlaybackResult AudioSamplePool::playUniquePid(PAudioSample sample, PAudioDataSource source, Pid pid, bool positional) {
    update();
    for (AudioSamplePoolEntry &entry : _samplePool) {
        if (entry.pid == pid) {
            return SOUND_PLAYBACK_SKIPPED;
        }
    }
    if (!sample->Open(source)) {
        return SOUND_PLAYBACK_FAILED;
    }
    sample->Play(_looping, positional);
    _samplePool.push_back(AudioSamplePoolEntry(sample, SOUND_Invalid, pid, positional));
    return SOUND_PLAYBACK_SUCCEEDED;
}

void AudioSamplePool::pause() {
    update();
    for (AudioSamplePoolEntry &entry : _samplePool) {
        entry.samplePtr->Pause();
    }
}

void AudioSamplePool::resume() {
    update();
    for (AudioSamplePoolEntry &entry : _samplePool) {
        entry.samplePtr->Resume();
    }
}

void AudioSamplePool::stop() {
    for (AudioSamplePoolEntry &entry : _samplePool) {
        entry.samplePtr->Stop();
    }
    _samplePool.clear();
}

void AudioSamplePool::stopSoundId(SoundId soundId) {
    assert(soundId != SOUND_Invalid);

    auto it = _samplePool.begin();
    while (it != _samplePool.end()) {
        if ((*it).id == soundId) {
            (*it).samplePtr->Stop();
            it = _samplePool.erase(it);
        } else {
            it++;
        }
    }
}

void AudioSamplePool::stopPid(Pid pid) {
    assert(pid != Pid());

    auto it = _samplePool.begin();
    while (it != _samplePool.end()) {
        if ((*it).pid == pid) {
            (*it).samplePtr->Stop();
            it = _samplePool.erase(it);
        } else {
            it++;
        }
    }
}

void AudioSamplePool::update() {
    auto it = _samplePool.begin();
    std::erase_if(_samplePool, [](const AudioSamplePoolEntry& entry) { return entry.samplePtr->IsStopped(); });
}

void AudioSamplePool::updatePositions(const std::function<std::optional<Vec3f>(Pid)> &positionOf) {
    for (AudioSamplePoolEntry &entry : _samplePool) {
        if (!entry.positional || !entry.pid)
            continue;
        if (std::optional<Vec3f> position = positionOf(entry.pid))
            entry.samplePtr->SetPosition(position->x, position->y, position->z, MAX_SOUND_DIST);
    }
}

void AudioSamplePool::setVolume(float value) {
    for (AudioSamplePoolEntry &entry : _samplePool) {
        entry.samplePtr->SetVolume(value);
    }
}

bool AudioSamplePool::hasPlaying() {
    for (AudioSamplePoolEntry &entry : _samplePool) {
        if (!entry.samplePtr->IsStopped()) {
            return true;
        }
    }
    return false;
}
