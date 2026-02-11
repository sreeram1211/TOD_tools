/*
 * SoundSystem_OpenSLES.cpp
 *
 * OpenSL ES audio backend implementation.
 */
#include "SoundSystem_OpenSLES.h"
#include "Platform_Android.h"
#include <cmath>
#include <cstring>
#include <algorithm>

SoundSystem_OpenSLES* SoundSystem_OpenSLES::s_Instance = nullptr;

// ===========================================================================
// SoundBufferSLES
// ===========================================================================
SoundBufferSLES::SoundBufferSLES()
    : m_PlayerObj(nullptr)
    , m_PlayItf(nullptr)
    , m_VolumeItf(nullptr)
    , m_RateItf(nullptr)
    , m_BufferQueueItf(nullptr)
    , m_Volume(1.0f)
    , m_Pan(0.0f)
    , m_Frequency(1.0f)
    , m_Looped(false)
    , m_PosX(0), m_PosY(0), m_PosZ(0)
    , m_MinDistance(1.0f)
    , m_MaxDistance(100.0f)
    , m_RollOff(1.0f)
    , m_StreamCallback(nullptr)
    , m_StreamUserData(nullptr)
    , m_Data(nullptr)
    , m_DataSize(0)
{}

SoundBufferSLES::~SoundBufferSLES() {
    Destroy();
}

bool SoundBufferSLES::Create(SLEngineItf engineItf, SLObjectItf outputMixObj,
                              uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample,
                              const void* data, uint32_t dataSize) {
    // Data source configuration
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
    };

    SLuint32 slSampleRate;
    switch (sampleRate) {
        case 8000:  slSampleRate = SL_SAMPLINGRATE_8;     break;
        case 11025: slSampleRate = SL_SAMPLINGRATE_11_025; break;
        case 22050: slSampleRate = SL_SAMPLINGRATE_22_05;  break;
        case 44100: slSampleRate = SL_SAMPLINGRATE_44_1;   break;
        case 48000: slSampleRate = SL_SAMPLINGRATE_48;     break;
        default:    slSampleRate = SL_SAMPLINGRATE_44_1;   break;
    }

    SLuint32 slChannelMask = (channels == 1) ?
        SL_SPEAKER_FRONT_CENTER :
        (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);

    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        channels,
        slSampleRate,
        (SLuint32)(bitsPerSample),
        (SLuint32)(bitsPerSample),
        slChannelMask,
        SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    // Data sink (output mix)
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, outputMixObj };
    SLDataSink audioSnk = { &loc_outmix, nullptr };

    // Create audio player
    const SLInterfaceID ids[] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME, SL_IID_PLAYBACKRATE };
    const SLboolean req[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE };

    SLresult result = (*engineItf)->CreateAudioPlayer(engineItf, &m_PlayerObj,
                                                       &audioSrc, &audioSnk,
                                                       3, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("CreateAudioPlayer failed: %d", result);
        return false;
    }

    result = (*m_PlayerObj)->Realize(m_PlayerObj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Player Realize failed: %d", result);
        return false;
    }

    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_PLAY, &m_PlayItf);
    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_BUFFERQUEUE, &m_BufferQueueItf);
    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_VOLUME, &m_VolumeItf);
    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_PLAYBACKRATE, &m_RateItf);

    // Register buffer queue callback
    (*m_BufferQueueItf)->RegisterCallback(m_BufferQueueItf, BufferQueueCallback, this);

    // Copy sound data
    if (data && dataSize > 0) {
        m_Data = new uint8_t[dataSize];
        memcpy(m_Data, data, dataSize);
        m_DataSize = dataSize;
    }

    return true;
}

