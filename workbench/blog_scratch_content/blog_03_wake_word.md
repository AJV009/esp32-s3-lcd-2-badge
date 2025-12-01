---
title: "Wake Word Detection - Training 'Hey Daisy' from Scratch"
meta_title: "Custom Wake Word Detection on ESP32-S3 with MicroWakeWord"
description: "How we trained a custom 'Hey Daisy' wake word detector using confusable negatives, synthetic voice generation, and deployed it on ESP32-S3 with EdgeNeuron TFLite."
date: 2025-12-02T00:00:00Z
image: "assets/cover.jpg"
categories: ["Hardware", "Embedded Systems", "AI", "GenAI"]
author: "Alphons Jaimon"
ai_assistance: true
tags: ["ESP32", "ESP32-S3", "Wake Word", "TFLite", "EdgeNeuron", "MicroWakeWord", "Speech Recognition", "TinyML"]
series_id: "esp32-oaisys25-badge"
series_name: "Project 'Tiny Haze' - An ESP32 powered Digital Badge"
series_order: 3
draft: false
---

In the [first blog of this series](/blog/esp32-video-badge-gyro-rotation), we got MJPEG video playing on our ESP32-S3 badge with gyroscope-based rotation. Then in [blog two](/blog/deconstructing-llama2-c-esp32-llm), we dove deep into running a tiny LLM on the same chip. But here's the thing - nobody wants to press a button every time they want to talk to their badge. We needed a way to make it *listen*, constantly, waiting for a magic phrase to wake it up.

Welcome to the world of wake word detection. And yes, I had Claude Code helping me through most of this. Full disclosure: a lot of the training pipeline code, the debugging sessions, and figuring out why my model kept triggering on "hey lazy" - all of that involved back-and-forth with Claude. This blog is my attempt to document what we learned together.

{{< sub-section title="Why Wake Words?" icon="fa-microphone" >}}

If you've ever said "Hey Siri" or "OK Google," you've used wake word detection. The idea is beautifully simple: instead of requiring a button press, the device listens constantly for a specific trigger phrase. Everything else gets ignored.

For our OAISYS25 badge, I wanted "Hey Daisy" as the wake phrase. Why Daisy? Honestly, it just sounded nice, and it's phonetically distinct enough that random conversation shouldn't trigger it. At least, that was the theory. Reality, as always, had other plans.

The alternative was push-to-talk, which works fine but feels clunky for a conference badge. Imagine walking up to someone, reaching for a button, pressing it, then speaking. The whole interaction becomes awkward. With wake word detection, you just say "Hey Daisy, tell me about yourself" and the badge responds. Much more natural.

But here's the catch: wake word detection has to run *constantly*. Unlike the LLM that only fires when needed, the wake word model runs on every audio frame. That means it needs to be tiny, fast, and accurate. We're talking about a model that can process audio in real-time while leaving enough headroom for everything else.

{{< /sub-section >}}

{{< sub-section title="The MicroWakeWord Framework" icon="fa-brain" >}}

