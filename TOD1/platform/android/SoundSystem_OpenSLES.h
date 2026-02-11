/*
 * SoundSystem_OpenSLES.h
 *
 * OpenSL ES audio backend replacing DirectSound.
 * Provides 3D positional audio, streaming, and sound buffer management.
 */
#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

// ---------------------------------------------------------------------------
// Sound buffer (replaces LPDIRECTSOUNDBUFFER)
// ---------------------------------------------------------------------------
class SoundBufferSLES {
public:
    SoundBufferSLES();
    ~SoundBufferSLES();

    bool    Create(SLEngineItf engineItf, SLObjectItf outputMixObj,
                   uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample,
                   const void* data, uint32_t dataSize);
    bool    CreateStreaming(SLEngineItf engineItf, SLObjectItf outputMixObj,
                            uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample,
                            uint32_t bufferCount, uint32_t bufferSize);

    void    Play(bool looped = false);
    void    Stop();
    void    Pause();
    void    Resume();
    bool    IsPlaying() const;

    void    SetVolume(float volume);    // 0.0 - 1.0
    float   GetVolume() const;
    void    SetPan(float pan);          // -1.0 (left) to 1.0 (right)
    float   GetPan() const;
    void    SetFrequency(float freq);   // Playback rate multiplier
    float   GetFrequency() const;

    // 3D sound properties (simulated via panning/volume attenuation)
    void    SetPosition(float x, float y, float z);
    void    SetMinDistance(float dist);
    void    SetMaxDistance(float dist);
    void    SetRollOff(float rolloff);

    // Streaming support
    void    EnqueueBuffer(const void* data, uint32_t size);
    void    SetStreamCallback(void (*callback)(SoundBufferSLES*, void*), void* userData);

    // Cleanup
    void    Destroy();

private:
    static void BufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void* context);

    SLObjectItf         m_PlayerObj;
    SLPlayItf           m_PlayItf;
    SLVolumeItf         m_VolumeItf;
    SLPlaybackRateItf   m_RateItf;
    SLAndroidSimpleBufferQueueItf m_BufferQueueItf;

    float               m_Volume;
    float               m_Pan;
    float               m_Frequency;
    bool                m_Looped;

    // 3D simulation
    float               m_PosX, m_PosY, m_PosZ;
    float               m_MinDistance;
    float               m_MaxDistance;
    float               m_RollOff;

    // Streaming
    void                (*m_StreamCallback)(SoundBufferSLES*, void*);
    void*               m_StreamUserData;

    // Data for non-streaming playback
    uint8_t*            m_Data;
    uint32_t            m_DataSize;
};

// ---------------------------------------------------------------------------
// SoundSystem_OpenSLES - main audio system (replaces DieselPowerSound/DirectSound)
// ---------------------------------------------------------------------------
class SoundSystem_OpenSLES {
public:
    SoundSystem_OpenSLES();
    ~SoundSystem_OpenSLES();

    bool    Initialize();
    void    Shutdown();

    // Listener (camera/player position for 3D audio)
    void    SetListenerPosition(float x, float y, float z);
    void    SetListenerOrientation(float fwdX, float fwdY, float fwdZ,
                                    float upX, float upY, float upZ);
    void    SetMasterVolume(float volume);
    float   GetMasterVolume() const;

    // Sound buffer creation
    SoundBufferSLES*    CreateSoundBuffer(uint32_t sampleRate, uint32_t channels,
                                           uint32_t bitsPerSample,
                                           const void* data, uint32_t dataSize);
    SoundBufferSLES*    CreateStreamingBuffer(uint32_t sampleRate, uint32_t channels,
                                              uint32_t bitsPerSample,
                                              uint32_t bufferCount, uint32_t bufferSize);

    // Update (call each frame to update 3D audio simulation)
    void    Update();

    // Engine interfaces
    SLEngineItf     GetEngine() const { return m_EngineItf; }
    SLObjectItf     GetOutputMix() const { return m_OutputMixObj; }

    // Singleton
    static SoundSystem_OpenSLES* GetInstance() { return s_Instance; }

private:
    // OpenSL ES engine objects
    SLObjectItf     m_EngineObj;
    SLEngineItf     m_EngineItf;
    SLObjectItf     m_OutputMixObj;

    // Listener state
    float           m_ListenerX, m_ListenerY, m_ListenerZ;
    float           m_ListenerFwdX, m_ListenerFwdY, m_ListenerFwdZ;
    float           m_MasterVolume;

    // Active sound buffers
    std::vector<SoundBufferSLES*> m_ActiveBuffers;
    std::mutex      m_BuffersMutex;

    static SoundSystem_OpenSLES* s_Instance;
};
