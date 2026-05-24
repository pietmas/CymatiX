// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <app/Config.h>
#include <app/SharedTypes.h>
#include <audio/AudioEngine.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace audio
{

// all miniaudio state avoids miniaudio.h in header
struct AudioEngineImpl
{
    ma_context m_context{};
    ma_decoder decoder{};
    ma_device device{};
    ma_rb rb{};

    bool contextInitialized = false;
    bool decoderInitialized = false;
    bool deviceInitialized = false;
    bool rbInitialized = false;
    bool playing = false;

    bool m_loopbackAvailable = false;
    int m_monitorDeviceIndex = -1;
    std::string m_lastFilePath;
    std::vector<std::string> m_captureNames;
    std::vector<ma_device_id> m_captureIds;
    AudioSource m_currentSource = AudioSource::File;

    uint32_t sampleRate = Config::SAMPLE_RATE;

    // ring buffer holds 8192 mono float32 samples
    static constexpr uint32_t RING_BUFFER_FRAMES = 8192;
};

// capture callback: reads pInput, downmix to mono, pushes to ring buffer
static void
captureCallback(ma_device *pDevice, void * /*pOutput*/, const void *pInput, ma_uint32 frameCount)
{
    auto *impl = (AudioEngineImpl *)pDevice->pUserData;
    if (!pInput)
    {
        return;
    }

    const float *src = (const float *)pInput;
    uint32_t channels = pDevice->capture.channels;

    void *pWrite;
    size_t bytesToWrite = frameCount * sizeof(float);

    if (ma_rb_acquire_write(&impl->rb, &bytesToWrite, &pWrite) != MA_SUCCESS || bytesToWrite == 0)
    {
        return;
    }

    float *dst = (float *)pWrite;
    uint32_t framesToWrite = (uint32_t)(bytesToWrite / sizeof(float));

    for (uint32_t i = 0; i < framesToWrite; i++)
    {
        float mono = 0.0f;
        for (uint32_t c = 0; c < channels; c++)
        {
            mono += src[i * channels + c];
        }
        dst[i] = mono / (float)channels;
    }

    ma_rb_commit_write(&impl->rb, framesToWrite * sizeof(float));
}

// open capture device at given enumerated index
static bool openMicrophone(AudioEngineImpl *impl, int deviceIndex)
{
    if (deviceIndex < 0 || (size_t)deviceIndex >= impl->m_captureIds.size())
    {
        fprintf(stderr, "[AudioEngine] invalid device index %d\n", deviceIndex);
        return false;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = 44100;
    cfg.capture.pDeviceID = &impl->m_captureIds[deviceIndex];
    cfg.periodSizeInFrames = 512;
    cfg.dataCallback = captureCallback;
    cfg.pUserData = impl;

    ma_result r = ma_device_init(&impl->m_context, &cfg, &impl->device);
    if (r != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] mic init failed (result=%d)\n", (int)r);
        return false;
    }

    impl->deviceInitialized = true;
    return true;
}

// open system loopback device (captures what the system is currently playing)
static bool openLoopback(AudioEngineImpl *impl)
{
    if (!impl->m_loopbackAvailable)
    {
        fprintf(stderr, "[AudioEngine] loopback not available on this system\n");
        return false;
    }

    ma_device_config cfg;
    if (impl->m_monitorDeviceIndex >= 0)
    {
        // Linux/PipeWire: open .monitor source as a regular capture device
        cfg = ma_device_config_init(ma_device_type_capture);
        cfg.capture.pDeviceID = &impl->m_captureIds[impl->m_monitorDeviceIndex];
    }
    else
    {
        // Windows/WASAPI: use native loopback device type
        cfg = ma_device_config_init(ma_device_type_loopback);
    }
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = 44100;
    cfg.periodSizeInFrames = 512;
    cfg.dataCallback = captureCallback;
    cfg.pUserData = impl;

    ma_result r = ma_device_init(&impl->m_context, &cfg, &impl->device);
    if (r != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] loopback init failed (result=%d)\n", (int)r);
        // device didnt open mark unavailable so UI grays it out next time
        impl->m_loopbackAvailable = false;
        return false;
    }

    impl->deviceInitialized = true;
    return true;
}

// audio callback runs on miniaudio background thread
static void
dataCallback(ma_device *pDevice, void *pOutput, const void * /*pInput*/, ma_uint32 frameCount)
{
    auto *impl = (AudioEngineImpl *)pDevice->pUserData;

    ma_uint64 framesRead = 0;
    ma_result res = ma_decoder_read_pcm_frames(&impl->decoder, pOutput, frameCount, &framesRead);

    // loop at EOF
    if (res == MA_AT_END || framesRead < frameCount)
    {
        ma_decoder_seek_to_pcm_frame(&impl->decoder, 0);
    }

    // mix to mono float32, push to ring buffer for FFT
    const float *src = (const float *)pOutput;
    uint32_t channels = pDevice->playback.channels;

    void *pWrite;
    size_t bytesToWrite = framesRead * sizeof(float);

    if (ma_rb_acquire_write(&impl->rb, &bytesToWrite, &pWrite) == MA_SUCCESS && bytesToWrite > 0)
    {
        float *dst = (float *)pWrite;
        uint32_t framesToWrite = (uint32_t)(bytesToWrite / sizeof(float));

        for (uint32_t i = 0; i < framesToWrite; i++)
        {
            float mono = 0.0f;
            for (uint32_t c = 0; c < channels; c++)
            {
                mono += src[i * channels + c];
            }
            dst[i] = mono / (float)channels;
        }

        ma_rb_commit_write(&impl->rb, framesToWrite * sizeof(float));
    }
}

