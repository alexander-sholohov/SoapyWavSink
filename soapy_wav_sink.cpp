#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Registry.hpp>

#include <AudioFile.h>

#include <chrono>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;

#define ENV_VAR_NAME "SOAPY_WAV_SINK_FILE"
#define SOAPY_FILE_NAME_KEY "file_name"

// clang-format off
// SoapySDRUtil --find="driver=wav_sink,file_name=HDSDR_20120317_155627Z_RDXC_CW_14045kHz_RF.wav"
// SoapySDRUtil --probe="driver=wav_sink,file_name=HDSDR_20120317_155627Z_RDXC_CW_14045kHz_RF.wav"
// SoapySDRUtil --args="driver=wav_sink,file_name=HDSDR_20120317_155627Z_RDXC_CW_14045kHz_RF.wav" --rate=96000 --channels=0 --direction=RX --format=CS16 
// SoapySDRUtil --args="driver=wav_sink,file_name=HDSDR_20131026_084332Z_28500kHz_RF_contest.wav" --rate=960000 --channels=0 --direction=RX --format=CS16
// clang-format on

struct WavStream
{
    WavStream(std::string fmt)
        : format(std::move(fmt))
    {
    }
    std::string format;
};

/***********************************************************************
 * Device interface
 **********************************************************************/
class WavSinkDevice : public SoapySDR::Device
{
  public:
    WavSinkDevice(std::string file_name)
        : _file_name(std::move(file_name))
    {

        if (!_audioFile.load(_file_name))
        {
            throw std::runtime_error("Unable to open wav file: " + _file_name);
        }

        if (_audioFile.getNumChannels() != 2)
        {
            _audioFile.printSummary();
            throw std::runtime_error("Audo file with 2 channels expected");
        }

        if (_audioFile.getBitDepth() != 16)
        {
            _audioFile.printSummary();
            throw std::runtime_error("Audo file with 16 bit depth expected");
        }

        // 256K samples min
        if (_audioFile.getNumSamplesPerChannel() < 256 * 1024)
        {
            _audioFile.printSummary();
            throw std::runtime_error("Audo file is too short");
        }
    };

    SoapySDR::Kwargs getHardwareInfo(void) const override
    {
        SoapySDR::Kwargs m;

        m[SOAPY_FILE_NAME_KEY] = _file_name;
        m["origin"] = "https://github.com/alexander-sholohov/SoapyAfedri";

        return m;
    }

    size_t getNumChannels(const int dir) const override
    {
        return (dir == SOAPY_SDR_RX) ? 1 : 0;
    }

    bool getFullDuplex(const int /*direction*/, const size_t /*channel*/) const override
    {
        return false;
    }

    double getSampleRate(const int direction, const size_t channel) const override
    {
        return static_cast<double>(_audioFile.getSampleRate());
    }

    std::vector<double> listSampleRates(const int direction, const size_t channel) const override
    {
        std::vector<double> result;
        result.push_back(static_cast<double>(_audioFile.getSampleRate()));

        return result;
    }

    std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const override
    {
        std::vector<std::string> formats;

        formats.push_back(SOAPY_SDR_CS16);
        formats.push_back(SOAPY_SDR_CF32);

        return formats;
    }

    std::string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const override
    {
        // Check that direction is SOAPY_SDR_RX
        if (direction != SOAPY_SDR_RX)
        {
            throw std::runtime_error("WavSink is RX only, use SOAPY_SDR_RX");
        }

        fullScale = 32768;
        return SOAPY_SDR_CS16;
    }

    SoapySDR::Stream *setupStream(const int direction, const std::string &format,
                                  const std::vector<size_t> &channels = std::vector<size_t>(),
                                  const SoapySDR::Kwargs &args = SoapySDR::Kwargs()) override
    {
        SoapySDR::logf(SOAPY_SDR_INFO, "WavSink in setupStream. Num_channels=%d, format=%s", channels.size(), format.c_str());

        if (direction != SOAPY_SDR_RX)
        {
            throw std::runtime_error("WavSink is RX only.");
        }

        if (format != SOAPY_SDR_CS16 && format != SOAPY_SDR_CF32)
        {
            SoapySDR::log(SOAPY_SDR_ERROR, "Invalid stream format");
            throw std::runtime_error("setupStream invalid format '" + format + "' -- Only CS16, CF32 are supported by WavSink module.");
        }

        return (SoapySDR::Stream *)(new WavStream(format));
    }

    void closeStream(SoapySDR::Stream *stream) override
    {
        this->deactivateStream(stream, 0, 0);
        delete reinterpret_cast<WavStream *>(stream);
    }

    int activateStream(SoapySDR::Stream *stream, const int flags = 0, const long long timeNs = 0, const size_t numElems = 0) override
    {
        const WavStream *wav_stream = reinterpret_cast<WavStream *>(stream);
        SoapySDR::logf(SOAPY_SDR_INFO, "WavSink in activateStream format=%s flags=%d ", wav_stream->format.c_str(), flags);

        if (flags != 0)
        {
            return SOAPY_SDR_NOT_SUPPORTED;
        }

        _rx_active = true;
        _samples_sent = 0;
        _pos = 0;
        _start_stamp = std::chrono::system_clock::now();
        return 0;
    }

