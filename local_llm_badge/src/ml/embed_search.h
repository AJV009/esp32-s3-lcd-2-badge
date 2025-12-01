/*******************************************************************************
 * OAISYS Badge - Embedding Similarity Search Module
 *
 * Performs cosine similarity search against pre-computed query embeddings
 * to find the closest matching intent for a given audio embedding.
 ******************************************************************************/

#pragma once

#include <Arduino.h>

//==============================================================================
// Search Result
//==============================================================================
struct SearchResult {
    int intentIdx;          // Index in intents file (0-based)
    float score;            // Cosine similarity score (0-1)
    char intent[256];       // Intent text string
};

//==============================================================================
// EmbeddingSearch - Cosine similarity search against pre-computed embeddings
//==============================================================================
class EmbeddingSearch {
public:
    EmbeddingSearch();
    ~EmbeddingSearch();

    // Load embeddings and intents from SD card
    // embeddingsPath: binary file with N × 256 float32 values
    // intentsPath: text file with N lines (one intent per line)
    bool begin(const char* embeddingsPath, const char* intentsPath);
    void end();

    // Search for closest embedding
    // embedding: float[256] query embedding
    // Returns SearchResult with best match
    SearchResult search(const float* embedding);

    // Get number of loaded embeddings
    int getCount() const { return _count; }

    // Check if loaded
    bool isLoaded() const { return _loaded; }

private:
    // Cosine similarity between two 256-dim vectors
    float _cosineSimilarity(const float* a, const float* b);

    // Load embeddings binary file
    bool _loadEmbeddings(const char* path);

    // Load intents text file
    bool _loadIntents(const char* path);

    // Get intent string by index
    void _getIntent(int idx, char* buffer, size_t bufSize);

    // Data
    float* _embeddings;      // N × 256 floats (in PSRAM)
    char* _intentsData;      // Raw intents text data
    int* _intentOffsets;     // Offsets into _intentsData for each intent
    int _count;              // Number of embeddings/intents

    bool _loaded;
};
