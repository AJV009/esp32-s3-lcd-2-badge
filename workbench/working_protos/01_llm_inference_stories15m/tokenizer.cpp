// tokenizer.cpp - BPE Tokenizer implementation

#include "tokenizer.h"
#include <SD.h>

static int compare_tokens(const void* a, const void* b) {
    return strcmp(((TokenIndex*)a)->str, ((TokenIndex*)b)->str);
}

static int str_lookup(char* str, TokenIndex* sorted_vocab, int vocab_size) {
    TokenIndex tok = {.str = str};
    TokenIndex* res = (TokenIndex*)bsearch(&tok, sorted_vocab, vocab_size, sizeof(TokenIndex), compare_tokens);
    return res != NULL ? res->id : -1;
}

bool build_tokenizer(Tokenizer* t, const char* tokenizer_path, int vocab_size) {
    t->vocab_size = vocab_size;
    t->vocab = (char**)malloc(vocab_size * sizeof(char*));
    t->vocab_scores = (v4sf*)malloc(vocab_size * sizeof(v4sf));
    t->sorted_vocab = NULL;

    for (int i = 0; i < 256; i++) {
        t->byte_pieces[i * 2] = (unsigned char)i;
        t->byte_pieces[i * 2 + 1] = '\0';
    }

    File file = SD.open(tokenizer_path, FILE_READ);
    if (!file) {
        Serial.printf("ERROR: Cannot open file %s\n", tokenizer_path);
        return false;
    }
    Serial.printf("Opened %s (size: %zu bytes)\n", tokenizer_path, file.size());

    if (file.read((uint8_t*)&t->max_token_length, sizeof(int)) != sizeof(int)) {
        Serial.println("ERROR: Failed to read max_token_length");
        file.close();
        return false;
    }
    Serial.printf("Max token length: %d\n", t->max_token_length);

    Serial.println("Reading vocabulary...");
    int len;
    for (int i = 0; i < vocab_size; i++) {
        if (file.read((uint8_t*)(t->vocab_scores + i), sizeof(v4sf)) != sizeof(v4sf)) {
            Serial.printf("ERROR: Failed to read score for token %d\n", i);
            file.close();
            return false;
        }
        if (file.read((uint8_t*)&len, sizeof(int)) != sizeof(int)) {
            Serial.printf("ERROR: Failed to read length for token %d\n", i);
            file.close();
            return false;
        }
        t->vocab[i] = (char*)malloc(len + 1);
        if (file.read((uint8_t*)t->vocab[i], len) != len) {
            Serial.printf("ERROR: Failed to read string for token %d (expected %d bytes)\n", i, len);
            file.close();
            return false;
        }
        t->vocab[i][len] = '\0';

        if ((i + 1) % 8000 == 0) {
            Serial.printf("  Loaded %d/%d tokens...\n", i + 1, vocab_size);
        }
    }

    file.close();
    Serial.println("Tokenizer loaded");
    return true;
}

void free_tokenizer(Tokenizer* t) {
    for (int i = 0; i < t->vocab_size; i++) {
        free(t->vocab[i]);
    }
    free(t->vocab);
    free(t->vocab_scores);
    free(t->sorted_vocab);
}

char* decode(Tokenizer* t, int prev_token, int token) {
    char* piece = t->vocab[token];
    if (prev_token == 1 && piece[0] == ' ') {
        piece++;
    }
    unsigned char byte_val;
    if (sscanf(piece, "<0x%02hhX>", &byte_val) == 1) {
        piece = (char*)t->byte_pieces + byte_val * 2;
    }
    return piece;
}

void encode(Tokenizer* t, char* text, int8_t bos, int8_t eos, int* tokens, int* n_tokens) {
    if (text == NULL) {
        Serial.println("Cannot encode NULL text");
        return;
    }

    if (t->sorted_vocab == NULL) {
        t->sorted_vocab = (TokenIndex*)malloc(t->vocab_size * sizeof(TokenIndex));
        for (int i = 0; i < t->vocab_size; i++) {
            t->sorted_vocab[i].str = t->vocab[i];
            t->sorted_vocab[i].id = i;
        }
        qsort(t->sorted_vocab, t->vocab_size, sizeof(TokenIndex), compare_tokens);
    }

    char* str_buffer = (char*)malloc((t->max_token_length * 2 + 1 + 2) * sizeof(char));
    size_t str_len = 0;

    *n_tokens = 0;

    if (bos) tokens[(*n_tokens)++] = 1;

    if (text[0] != '\0') {
        int dummy_prefix = str_lookup((char*)" ", t->sorted_vocab, t->vocab_size);
        tokens[(*n_tokens)++] = dummy_prefix;
    }

    for (char* c = text; *c != '\0'; c++) {
        if ((*c & 0xC0) != 0x80) {
            str_len = 0;
        }

        str_buffer[str_len++] = *c;
        str_buffer[str_len] = '\0';

        if ((*(c + 1) & 0xC0) == 0x80 && str_len < 4) {
            continue;
        }

        int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);

        if (id != -1) {
            tokens[(*n_tokens)++] = id;
        } else {
            for (int i = 0; i < str_len; i++) {
                tokens[(*n_tokens)++] = (unsigned char)str_buffer[i] + 3;
            }
        }
        str_len = 0;
    }

    while (1) {
        v4sf best_score = -1e10;
        int best_id = -1;
        int best_idx = -1;

        for (int i = 0; i < (*n_tokens - 1); i++) {
            sprintf(str_buffer, "%s%s", t->vocab[tokens[i]], t->vocab[tokens[i + 1]]);
            int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
            if (id != -1 && t->vocab_scores[id] > best_score) {
                best_score = t->vocab_scores[id];
                best_id = id;
                best_idx = i;
            }
        }

        if (best_idx == -1) break;

        tokens[best_idx] = best_id;
        for (int i = best_idx + 1; i < (*n_tokens - 1); i++) {
            tokens[i] = tokens[i + 1];
        }
        (*n_tokens)--;
    }

    if (eos) tokens[(*n_tokens)++] = 2;

    free(str_buffer);
}