bool SoundBufferSLES::CreateStreaming(SLEngineItf engineItf, SLObjectItf outputMixObj,
                                      uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample,
                                      uint32_t bufferCount, uint32_t bufferSize) {
    // Same setup as Create but with more buffer queue slots
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, bufferCount
    };

    SLuint32 slSampleRate;
    switch (sampleRate) {
        case 8000:  slSampleRate = SL_SAMPLINGRATE_8;     break;
        case 11025: slSampleRate = SL_SAMPLINGRATE_11_025; break;
        case 22050: slSampleRate = SL_SAMPLINGRATE_22_05;  break;
        case 44100: slSampleRate = SL_SAMPLINGRATE_44_1;   break;
        case 48000: slSampleRate = SL_SAMPLINGRATE_48;     break;
        default:    slSampleRate = SL_SAMPLINGRATE_44_1;   break;
    }

    SLuint32 slChannelMask = (channels == 1) ?
        SL_SPEAKER_FRONT_CENTER :
        (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT);

    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        channels,
        slSampleRate,
        (SLuint32)(bitsPerSample),
        (SLuint32)(bitsPerSample),
        slChannelMask,
        SL_BYTEORDER_LITTLEENDIAN
    };

    SLDataSource audioSrc = { &loc_bufq, &format_pcm };
    SLDataLocator_OutputMix loc_outmix = { SL_DATALOCATOR_OUTPUTMIX, outputMixObj };
    SLDataSink audioSnk = { &loc_outmix, nullptr };

    const SLInterfaceID ids[] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME };
    const SLboolean req[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };

    SLresult result = (*engineItf)->CreateAudioPlayer(engineItf, &m_PlayerObj,
                                                       &audioSrc, &audioSnk,
                                                       2, ids, req);
    if (result != SL_RESULT_SUCCESS) return false;

    (*m_PlayerObj)->Realize(m_PlayerObj, SL_BOOLEAN_FALSE);
    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_PLAY, &m_PlayItf);
    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_BUFFERQUEUE, &m_BufferQueueItf);
    (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_VOLUME, &m_VolumeItf);

    (*m_BufferQueueItf)->RegisterCallback(m_BufferQueueItf, BufferQueueCallback, this);
    return true;
}

void SoundBufferSLES::BufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    SoundBufferSLES* self = (SoundBufferSLES*)context;

    if (self->m_Looped && self->m_Data && self->m_DataSize > 0) {
        (*bq)->Enqueue(bq, self->m_Data, self->m_DataSize);
    }

    if (self->m_StreamCallback) {
        self->m_StreamCallback(self, self->m_StreamUserData);
    }
}

void SoundBufferSLES::Play(bool looped) {
    if (!m_PlayItf) return;

    m_Looped = looped;

    if (m_Data && m_DataSize > 0 && m_BufferQueueItf) {
        (*m_BufferQueueItf)->Clear(m_BufferQueueItf);
        (*m_BufferQueueItf)->Enqueue(m_BufferQueueItf, m_Data, m_DataSize);
    }

    (*m_PlayItf)->SetPlayState(m_PlayItf, SL_PLAYSTATE_PLAYING);
}

void SoundBufferSLES::Stop() {
    if (!m_PlayItf) return;
    (*m_PlayItf)->SetPlayState(m_PlayItf, SL_PLAYSTATE_STOPPED);
    if (m_BufferQueueItf)
        (*m_BufferQueueItf)->Clear(m_BufferQueueItf);
}

void SoundBufferSLES::Pause() {
    if (m_PlayItf)
        (*m_PlayItf)->SetPlayState(m_PlayItf, SL_PLAYSTATE_PAUSED);
}

void SoundBufferSLES::Resume() {
    if (m_PlayItf)
        (*m_PlayItf)->SetPlayState(m_PlayItf, SL_PLAYSTATE_PLAYING);
}

bool SoundBufferSLES::IsPlaying() const {
    if (!m_PlayItf) return false;
    SLuint32 state;
    (*m_PlayItf)->GetPlayState(m_PlayItf, &state);
    return state == SL_PLAYSTATE_PLAYING;
}