AudioEngine::AudioEngine() : m_impl(std::make_unique<AudioEngineImpl>())
{
    // explicit context required for ma_context_get_devices()
    ma_result r = ma_context_init(nullptr, 0, nullptr, &m_impl->m_context);
    if (r != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] failed to init context\n");
        return;
    }
    m_impl->contextInitialized = true;
}

// stop device before uninit
AudioEngine::~AudioEngine()
{
    if (!m_impl)
    {
        return;
    }

    if (m_impl->deviceInitialized)
    {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
    }

    if (m_impl->decoderInitialized)
    {
        ma_decoder_uninit(&m_impl->decoder);
    }

    if (m_impl->rbInitialized)
    {
        ma_rb_uninit(&m_impl->rb);
    }

    if (m_impl->contextInitialized)
    {
        ma_context_uninit(&m_impl->m_context);
    }
}

// load audio file, set up playback device
bool AudioEngine::load(const std::string &path)
{
    // stop + clean up previous device/decoder
    if (m_impl->deviceInitialized)
    {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInitialized = false;
        m_impl->playing = false;
    }

    if (m_impl->decoderInitialized)
    {
        ma_decoder_uninit(&m_impl->decoder);
        m_impl->decoderInitialized = false;
    }

    // float32 output, miniaudio handles resample + channel convert
    ma_decoder_config decoderCfg = ma_decoder_config_init(ma_format_f32, 0, 0);

    ma_result res = ma_decoder_init_file(path.c_str(), &decoderCfg, &m_impl->decoder);
    if (res != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] could not open file: %s\n", path.c_str());
        return false;
    }
    m_impl->decoderInitialized = true;

    // get decoder output format for device config
    ma_uint32 channels;
    ma_uint32 sampleRate;
    ma_decoder_get_data_format(&m_impl->decoder, nullptr, &channels, &sampleRate, nullptr, 0);
    m_impl->sampleRate = sampleRate;

    // init ring buffer
    if (!m_impl->rbInitialized)
    {
        res = ma_rb_init(
            AudioEngineImpl::RING_BUFFER_FRAMES * sizeof(float),
            nullptr,
            nullptr,
            &m_impl->rb
        );
        if (res != MA_SUCCESS)
        {
            fprintf(stderr, "[AudioEngine] failed to create ring buffer\n");
            return false;
        }
        m_impl->rbInitialized = true;
    }
    else
    {
        ma_rb_reset(&m_impl->rb);
    }

    // playback device to match decoder
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate = sampleRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = m_impl.get();

    res = ma_device_init(&m_impl->m_context, &cfg, &m_impl->device);
    if (res != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] failed to initalize device\n");
        return false;
    }
    m_impl->deviceInitialized = true;
    m_impl->m_lastFilePath = path;

    printf("[AudioEngine] loaded: %s  (%u ch, %u Hz)\n", path.c_str(), channels, sampleRate);

    return true;
}

// start
void AudioEngine::play()
{
    if (!m_impl->deviceInitialized)
    {
        return;
    }
    ma_device_start(&m_impl->device);
    m_impl->playing = true;
}

// pause, keep position
void AudioEngine::pause()
{
    if (!m_impl->deviceInitialized)
    {
        return;
    }
    ma_device_stop(&m_impl->device);
    m_impl->playing = false;
}

// drain up to count mono float32 samples into buf
uint32_t AudioEngine::getLatestSamples(float *buf, uint32_t count)
{
    if (!m_impl->rbInitialized)
    {
        return 0;
    }

    void *pRead;
    size_t bytesToRead = count * sizeof(float);

    if (ma_rb_acquire_read(&m_impl->rb, &bytesToRead, &pRead) != MA_SUCCESS || bytesToRead == 0)
    {
        return 0;
    }

    uint32_t framesRead = (uint32_t)(bytesToRead / sizeof(float));
    memcpy(buf, pRead, framesRead * sizeof(float));
    ma_rb_commit_read(&m_impl->rb, bytesToRead);

    return framesRead;
}

bool AudioEngine::isPlaying() const
{
    return m_impl && m_impl->playing;
}
uint32_t AudioEngine::getSampleRate() const
{
    return m_impl ? m_impl->sampleRate : Config::SAMPLE_RATE;
}