After researching options, I settled on [microWakeWord](https://github.com/kahrendt/microWakeWord) by Kevin Ahrendt. It's specifically designed for microcontrollers, with training pipelines that produce TFLite models optimized for streaming inference.

The architecture is based on MixNet, which is essentially a MobileNet variant with mixed depthwise convolutions. What makes it special for wake word detection is the *streaming* design - the model maintains internal state between frames, processing one small chunk of audio at a time rather than requiring the entire utterance upfront.

Here's what the training config looks like for our model:

```python
# Model architecture
pointwise_filters = '192,192,192,192'
repeat_in_block = '1,1,1,1'
mixconv_kernel_sizes = '[5], [7,11], [9,15], [23]'
stride = 3
first_conv_filters = 96
first_conv_kernel_size = 5
```

The resulting model is around 1-2MB, which might sound big for a wake word detector, but it includes all the state needed for streaming inference. The model takes in 40-bin log-mel spectrogram features (3 frames at a time) and outputs a single probability: how likely is it that the wake word was just spoken?

I'll be honest - I don't fully understand all the MixNet architectural choices. Claude helped me understand that the mixed kernel sizes (5, 7, 11, 9, 15, 23) allow the model to capture both fine-grained phonetic details and longer temporal patterns. But the math behind why these specific numbers work? Still a bit fuzzy to me. What I do know is that it works, and the model size fits our constraints.

{{< /sub-section >}}

{{< sub-section title="Generating Training Data" icon="fa-database" >}}

This is where things got interesting. Training a wake word detector requires thousands of positive samples (people saying "Hey Daisy") and even more negative samples (everything else). Recording real people saying the phrase thousands of times wasn't practical, so we went synthetic.

**Phonetic Variations**

First, I created 20 different phonetic variations of "Hey Daisy":

```python
PHONETIC_VARIATIONS = [
    "Hey Daisy ", "hey Daisy ", "hey daisy ",
    "Hey Daisy. ", "Hey Daisy! ",
    "hey day zee ", "hey day-zee ", "hey daisee ",
    "hey daysy ", "hey daezy ",
    "hay daisy ", "hay Daisy ",
    "HEY Daisy ", "hey DAISY ", "HEY DAISY ",
    "hey daisy? ", "heyyy daisy ",
    "hey daizy ", "hey dayzie ", "hey dazey ",
]
```

Why so many variations? Text-to-speech models interpret text differently based on capitalization, punctuation, and spelling. "HEY DAISY" sounds more emphatic than "hey daisy". The trailing space matters too - without it, some TTS engines produce clipped audio.

**Piper TTS for Volume**

For the bulk of our samples, we used [Piper TTS](https://github.com/rhasspy/piper) with the LibriTTS medium model. It's fast and can generate diverse voices:

```bash
python3 generate_samples.py "Hey Daisy " \
    --max-samples 100 \
    --model en_US-libritts_r-medium.pt
```

100 samples per variation = 2,000 Piper samples total.

**XTTS-v2 for Realism**

Piper is great for volume, but the voices can sound a bit synthetic. For more realistic samples, we used XTTS-v2 with voice cloning from TED speaker recordings:

```python
TED_SPEAKERS = ['BillGates', 'DaphneKoller', 'FeiFeiLi', 'GeorgeTakei',
                'JaneGoodall', 'SalmanKhan', 'StephenHawking', 'StephenWolfram']
```

8 speakers x 10 voice samples each x 20 variations = 1,600 XTTS samples.

Combined total: **3,600 positive samples** of "Hey Daisy" in various voices, accents, and intonations.

The full training notebook is at `workbench/tests/wakeword_training_ipynb_poc/wakeword_training.ipynb` if you want to see the gory details.

{{< /sub-section >}}

{{< sub-section title="The Confusable Negatives Innovation" icon="fa-exclamation-triangle" >}}

Here's where I learned something the hard way. My first trained model had a problem: it kept triggering on phrases that *weren't* "Hey Daisy."

"Hey crazy!" - TRIGGERED.
"Hey lazy!" - TRIGGERED.
"Hey baby!" - TRIGGERED.
"Daisy" alone - TRIGGERED.
Just "Hey" - TRIGGERED.

The model was picking up on phonetic similarity without learning to *reject* near-misses. The generic "speech" and "no speech" negative samples from microWakeWord's default dataset weren't enough. They taught the model to distinguish wake word from silence or random chatter, but not from *similar* phrases.

The solution? **Confusable negatives** - explicit samples of what the model should NOT trigger on, weighted heavily during training.

```python
CONFUSABLE_NEGATIVES = [
    # Partials (most important!)
    "Hey ", "hey ", "HEY ", "hay ",
    "Daisy ", "daisy ", "DAISY ", "daisee ", "daysy ", "day zee ",

    # Similar to "Daisy"
    "crazy ", "lazy ", "hazy ", "mazy ",
    "Tracy ", "Stacy ", "Gracie ", "Lacey ", "Macy ", "Casey ",
    "racy ", "spacey ",

    # Similar phrases
    "hey crazy ", "hey lazy ", "hey Tracy ", "hey Stacy ",
    "hey baby ", "hey lady ", "hey maybe ", "hey safety ",
    "say daisy ", "pay daisy ", "play daisy ", "stay daisy ",

    # Rhyming words
    "haze ", "days ", "daze ", "phase ", "craze ",
    "maze ", "blaze ", "gaze ", "raise ", "praise ",

    # Common "hey" phrases
    "hey there ", "hey you ", "hey what ", "hey how ",
    "hey wait ", "hey look ", "hey come ", "hey stop ",

    # Other flower names
    "hey Rose ", "hey Lily ", "hey Violet ", "hey Iris ", "hey Poppy ",
]
```

That's 63 confusable phrases. At 50 Piper samples per phrase, we generated **3,150 confusable negative samples**.

The key is the sampling weight during training:

```python
{
    'type': 'mmap',
    'features_dir': str(CONFUSABLE_FEATURES_DIR),
    'truth': False,  # These are NEGATIVES
    'sampling_weight': 15.0,  # HIGH weight - prioritize learning these!
    'penalty_weight': 2.0,    # Extra penalty for false positives
    'truncation_strategy': 'truncate_start',
}
```

A sampling weight of 15.0 means confusable negatives appear 15x more often during training than their natural frequency would suggest. The model sees "hey lazy" over and over, each time learning "this is NOT the wake word."

After retraining with confusable negatives, the false positive rate dropped dramatically. "Hey crazy" no longer triggers. "Hey Daisy" still does. Success.

{{< /sub-section >}}

{{< sub-section title="Audio Augmentation Pipeline" icon="fa-wave-square" >}}

Real-world audio is messy. Conference halls have background chatter. Someone might mumble. There might be music playing. To make our model robust, we augmented the training data heavily.

```python
augmentation = Augmentation(
    augmentation_probabilities={
        "SevenBandParametricEQ": 0.1,
        "TanhDistortion": 0.1,
        "PitchShift": 0.1,
        "BandStopFilter": 0.1,
        "AddColorNoise": 0.1,
        "AddBackgroundNoise": 0.75,  # 75% of samples get background noise
        "Gain": 1.0,                  # Always vary volume
        "RIR": 0.5,                   # 50% get room impulse response
    },
    impulse_paths=['/path/to/mit_ir/Audio'],  # MIT impulse database
    background_paths=['/path/to/audioset'],   # AudioSet background noises
)
```

**Background Noise Injection (75% probability)**

We downloaded 500 samples from AudioSet - everything from crowd noise to machinery to music. Adding these as background teaches the model to pick out "Hey Daisy" from a noisy environment.

**Room Impulse Response Convolution (50%)**

MIT provides a database of impulse responses recorded in various rooms. Convolving our dry TTS audio with these makes it sound like it was recorded in an actual room with reflections and reverb. This is crucial because the badge will be used in conference halls, not anechoic chambers.

**SpecAugment**

SpecAugment is a technique from Google that masks random time and frequency regions of the spectrogram:

```python
'time_mask_max_size': [10],   # Up to 10 frames
'time_mask_count': [2],        # Two time masks
'freq_mask_max_size': [3],     # Up to 3 frequency bins
'freq_mask_count': [2],        # Two frequency masks
```

This prevents the model from over-relying on any specific time or frequency pattern.

With augmentation, our 3,600 positive samples become 288,000 training spectrograms (10x repetition with different augmentations). The confusable negatives become 126,000 spectrograms (5x repetition).

{{< /sub-section >}}

{{< sub-section title="Feature Extraction" icon="fa-chart-bar" >}}

Before audio hits the model, it needs to be converted to spectrograms. We use the TensorFlow Lite Microfrontend library, which is designed specifically for embedded devices.

The feature extraction config, defined in `local_llm_badge/src/audio/mic_stream.h`:

```cpp
#define FEATURE_WINDOW_MS   30   // 30ms analysis window
#define FEATURE_STRIDE_MS   10   // 10ms between frames (67% overlap)
#define FEATURE_BINS        40   // 40 mel-frequency bins
#define FEATURE_FRAMES      3    // Model input: 3 frames at a time
```

The frontend does several things:

1. **Mel filterbank**: Converts the raw FFT output to mel-frequency bins (perceptually-weighted)
2. **PCAN gain control**: Adaptive gain that normalizes volume, making the model robust to quiet or loud speech
3. **Noise reduction**: Smooths and suppresses background noise

From `local_llm_badge/src/audio/mic_stream.cpp`:

```cpp
bool MicStream::_initFrontend() {
    FrontendConfig frontend_config;
    frontend_config.window.size_ms = FEATURE_WINDOW_MS;
    frontend_config.window.step_size_ms = FEATURE_STRIDE_MS;
    frontend_config.filterbank.num_channels = FEATURE_BINS;
    frontend_config.filterbank.lower_band_limit = 125.0f;
    frontend_config.filterbank.upper_band_limit = 7500.0f;
    frontend_config.noise_reduction.smoothing_bits = 10;
    frontend_config.noise_reduction.even_smoothing = 0.025f;
    frontend_config.noise_reduction.odd_smoothing = 0.06f;
    frontend_config.noise_reduction.min_signal_remaining = 0.05f;
    frontend_config.pcan_gain_control.enable_pcan = 1;
    frontend_config.pcan_gain_control.strength = 0.95f;
    frontend_config.pcan_gain_control.offset = 80.0f;
    frontend_config.pcan_gain_control.gain_bits = 21;
    frontend_config.log_scale.enable_log = 1;
    frontend_config.log_scale.scale_shift = 6;

    return FrontendPopulateState(&frontend_config, _frontendState, SAMPLE_RATE);
}
```

The PCAN gain control is particularly important. Without it, someone whispering "Hey Daisy" from far away would produce much smaller values than someone shouting it nearby. PCAN normalizes both to similar magnitudes, improving detection at various volumes.

{{< /sub-section >}}

{{< sub-section title="On-Device Inference with EdgeNeuron" icon="fa-microchip" >}}

Now for the fun part - running the trained model on the ESP32-S3. We use the EdgeNeuron library, which bundles TensorFlow Lite Micro with the microfrontend.

**The Dual-Arena Setup**

Streaming models need two memory regions:
- **Tensor arena** (502KB): For input/output tensors and intermediate activations
- **Variable arena** (52KB): For persistent state between frames

From `local_llm_badge/src/ml/wake_word.h`:

```cpp
static constexpr size_t TENSOR_ARENA_SIZE = 502000;
static constexpr size_t VAR_ARENA_SIZE = 52000;
static constexpr int MAX_RESOURCE_VARS = 100;
```

**Placement New for Interpreter Reset**

Here's a pattern I learned during debugging. When you need to reload a model (say, after switching between wake word and audio embedding models), you can't just create a new interpreter - the memory layout has to be exactly right. The solution is C++ placement new:

```cpp
// Placement new resets interpreter state without reallocation
static uint8_t interpreterBuffer[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));
_interpreter = new (interpreterBuffer) tflite::MicroInterpreter(
    model, resolver, _tensorArena, TENSOR_ARENA_SIZE, _resourceVars);
```

This constructs a new interpreter object in a pre-allocated buffer, ensuring the destructor of the old interpreter runs first. It's critical for avoiding memory fragmentation when loading/unloading models.

**Quantization-Aware Feature Conversion**

The model expects int8 input, but our microfrontend produces uint16 values. The conversion needs to match what was done during training:

```cpp
if (_inputTensor->type == kTfLiteInt8) {
    int8_t* input_data = _inputTensor->data.int8;
    for (int i = 0; i < total_features; i++) {
        // Convert uint16 features to int8 range [-128, 127]
        int32_t scaled = ((int32_t)features[i] * 256 + 333) / 666 - 128;
        input_data[i] = constrain(scaled, -128, 127);
    }
}
```

Getting this scaling wrong was a source of many hours of debugging. The model would produce garbage outputs until we matched the exact quantization math from training. Claude helped me trace through the microWakeWord Python code to find the right formula.

{{< /sub-section >}}

{{< sub-section title="Sliding Window Detection" icon="fa-window-maximize" >}}

A single inference gives us a probability. But one high probability could be noise. We need temporal consistency - the probability should stay high across multiple frames when the wake word is actually spoken.

The detection logic from `local_llm_badge/src/ml/wake_word.cpp`:

```cpp
bool WakeWordDetector::detect(const uint16_t* features) {
    if (!_loaded) return false;

    // Run inference
    float prob = _runInference(features);
    _lastProbability = prob;

    // Update sliding window
    _probabilityWindow[_windowPos] = prob;
    _windowPos = (_windowPos + 1) % _config.sliding_window_size;

    if (_windowPos == 0) {
        _windowFilled = true;
    }

    // Need full window before detection
    if (!_windowFilled) {
        return false;
    }

    // Check cooldown
    if (millis() - _lastDetectionMs < (unsigned long)_config.cooldown_ms) {
        return false;
    }

    // Calculate average and count high frames
    float avg = 0.0f;
    int high_frames = 0;
    for (int i = 0; i < _config.sliding_window_size; i++) {
        avg += _probabilityWindow[i];
        if (_probabilityWindow[i] >= _config.min_frame_prob) {
            high_frames++;
        }
    }
    avg /= _config.sliding_window_size;

    // Detection criteria: avg >= 0.5 AND >= 6 frames > 0.5
    if (avg >= _config.probability_cutoff && high_frames >= _config.min_high_frames) {
        _lastDetectionMs = millis();
        _resetWindow();
        return true;
    }

    return false;
}
```

The detection requires BOTH conditions:
1. Average probability across 10 frames >= 0.5
2. At least 6 of those 10 frames have probability > 0.5

This prevents false triggers from a single spike while still allowing quick detection when the wake word is genuinely spoken.

**Cooldown Mechanism**

After a detection, we need a cooldown period to prevent re-triggering:

```cpp
if (millis() - _lastDetectionMs < (unsigned long)_config.cooldown_ms) {
    return false;  // Still in cooldown
}
```

The default is 1500ms (1.5 seconds). This gives time for the user to start speaking their actual question before the badge could potentially re-trigger on echo or feedback.

{{< /sub-section >}}

{{< sub-section title="Runtime Configuration" icon="fa-cog" >}}

All the detection parameters are configurable via JSON on the SD card, meaning you can tune the sensitivity without recompiling:

```json
{
  "type": "micro",
  "wake_word": "Hey Daisy",
  "model": "hey_daisy.tflite",
  "version": 1,
  "micro": {
    "probability_cutoff": 0.5,
    "sliding_window_average_size": 10,
    "min_high_frames": 6,
    "min_frame_prob": 0.5,
    "cooldown_ms": 1500
  }
}
```

This lives at `/models/hey_daisy.json` on the SD card. If you're getting too many false positives at a conference, bump `probability_cutoff` to 0.6 or 0.7. Too many missed detections? Lower `min_high_frames` to 4 or 5.

The config loading code from `local_llm_badge/src/ml/wake_word.cpp`:

```cpp
bool WakeWordDetector::_loadConfig(const char* path) {
    File f = SD.open(path);
    if (!f) {
        return false;  // Will use defaults
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error) {
        return false;
    }

    const char* wake_word = doc["wake_word"] | "Hey Daisy";
    strncpy(_config.wake_word, wake_word, sizeof(_config.wake_word) - 1);

    JsonObject micro = doc["micro"];
    if (micro) {
        _config.probability_cutoff = micro["probability_cutoff"] | 0.5f;
        _config.sliding_window_size = micro["sliding_window_average_size"] | 10;
        _config.min_high_frames = micro["min_high_frames"] | 6;
        _config.min_frame_prob = micro["min_frame_prob"] | 0.5f;
        _config.cooldown_ms = micro["cooldown_ms"] | 1500;
    }

    return true;
}
```

{{< /sub-section >}}

{{< sub-section title="Results and Lessons Learned" icon="fa-graduation-cap" >}}

After all that work, here's where we ended up:

**Performance Numbers**
- Model size: ~1.7MB TFLite (quantized int8)
- Inference time: ~50ms per frame
- Memory: 554KB total (502KB tensor arena + 52KB variable arena)
- False positive rate: Very low after confusable negatives training
- True positive rate: >95% in quiet environments, ~85% with moderate background noise

**What Worked**

1. **Confusable negatives** were the biggest win. Without them, the model was unusable. With them, it reliably distinguishes "Hey Daisy" from "Hey crazy," "Hey lazy," and partial phrases.

2. **Heavy augmentation** made the model robust to real-world conditions. Background noise, reverb, volume variations - it handles them all reasonably well.

3. **JSON configuration** saved countless recompile cycles during tuning. Being able to adjust thresholds on the fly was essential.

4. **Piper + XTTS combination** gave us both volume (2,000 Piper samples) and realistic diversity (1,600 XTTS samples). Neither alone would have been sufficient.

**What I Learned**

1. **Wake word detection is harder than it looks.** It's not just "detect a phrase" - it's "detect THIS phrase and ONLY this phrase, even when something very similar is said."

2. **Synthetic data works surprisingly well.** I was skeptical about training entirely on TTS output, but with enough voice diversity and augmentation, it generalizes to real human speech.

3. **Streaming models are tricky.** The state management, arena sizing, and proper interpreter reinitialization all have gotchas. The placement new pattern for interpreter reset was not obvious.

4. **Always save your training artifacts.** I lost my first trained model when I accidentally overwrote the output directory. Now everything gets versioned.

**What's Next**

The wake word detector triggers. Great. But then what? The badge needs to understand what you said after the trigger phrase. That's where things get really interesting - and where we hit the limits of what fits in 8MB of PSRAM. Spoiler: we couldn't fit any speech-to-text model, so we invented a workaround using audio embeddings and similarity search.

But that's a story for the next blog.

{{< /sub-section >}}

---

**Key Files Referenced:**
- Wake word detection: `/home/alphons/project/OAISYS25/badge/local_llm_badge/src/ml/wake_word.cpp`
- Mic stream with microfrontend: `/home/alphons/project/OAISYS25/badge/local_llm_badge/src/audio/mic_stream.cpp`
- Original prototype: `/home/alphons/project/OAISYS25/badge/workbench/working_protos/03_custom_wakeword/03_custom_wakeword.ino`
- Training notebook: `/home/alphons/project/OAISYS25/badge/workbench/tests/wakeword_training_ipynb_poc/wakeword_training.ipynb`
- Model config: `/home/alphons/project/OAISYS25/badge/workbench/tests/wakeword_training_ipynb_poc/hey_daisy.json`

**AI Assistance Disclosure:** This blog post was written with significant help from Claude Code. The training pipeline development, debugging sessions, and much of the code analysis involved collaborative work with Claude. I've tried to be honest about what I fully understand versus what I implemented with Claude's guidance.