void SoundBufferSLES::SetVolume(float volume) {
    m_Volume = volume;
    if (m_VolumeItf) {
        // Convert linear 0.0-1.0 to millibels
        SLmillibel mb;
        if (volume <= 0.0f)
            mb = SL_MILLIBEL_MIN;
        else
            mb = (SLmillibel)(2000.0f * log10f(volume));
        (*m_VolumeItf)->SetVolumeLevel(m_VolumeItf, mb);
    }
}

float SoundBufferSLES::GetVolume() const { return m_Volume; }

void SoundBufferSLES::SetPan(float pan) {
    m_Pan = pan;
    // OpenSL ES doesn't have a direct pan interface on all devices
    // Simulated via stereo volume if needed
}

float SoundBufferSLES::GetPan() const { return m_Pan; }

void SoundBufferSLES::SetFrequency(float freq) {
    m_Frequency = freq;
    if (m_RateItf) {
        SLpermille rate = (SLpermille)(freq * 1000.0f);
        (*m_RateItf)->SetRate(m_RateItf, rate);
    }
}

float SoundBufferSLES::GetFrequency() const { return m_Frequency; }

void SoundBufferSLES::SetPosition(float x, float y, float z) {
    m_PosX = x; m_PosY = y; m_PosZ = z;
}

void SoundBufferSLES::SetMinDistance(float dist) { m_MinDistance = dist; }
void SoundBufferSLES::SetMaxDistance(float dist) { m_MaxDistance = dist; }
void SoundBufferSLES::SetRollOff(float rolloff) { m_RollOff = rolloff; }

void SoundBufferSLES::EnqueueBuffer(const void* data, uint32_t size) {
    if (m_BufferQueueItf && data && size > 0) {
        (*m_BufferQueueItf)->Enqueue(m_BufferQueueItf, data, size);
    }
}

void SoundBufferSLES::SetStreamCallback(void (*callback)(SoundBufferSLES*, void*), void* userData) {
    m_StreamCallback = callback;
    m_StreamUserData = userData;
}

void SoundBufferSLES::Destroy() {
    if (m_PlayerObj) {
        (*m_PlayerObj)->Destroy(m_PlayerObj);
        m_PlayerObj = nullptr;
        m_PlayItf = nullptr;
        m_VolumeItf = nullptr;
        m_RateItf = nullptr;
        m_BufferQueueItf = nullptr;
    }
    if (m_Data) {
        delete[] m_Data;
        m_Data = nullptr;
        m_DataSize = 0;
    }
}

// ===========================================================================
// SoundSystem_OpenSLES
// ===========================================================================
SoundSystem_OpenSLES::SoundSystem_OpenSLES()
    : m_EngineObj(nullptr)
    , m_EngineItf(nullptr)
    , m_OutputMixObj(nullptr)
    , m_ListenerX(0), m_ListenerY(0), m_ListenerZ(0)
    , m_ListenerFwdX(0), m_ListenerFwdY(0), m_ListenerFwdZ(-1)
    , m_MasterVolume(1.0f)
{}

SoundSystem_OpenSLES::~SoundSystem_OpenSLES() {
    Shutdown();
}

bool SoundSystem_OpenSLES::Initialize() {
    s_Instance = this;

    // Create engine
    SLresult result = slCreateEngine(&m_EngineObj, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("slCreateEngine failed: %d", result);
        return false;
    }

    result = (*m_EngineObj)->Realize(m_EngineObj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Engine Realize failed: %d", result);
        return false;
    }

    result = (*m_EngineObj)->GetInterface(m_EngineObj, SL_IID_ENGINE, &m_EngineItf);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("GetInterface SL_IID_ENGINE failed: %d", result);
        return false;
    }

    // Create output mix
    result = (*m_EngineItf)->CreateOutputMix(m_EngineItf, &m_OutputMixObj, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("CreateOutputMix failed: %d", result);
        return false;
    }

    result = (*m_OutputMixObj)->Realize(m_OutputMixObj, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("OutputMix Realize failed: %d", result);
        return false;
    }

    LOGI("OpenSL ES audio system initialized");
    return true;
}