// list capture devices and probe loopback availability
void AudioEngine::enumerateCaptureDevices()
{
    if (!m_impl->contextInitialized)
    {
        return;
    }

    ma_device_info *pCaptureDevices = nullptr;
    ma_uint32 captureCount = 0;
    ma_result r = ma_context_get_devices(
        &m_impl->m_context,
        nullptr,
        nullptr,
        &pCaptureDevices,
        &captureCount
    );
    if (r != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] failed to enumerate devices\n");
        return;
    }

    m_impl->m_captureNames.clear();
    m_impl->m_captureIds.clear();
    for (ma_uint32 i = 0; i < captureCount; i++)
    {
        m_impl->m_captureNames.push_back(pCaptureDevices[i].name);
        m_impl->m_captureIds.push_back(pCaptureDevices[i].id);
    }

    // first: look for a monitor capture source (PipeWire/PulseAudio on Linux)
    m_impl->m_monitorDeviceIndex = -1;
    const std::string monitorPrefix = "Monitor of ";
    const std::string monitorSuffix = ".monitor";
    for (int i = 0; i < (int)m_impl->m_captureNames.size(); i++)
    {
        const std::string &name = m_impl->m_captureNames[i];
        bool isPrefixMatch = name.size() >= monitorPrefix.size() &&
                             name.compare(0, monitorPrefix.size(), monitorPrefix) == 0;
        bool isSuffixMatch =
            name.size() >= monitorSuffix.size() &&
            name.compare(name.size() - monitorSuffix.size(), monitorSuffix.size(), monitorSuffix) ==
                0;
        if (isPrefixMatch || isSuffixMatch)
        {
            m_impl->m_monitorDeviceIndex = i;
            m_impl->m_loopbackAvailable = true;
            printf("[AudioEngine] loopback via monitor source: %s\n", name.c_str());
            break;
        }
    }

    // fallback: probe native loopback device (WASAPI on Windows)
    if (!m_impl->m_loopbackAvailable)
    {
        ma_device_config loopbackCfg = ma_device_config_init(ma_device_type_loopback);
        loopbackCfg.capture.format = ma_format_f32;
        loopbackCfg.capture.channels = 1;
        loopbackCfg.sampleRate = 44100;
        loopbackCfg.dataCallback = captureCallback;
        loopbackCfg.pUserData = m_impl.get();
        ma_device probe{};
        ma_result lr = ma_device_init(&m_impl->m_context, &loopbackCfg, &probe);
        if (lr == MA_SUCCESS)
        {
            ma_device_uninit(&probe);
            m_impl->m_loopbackAvailable = true;
            printf("[AudioEngine] loopback via native loopback device\n");
        }
        else
        {
            fprintf(stderr, "[AudioEngine] loopback not available (result=%d)\n", (int)lr);
            m_impl->m_loopbackAvailable = false;
        }
    }
}

// return names of enumerated capture devices
std::vector<std::string> AudioEngine::getCaptureDeviceNames() const
{
    return m_impl->m_captureNames;
}

// return whether loopback was successfully probed at startup
bool AudioEngine::isLoopbackAvailable() const
{
    return m_impl && m_impl->m_loopbackAvailable;
}

// teardown current device, flush ring buffer, init new source
bool AudioEngine::switchSource(AudioSource src, int deviceIndex)
{
    // stop and release the current device before changing source
    if (m_impl->deviceInitialized)
    {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInitialized = false;
        m_impl->playing = false;
    }

    // flush stale samples so old source doesnt bleed into new FFT frames
    if (m_impl->rbInitialized)
    {
        ma_rb_reset(&m_impl->rb);
    }

    // ring buffer may not exist yet on first call before any load()
    if (!m_impl->rbInitialized)
    {
        ma_result r = ma_rb_init(
            AudioEngineImpl::RING_BUFFER_FRAMES * sizeof(float),
            nullptr,
            nullptr,
            &m_impl->rb
        );
        if (r != MA_SUCCESS)
        {
            fprintf(stderr, "[AudioEngine] failed to init ring buffer\n");
            return false;
        }
        m_impl->rbInitialized = true;
    }

    if (src == AudioSource::File)
    {
        if (m_impl->m_lastFilePath.empty())
        {
            fprintf(stderr, "[AudioEngine] no file loaded, cant switch to File\n");
            return false;
        }
        bool ok = load(m_impl->m_lastFilePath);
        if (ok)
        {
            play();
        }
        return ok;
    }

    if (src == AudioSource::Microphone)
    {
        bool ok = openMicrophone(m_impl.get(), deviceIndex);
        if (ok)
        {
            ma_device_start(&m_impl->device);
            m_impl->playing = true;
            m_impl->m_currentSource = AudioSource::Microphone;
        }
        return ok;
    }

    if (src == AudioSource::Loopback)
    {
        bool ok = openLoopback(m_impl.get());
        if (ok)
        {
            ma_device_start(&m_impl->device);
            m_impl->playing = true;
            m_impl->m_currentSource = AudioSource::Loopback;
        }
        return ok;
    }

    fprintf(stderr, "[AudioEngine] unknown source\n");
    return false;
}

} // namespace audio
