// embedding_writer.h - JSON serialization for embeddings

#ifndef EMBEDDING_WRITER_H
#define EMBEDDING_WRITER_H

#include <Arduino.h>
#include <SD.h>

class EmbeddingWriter {
public:
    EmbeddingWriter();
    ~EmbeddingWriter();

    // Write embeddings to SD card as JSON
    // Format: {"embeddings": [val1, val2, ..., val1024], "dimension": 1024}
    bool writeJSON(const char* filepath, float* embeddings, int dimension);

private:
    // Helper to write float with precision
    void writeFloat(File& file, float value, int precision = 6);
};

#endif // EMBEDDING_WRITER_H
