/*******************************************************************************
 * OAISYS Badge - Robotic TTS Module Implementation
 *
 * Uses ESP32-SAM library for 1980s-style robotic speech synthesis.
 ******************************************************************************/

#include "robot_tts.h"
#include "../../config.h"

#include <driver/i2s.h>
#include <sam_arduino.h>  // Only included here to avoid multiple definition errors

//==============================================================================
// SAMI2SOutput - Custom I2S Output for SAM (using legacy I2S driver)
//==============================================================================
class SAMI2SOutput : public SAMOutputBase {
public:
    SAMI2SOutput(i2s_port_t port, int bclk, int lrc, int dout);
    ~SAMI2SOutput();

    void open() override;
    int close() override;
    int drain() override;
    bool write(byte* buffer, int bytes_count) override;
    const char* name() override { return "SAMI2SOutput"; }
    int channels() override { return 2; }  // SAM outputs stereo
    void setChannels(int ch) override {}   // Fixed at stereo

private:
    i2s_port_t _port;
    int _bclk;
    int _lrc;
    int _dout;
    bool _driverInstalled;
};

//==============================================================================
// SAMI2SOutput Implementation
//==============================================================================

SAMI2SOutput::SAMI2SOutput(i2s_port_t port, int bclk, int lrc, int dout)
    : _port(port)
    , _bclk(bclk)
    , _lrc(lrc)
    , _dout(dout)
    , _driverInstalled(false)
{
}

SAMI2SOutput::~SAMI2SOutput() {
    if (_driverInstalled) {
        close();
    }
}

void SAMI2SOutput::open() {
    if (_driverInstalled) return;

    // Configure I2S for SAM's native sample rate (22050 Hz, stereo, 16-bit)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMOutputBase::sampleRate(),  // 22050 Hz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = _bclk,
        .ws_io_num = _lrc,
        .data_out_num = _dout,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    if (i2s_driver_install(_port, &i2s_config, 0, NULL) != ESP_OK) {
        Serial.println("TTS: I2S driver install failed");
        return;
    }

    if (i2s_set_pin(_port, &pin_config) != ESP_OK) {
        Serial.println("TTS: I2S pin config failed");
        i2s_driver_uninstall(_port);
        return;
    }

    _driverInstalled = true;
    SAMOutputBase::open();
    Serial.printf("TTS: I2S initialized at %d Hz\n", SAMOutputBase::sampleRate());
}

int SAMI2SOutput::close() {
    if (_driverInstalled) {
        i2s_zero_dma_buffer(_port);
        i2s_driver_uninstall(_port);
        _driverInstalled = false;
    }
    return SAMOutputBase::close();
}

int SAMI2SOutput::drain() {
    if (_driverInstalled) {
        i2s_zero_dma_buffer(_port);
    }
    return 0;
}

bool SAMI2SOutput::write(byte* buffer, int bytes_count) {
    if (!_driverInstalled) return false;

    size_t bytes_written;
    esp_err_t result = i2s_write(_port, buffer, bytes_count, &bytes_written, portMAX_DELAY);

    if (result != ESP_OK) {
        Serial.println("TTS: I2S write failed");
        return false;
    }
    return true;
}

//==============================================================================
// RobotTTS Implementation
//==============================================================================

RobotTTS::RobotTTS()
    : _sam(nullptr)
    , _output(nullptr)
    , _ready(false)
{
}

RobotTTS::~RobotTTS() {
    end();
}

bool RobotTTS::begin() {
    if (_ready) return true;

    // Create I2S output for speaker
    _output = new SAMI2SOutput(SPK_I2S_PORT, SPK_BCLK, SPK_LRC, SPK_DOUT);
    if (!_output) {
        Serial.println("TTS: Output alloc failed");
        return false;
    }

    // Create SAM instance with our I2S output
    _sam = new SAM(_output);
    if (!_sam) {
        Serial.println("TTS: SAM alloc failed");
        delete _output;
        _output = nullptr;
        return false;
    }

    // Set default ultra-robotic voice
    setVoice(ROBOT);

    _ready = true;
    Serial.println("TTS: Ready (SAM with I2S output)");
    return true;
}

void RobotTTS::end() {
    if (_sam) {
        _sam->end();
        delete _sam;
        _sam = nullptr;
    }
    if (_output) {
        _output->close();
        delete _output;
        _output = nullptr;
    }
    _ready = false;
}

bool RobotTTS::speak(const char* text) {
    if (!_ready || !text || strlen(text) == 0) return false;

    Serial.printf("TTS: Speaking \"%s\"\n", text);
    bool result = _sam->say(text);

    if (!result) {
        Serial.println("TTS: Speech failed");
    }
    return result;
}

bool RobotTTS::speakPhonemes(const char* phonemes) {
    if (!_ready || !phonemes || strlen(phonemes) == 0) return false;

    Serial.printf("TTS: Phonemes \"%s\"\n", phonemes);
    return _sam->sayPhone(phonemes);
}

void RobotTTS::setVoice(Voice voice) {
    if (!_sam) return;

    switch (voice) {
        case ROBOT:
            // Ultra-robotic: slow, low pitch, harsh throat/mouth
            _sam->setSpeed(100);    // Slower for dramatic effect
            _sam->setPitch(50);     // Lower pitch
            _sam->setThroat(200);   // Harsh throat
            _sam->setMouth(200);    // Wide mouth resonance
            break;
        case LITTLE_ROBOT:
            _sam->setVoice(SAM::LittleRobot);
            break;
        case ALIEN:
            _sam->setVoice(SAM::ExtraTerrestrial);
            break;
        case ELF:
            _sam->setVoice(SAM::Elf);
            break;
        case SAM_DEFAULT:
        default:
            _sam->setVoice(SAM::Sam);
            break;
    }
}

void RobotTTS::setSpeed(uint8_t speed) {
    if (_sam) _sam->setSpeed(speed);
}

void RobotTTS::setPitch(uint8_t pitch) {
    if (_sam) _sam->setPitch(pitch);
}

void RobotTTS::setMouth(uint8_t mouth) {
    if (_sam) _sam->setMouth(mouth);
}

void RobotTTS::setThroat(uint8_t throat) {
    if (_sam) _sam->setThroat(throat);
}

void RobotTTS::setVolume(uint8_t percent) {
    if (_sam) _sam->setVolume(percent);
}

void RobotTTS::setSingMode(bool enable) {
    if (_sam) _sam->setSingMode(enable);
}