void SoundSystem_OpenSLES::Shutdown() {
    std::lock_guard<std::mutex> lock(m_BuffersMutex);

    for (auto* buf : m_ActiveBuffers) {
        buf->Destroy();
        delete buf;
    }
    m_ActiveBuffers.clear();

    if (m_OutputMixObj) {
        (*m_OutputMixObj)->Destroy(m_OutputMixObj);
        m_OutputMixObj = nullptr;
    }
    if (m_EngineObj) {
        (*m_EngineObj)->Destroy(m_EngineObj);
        m_EngineObj = nullptr;
        m_EngineItf = nullptr;
    }

    s_Instance = nullptr;
    LOGI("OpenSL ES audio system shut down");
}

void SoundSystem_OpenSLES::SetListenerPosition(float x, float y, float z) {
    m_ListenerX = x; m_ListenerY = y; m_ListenerZ = z;
}

void SoundSystem_OpenSLES::SetListenerOrientation(float fwdX, float fwdY, float fwdZ,
                                                    float upX, float upY, float upZ) {
    m_ListenerFwdX = fwdX; m_ListenerFwdY = fwdY; m_ListenerFwdZ = fwdZ;
}

void SoundSystem_OpenSLES::SetMasterVolume(float volume) {
    m_MasterVolume = volume;
}

float SoundSystem_OpenSLES::GetMasterVolume() const {
    return m_MasterVolume;
}

SoundBufferSLES* SoundSystem_OpenSLES::CreateSoundBuffer(uint32_t sampleRate, uint32_t channels,
                                                          uint32_t bitsPerSample,
                                                          const void* data, uint32_t dataSize) {
    auto* buf = new SoundBufferSLES();
    if (!buf->Create(m_EngineItf, m_OutputMixObj, sampleRate, channels, bitsPerSample, data, dataSize)) {
        delete buf;
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_BuffersMutex);
    m_ActiveBuffers.push_back(buf);
    return buf;
}

SoundBufferSLES* SoundSystem_OpenSLES::CreateStreamingBuffer(uint32_t sampleRate, uint32_t channels,
                                                              uint32_t bitsPerSample,
                                                              uint32_t bufferCount, uint32_t bufferSize) {
    auto* buf = new SoundBufferSLES();
    if (!buf->CreateStreaming(m_EngineItf, m_OutputMixObj, sampleRate, channels, bitsPerSample, bufferCount, bufferSize)) {
        delete buf;
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_BuffersMutex);
    m_ActiveBuffers.push_back(buf);
    return buf;
}

void SoundSystem_OpenSLES::Update() {
    std::lock_guard<std::mutex> lock(m_BuffersMutex);

    // Update 3D sound simulation: adjust volume based on distance from listener
    for (auto* buf : m_ActiveBuffers) {
        if (!buf->IsPlaying()) continue;

        float dx = buf->m_PosX - m_ListenerX;
        float dy = buf->m_PosY - m_ListenerY;
        float dz = buf->m_PosZ - m_ListenerZ;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        // Skip non-positioned sounds (at origin)
        if (dist < 0.001f) continue;

        // Simple distance attenuation
        float minDist = buf->m_MinDistance;
        float maxDist = buf->m_MaxDistance;
        float rolloff = buf->m_RollOff;

        float attenuation = 1.0f;
        if (dist > minDist) {
            if (dist >= maxDist) {
                attenuation = 0.0f;
            } else {
                attenuation = minDist / (minDist + rolloff * (dist - minDist));
            }
        }

        buf->SetVolume(attenuation * m_MasterVolume);
    }

    // Clean up stopped buffers that are no longer needed
    m_ActiveBuffers.erase(
        std::remove_if(m_ActiveBuffers.begin(), m_ActiveBuffers.end(),
            [](SoundBufferSLES* buf) { return false; }),  // Don't auto-remove, let caller manage
        m_ActiveBuffers.end()
    );
}