    int deactivateStream(SoapySDR::Stream *stream, const int flags = 0, const long long timeNs = 0) override
    {
        // WavStream* wav_stream = reinterpret_cast<WavStream *>(stream);
        SoapySDR::logf(SOAPY_SDR_DEBUG, "WavSink in deactivateStream flags=%d", flags);

        if (flags != 0)
        {
            return SOAPY_SDR_NOT_SUPPORTED;
        }

        _rx_active = false;
        return 0;
    }

    int readStream(SoapySDR::Stream *stream, void *const *buffs, const size_t numElems, int &flags, long long &timeNs,
                   const long timeoutUs = 100000) override
    {
        const WavStream *wav_stream = reinterpret_cast<const WavStream *>(stream);

        // default slice size
        size_t slice_size = 32678;

        // The more sps we have the more slice size we need. The relationship is experimental.
        if (_audioFile.getSampleRate() > 1000000)
        {
            slice_size *= 4;
        }
        else if (_audioFile.getSampleRate() > 500000)
        {
            slice_size *= 2;
        }

        // bottom limit by requested size
        if (numElems < slice_size)
        {
            slice_size = numElems;
        }

        // prevent move out of the file
        if (_pos + slice_size > _audioFile.getNumSamplesPerChannel())
        {
            _pos = 0;
        }

        // calculate number of samples based on time elapsed since start
        const auto now_stamp = std::chrono::system_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_stamp - _start_stamp);
        int64_t desired_num_samples = elapsed.count() * _audioFile.getSampleRate() / 1000;
        // printf("elapsed=%ld desired_num_samples=%ld _samples_sent=%ld\n", elapsed.count(), desired_num_samples, _samples_sent);

        // small sleep if time for next silce is not exceeded
        if (desired_num_samples < _samples_sent + (int64_t)slice_size)
        {
            std::this_thread::sleep_for(5ms);
            return 0;
        }

        // fill destination buffer
        if (wav_stream->format == SOAPY_SDR_CF32)
        {
            // f32 format
            float *buffer = (float *)buffs[0]; // channel 0 only
            for (size_t idx = 0; idx < slice_size; idx++)
            {
                buffer[idx * 2 + 0] = (float)_audioFile.samples[0][_pos + idx] / 32768.0; // I
                buffer[idx * 2 + 1] = (float)_audioFile.samples[1][_pos + idx] / 32768.0; // Q
            }
        }
        else
        {
            // native short format
            short *buffer = (short *)buffs[0]; // channel 0 only
            for (size_t idx = 0; idx < slice_size; idx++)
            {
                buffer[idx * 2 + 0] = _audioFile.samples[0][_pos + idx]; // I
                buffer[idx * 2 + 1] = _audioFile.samples[1][_pos + idx]; // Q
            }
        }

        // adjust counters
        _pos += slice_size;
        _samples_sent += slice_size;

        return (int)slice_size;
    }

    // Implement all applicable virtual methods from SoapySDR::Device
  private:
    std::string _file_name;
    AudioFile<short> _audioFile;
    bool _rx_active{};
    std::chrono::system_clock::time_point _start_stamp;
    int64_t _samples_sent{};
    size_t _pos{};
};

/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findWavSinkDevice(const SoapySDR::Kwargs &args)
{
    auto res = SoapySDR::KwargsList();
    std::string file_name;

    // probe provided arguments first, next - check environment variable
    const auto it = args.find(SOAPY_FILE_NAME_KEY);
    if (it != args.end())
    {
        file_name = it->second;
    }
    else if (std::getenv(ENV_VAR_NAME))
    {
        file_name = std::getenv(ENV_VAR_NAME);
    }

    if (!file_name.empty())
    {
        auto m = SoapySDR::Kwargs();
        m["label"] = std::string("wav_sink :: " + file_name);
        m[SOAPY_FILE_NAME_KEY] = file_name;

        res.push_back(m);
    }

    return res;
}

/***********************************************************************
 * Make device instance
 **********************************************************************/
SoapySDR::Device *makeWavSinkDevice(const SoapySDR::Kwargs &args)
{
    SoapySDR::log(SOAPY_SDR_INFO, "WavSink is making device:");
    // for (auto it = args.begin(); it != args.end(); ++it)
    // {
    //     SoapySDR::logf(SOAPY_SDR_INFO, "wav_sink key: %s - %s", it->first.c_str(), it->second.c_str());
    // }

    if (args.count(SOAPY_FILE_NAME_KEY) == 0)
    {
        throw std::runtime_error("Unable to create WavSink device without file_name");
    }

    std::string file_name = args.at(SOAPY_FILE_NAME_KEY);
    return new WavSinkDevice(file_name);
}

/***********************************************************************
 * Registration
 **********************************************************************/
static SoapySDR::Registry registerMyDevice("wav_sink", &findWavSinkDevice, &makeWavSinkDevice, SOAPY_SDR_ABI_VERSION);
