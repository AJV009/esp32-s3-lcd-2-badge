/*******************************************************************************
 * OAISYS Badge - Embedding Similarity Search Implementation
 *
 * Optimized with ESP-DSP SIMD for fast cosine similarity search.
 ******************************************************************************/

#include "embed_search.h"
#include "audio_embed.h"  // For AUDIO_EMBEDDING_DIM
#include "../../config.h"

#include <SD.h>
#include <math.h>
#include <esp_heap_caps.h>

// ESP-DSP for SIMD dot product (same as used in llm_core.cpp)
#include <dsps_dotprod.h>

EmbeddingSearch::EmbeddingSearch()
    : _embeddings(nullptr)
    , _intentsData(nullptr)
    , _intentOffsets(nullptr)
    , _count(0)
    , _loaded(false)
{
}

EmbeddingSearch::~EmbeddingSearch() {
    end();
}

bool EmbeddingSearch::begin(const char* embeddingsPath, const char* intentsPath) {
    if (_loaded) return true;

    // Load embeddings first
    if (!_loadEmbeddings(embeddingsPath)) {
        Serial.println("EmbedSearch: Embeddings load failed");
        return false;
    }

    // Load intents
    if (!_loadIntents(intentsPath)) {
        Serial.println("EmbedSearch: Intents load failed");
        end();
        return false;
    }

    _loaded = true;
    Serial.printf("EmbedSearch: Loaded %d embeddings (%.1fKB)\n",
                  _count, (_count * AUDIO_EMBEDDING_DIM * sizeof(float)) / 1024.0f);
    return true;
}

void EmbeddingSearch::end() {
    if (_embeddings) {
        free(_embeddings);
        _embeddings = nullptr;
    }
    if (_intentsData) {
        free(_intentsData);
        _intentsData = nullptr;
    }
    if (_intentOffsets) {
        free(_intentOffsets);
        _intentOffsets = nullptr;
    }
    _count = 0;
    _loaded = false;
}

bool EmbeddingSearch::_loadEmbeddings(const char* path) {
    File f = SD.open(path);
    if (!f) {
        Serial.printf("EmbedSearch: Cannot open %s\n", path);
        return false;
    }

    // Calculate number of embeddings
    size_t fileSize = f.size();
    _count = fileSize / (AUDIO_EMBEDDING_DIM * sizeof(float));

    if (_count == 0) {
        Serial.println("EmbedSearch: Empty embeddings file");
        f.close();
        return false;
    }

    Serial.printf("EmbedSearch: File size %d bytes, %d embeddings\n", fileSize, _count);

    // Allocate in PSRAM with 16-byte alignment for ESP-DSP SIMD
    size_t embedSize = _count * AUDIO_EMBEDDING_DIM * sizeof(float);
    _embeddings = (float*)heap_caps_aligned_alloc(16, embedSize, MALLOC_CAP_SPIRAM);
    if (!_embeddings) {
        Serial.println("EmbedSearch: Embeddings alloc failed");
        f.close();
        return false;
    }

    // Read all embeddings
    size_t bytesRead = f.read((uint8_t*)_embeddings, fileSize);
    f.close();

    if (bytesRead != fileSize) {
        Serial.println("EmbedSearch: Embeddings read incomplete");
        free(_embeddings);
        _embeddings = nullptr;
        return false;
    }

    return true;
}

bool EmbeddingSearch::_loadIntents(const char* path) {
    File f = SD.open(path);
    if (!f) {
        Serial.printf("EmbedSearch: Cannot open %s\n", path);
        return false;
    }

    // Read entire file into memory
    size_t fileSize = f.size();
    _intentsData = (char*)ps_malloc(fileSize + 1);
    if (!_intentsData) {
        Serial.println("EmbedSearch: Intents alloc failed");
        f.close();
        return false;
    }

    f.read((uint8_t*)_intentsData, fileSize);
    _intentsData[fileSize] = '\0';
    f.close();

    // Count lines and build offset table
    int lineCount = 0;
    for (size_t i = 0; i < fileSize; i++) {
        if (_intentsData[i] == '\n') lineCount++;
    }
    // Add 1 if file doesn't end with newline
    if (fileSize > 0 && _intentsData[fileSize - 1] != '\n') lineCount++;

    // Verify line count matches embedding count
    if (lineCount < _count) {
        Serial.printf("EmbedSearch: Intent count mismatch: %d lines vs %d embeddings\n",
                      lineCount, _count);
        // Use minimum
        _count = lineCount;
    }

    // Build offset table
    _intentOffsets = (int*)ps_malloc(_count * sizeof(int));
    if (!_intentOffsets) {
        Serial.println("EmbedSearch: Offsets alloc failed");
        free(_intentsData);
        _intentsData = nullptr;
        return false;
    }

    int line = 0;
    _intentOffsets[0] = 0;

    for (size_t i = 0; i < fileSize && line < _count; i++) {
        if (_intentsData[i] == '\n') {
            _intentsData[i] = '\0';  // Replace newline with null terminator
            line++;
            if (line < _count) {
                _intentOffsets[line] = i + 1;
            }
        }
    }

    Serial.printf("EmbedSearch: Loaded %d intents\n", _count);
    return true;
}

