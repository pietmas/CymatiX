// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <audio/AudioEngine.h>

#include <cstdio>
#include <cstring>

namespace audio
{

// all miniaudio state avoids miniaudio.h in header
struct AudioEngineImpl
{
    ma_decoder decoder{};
    ma_device device{};
    ma_rb rb{};

    bool decoderInitialized = false;
    bool deviceInitialized = false;
    bool rbInitialized = false;
    bool playing = false;

    uint32_t sampleRate = 44100;

    // ring buffer holds 8192 mono float32 samples
    static constexpr uint32_t RING_BUFFER_FRAMES = 8192;
};

// audio callback runs on miniaudio background thread
static void
dataCallback(ma_device *pDevice, void *pOutput, const void * /*pInput*/, ma_uint32 frameCount)
{
    auto *impl = (AudioEngineImpl *)pDevice->pUserData;

    ma_uint64 framesRead = 0;
    ma_result res = ma_decoder_read_pcm_frames(&impl->decoder, pOutput, frameCount, &framesRead);

    // loop at EOF
    if (res == MA_AT_END || framesRead < frameCount)
        ma_decoder_seek_to_pcm_frame(&impl->decoder, 0);

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
                mono += src[i * channels + c];
            dst[i] = mono / (float)channels;
        }

        ma_rb_commit_write(&impl->rb, framesToWrite * sizeof(float));
    }
}

AudioEngine::AudioEngine() : m_impl(std::make_unique<AudioEngineImpl>()) {}

// stop device before uninit
AudioEngine::~AudioEngine()
{
    if (!m_impl)
        return;

    if (m_impl->deviceInitialized)
    {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
    }

    if (m_impl->decoderInitialized)
        ma_decoder_uninit(&m_impl->decoder);

    if (m_impl->rbInitialized)
        ma_rb_uninit(&m_impl->rb);
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

    res = ma_device_init(nullptr, &cfg, &m_impl->device);
    if (res != MA_SUCCESS)
    {
        fprintf(stderr, "[AudioEngine] failed to initalize device\n");
        return false;
    }
    m_impl->deviceInitialized = true;

    printf("[AudioEngine] loaded: %s  (%u ch, %u Hz)\n", path.c_str(), channels, sampleRate);

    return true;
}

// start
void AudioEngine::play()
{
    if (!m_impl->deviceInitialized)
        return;
    ma_device_start(&m_impl->device);
    m_impl->playing = true;
}

// pause, keep position
void AudioEngine::pause()
{
    if (!m_impl->deviceInitialized)
        return;
    ma_device_stop(&m_impl->device);
    m_impl->playing = false;
}

// drain up to count mono float32 samples into buf
uint32_t AudioEngine::getLatestSamples(float *buf, uint32_t count)
{
    if (!m_impl->rbInitialized)
        return 0;

    void *pRead;
    size_t bytesToRead = count * sizeof(float);

    if (ma_rb_acquire_read(&m_impl->rb, &bytesToRead, &pRead) != MA_SUCCESS || bytesToRead == 0)
        return 0;

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
    return m_impl ? m_impl->sampleRate : 44100;
}

} // namespace audio
