// embedding_writer.cpp - JSON writer implementation

#include "embedding_writer.h"

EmbeddingWriter::EmbeddingWriter() {
}

EmbeddingWriter::~EmbeddingWriter() {
}

bool EmbeddingWriter::writeJSON(const char* filepath, float* embeddings, int dimension) {
    // Delete existing file
    if (SD.exists(filepath)) {
        SD.remove(filepath);
    }

    // Open file for writing
    File file = SD.open(filepath, FILE_WRITE);
    if (!file) {
        Serial.printf("ERROR: Cannot open %s for writing\n", filepath);
        return false;
    }

    // Write JSON header
    file.println("{");
    file.print("  \"dimension\": ");
    file.print(dimension);
    file.println(",");
    file.println("  \"embeddings\": [");

    // Write embeddings array
    for (int i = 0; i < dimension; i++) {
        file.print("    ");
        writeFloat(file, embeddings[i], 6);

        if (i < dimension - 1) {
            file.println(",");
        } else {
            file.println();
        }
    }

    // Write JSON footer
    file.println("  ]");
    file.println("}");

    file.close();
    return true;
}

void EmbeddingWriter::writeFloat(File& file, float value, int precision) {
    // Handle special cases
    if (isnan(value)) {
        file.print("null");
        return;
    }
    if (isinf(value)) {
        if (value > 0) {
            file.print("\"Infinity\"");
        } else {
            file.print("\"-Infinity\"");
        }
        return;
    }

    // Write float with specified precision
    file.print(value, precision);
}