void EmbeddingSearch::_getIntent(int idx, char* buffer, size_t bufSize) {
    if (idx < 0 || idx >= _count || !_intentsData || !_intentOffsets) {
        buffer[0] = '\0';
        return;
    }

    const char* intentStr = _intentsData + _intentOffsets[idx];
    strncpy(buffer, intentStr, bufSize - 1);
    buffer[bufSize - 1] = '\0';
}

float EmbeddingSearch::_cosineSimilarity(const float* a, const float* b) {
    float dot = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;

    // ESP-DSP SIMD dot products - 5-10x faster than manual loops
    // dsps_dotprod_f32_aes3 uses PIE SIMD instructions on ESP32-S3
    dsps_dotprod_f32_aes3(a, b, &dot, AUDIO_EMBEDDING_DIM);
    dsps_dotprod_f32_aes3(a, a, &normA, AUDIO_EMBEDDING_DIM);
    dsps_dotprod_f32_aes3(b, b, &normB, AUDIO_EMBEDDING_DIM);

    float denom = sqrtf(normA) * sqrtf(normB);
    return (denom > 1e-8f) ? dot / denom : 0.0f;
}

SearchResult EmbeddingSearch::search(const float* embedding) {
    SearchResult result;
    result.intentIdx = -1;
    result.score = -1.0f;
    result.intent[0] = '\0';

    if (!_loaded || !embedding) {
        Serial.println("EmbedSearch: Not loaded or null embedding!");
        return result;
    }

    unsigned long startTime = millis();

    // Track top 5 for debugging
    const int TOP_K = 5;
    float topScores[TOP_K] = {-1, -1, -1, -1, -1};
    int topIndices[TOP_K] = {-1, -1, -1, -1, -1};

    // Linear search through all embeddings
    for (int i = 0; i < _count; i++) {
        float score = _cosineSimilarity(embedding, &_embeddings[i * AUDIO_EMBEDDING_DIM]);

        // Insert into top-K if better than worst
        if (score > topScores[TOP_K - 1]) {
            // Find insertion point
            int insertAt = TOP_K - 1;
            while (insertAt > 0 && score > topScores[insertAt - 1]) {
                topScores[insertAt] = topScores[insertAt - 1];
                topIndices[insertAt] = topIndices[insertAt - 1];
                insertAt--;
            }
            topScores[insertAt] = score;
            topIndices[insertAt] = i;
        }
    }

    unsigned long searchTime = millis() - startTime;
    Serial.printf("EmbedSearch: Searched %d embeddings in %lu ms\n", _count, searchTime);

    // Debug: Print top 5 matches
    Serial.println("=== TOP 5 MATCHES ===");
    char intentBuf[128];
    for (int i = 0; i < TOP_K && topIndices[i] >= 0; i++) {
        _getIntent(topIndices[i], intentBuf, sizeof(intentBuf));
        // Truncate long intents for display
        if (strlen(intentBuf) > 50) {
            intentBuf[47] = '.';
            intentBuf[48] = '.';
            intentBuf[49] = '.';
            intentBuf[50] = '\0';
        }
        Serial.printf("  #%d: %.4f - \"%s\"\n", i + 1, topScores[i], intentBuf);
    }
    Serial.println("=====================");

    if (topIndices[0] >= 0) {
        result.intentIdx = topIndices[0];
        result.score = topScores[0];
        _getIntent(topIndices[0], result.intent, sizeof(result.intent));
    }

    return result;
}
