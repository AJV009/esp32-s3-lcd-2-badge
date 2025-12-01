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

    if (!t->vocab || !t->vocab_scores) {
        Serial.println("Tokenizer allocation failed!");
        return false;
    }

    for (int i = 0; i < 256; i++) {
        t->byte_pieces[i * 2] = (unsigned char)i;
        t->byte_pieces[i * 2 + 1] = '\0';
    }

    File file = SD.open(tokenizer_path, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open tokenizer: %s\n", tokenizer_path);
        return false;
    }

    if (file.read((uint8_t*)&t->max_token_length, sizeof(int)) != sizeof(int)) {
        file.close();
        return false;
    }

    if (t->max_token_length <= 0 || t->max_token_length > 1000) {
        file.close();
        return false;
    }

    int len;
    for (int i = 0; i < vocab_size; i++) {
        if (file.read((uint8_t*)(t->vocab_scores + i), sizeof(v4sf)) != sizeof(v4sf)) {
            file.close();
            return false;
        }
        if (file.read((uint8_t*)&len, sizeof(int)) != sizeof(int)) {
            file.close();
            return false;
        }

        if (len < 0 || len > 1000) {
            file.close();
            return false;
        }

        t->vocab[i] = (char*)malloc(len + 1);
        if (!t->vocab[i]) {
            file.close();
            return false;
        }

        if (file.read((uint8_t*)t->vocab[i], len) != len) {
            file.close();
            return false;
        }
        t->vocab[i][len] = '\0';
    }

    file.close();
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
    if (text == NULL) return;

    // Build sorted_vocab on first call
    if (t->sorted_vocab == NULL) {
        t->sorted_vocab = (TokenIndex*)malloc(t->vocab_size * sizeof(TokenIndex));
        if (!t->sorted_vocab) return;

        for (int i = 0; i < t->vocab_size; i++) {
            t->sorted_vocab[i].str = t->vocab[i];
            t->sorted_vocab[i].id = i;
        }
        qsort(t->sorted_vocab, t->vocab_size, sizeof(TokenIndex), compare_tokens);
    }

    // Allocate working buffer
    int buf_size = t->max_token_length * 2 + 3;
    char* str_buffer = (char*)malloc(buf_size);
    if (!str_buffer) return;

    size_t str_len = 0;
    *n_tokens = 0;

    // Add BOS if requested
    if (bos) {
        tokens[(*n_tokens)++] = 1;
    }

    // Add space prefix if text not empty
    // SentencePiece uses "▁" (U+2581) for space prefix, not ASCII " "
    if (text[0] != '\0') {
        int dummy_prefix = str_lookup((char*)"\xE2\x96\x81", t->sorted_vocab, t->vocab_size);
        if (dummy_prefix < 0) {
            dummy_prefix = str_lookup((char*)" ", t->sorted_vocab, t->vocab_size);
        }
        if (dummy_prefix >= 0 && dummy_prefix < t->vocab_size) {
            tokens[(*n_tokens)++] = dummy_prefix;
        }
    }

    // Special tokens to detect (must match training)
    const char* special_tokens[] = {"<|user|>", "<|assistant|>", "<|end|>", "<pad>", "<unk>", "<s>", "</s>"};
    const int special_ids[] = {4, 5, 6, 0, 1, 2, 3};
    const int num_special = 7;

    // Tokenize with special token detection
    char* c = text;
    while (*c != '\0') {
        // Check for special tokens at current position
        bool found_special = false;
        for (int s = 0; s < num_special; s++) {
            int slen = strlen(special_tokens[s]);
            if (strncmp(c, special_tokens[s], slen) == 0) {
                tokens[(*n_tokens)++] = special_ids[s];
                c += slen;
                found_special = true;
                break;
            }
        }
        if (found_special) continue;

        // Regular character processing
        if ((*c & 0xC0) != 0x80) {
            str_len = 0;
        }
        str_buffer[str_len++] = *c;
        str_buffer[str_len] = '\0';
        c++;

        // Continue for UTF-8 multi-byte
        if ((*c & 0xC0) == 0x80 && str_len < 4) {
            continue;
        }

        int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
        if (id != -1) {
            tokens[(*n_tokens)++] = id;
        } else {
            // Fallback: try SentencePiece byte tokens like "<0xAB>" or use UNK
            for (int i = 0; i < str_len; i++) {
                char byte_token[8];
                snprintf(byte_token, sizeof(byte_token), "<0x%02X>", (unsigned char)str_buffer[i]);
                int byte_id = str_lookup(byte_token, t->sorted_vocab, t->vocab_size);
                if (byte_id != -1) {
                    tokens[(*n_tokens)++] = byte_id;
                } else {
                    tokens[(*n_tokens)++] = 1;  // UNK token
                }
            }
        }
        str_len = 0;
    }

    // BPE merge loop
    while (1) {
        v4sf best_score = -1e10;
        int best_id = -1;
        int best_idx = -1;

        for (int i = 0; i < (*n_tokens - 1); i++) {
            int t1 = tokens[i];
            int t2 = tokens[i + 1];

            if (t1 < 0 || t1 >= t->vocab_size || t2 < 0 || t2 >= t->vocab_size) {
                continue;
            }
            if (!t->vocab[t1] || !t->vocab[t2]) {
                continue;
            }

            sprintf(str_buffer, "%s%s", t->vocab[t1], t->vocab[t2]);
            int id = str_lookup(str_buffer, t->sorted_vocab, t->vocab_size);
            if (id != -1 && t->vocab_scores[id] > best_score) {
                best_score = t->vocab_scores[id];
                best_id = id;
                best_idx = i;
            }
        }

        if (best_idx == -1) break;

        // Apply merge
        tokens[best_idx] = best_id;
        for (int i = best_idx + 1; i < (*n_tokens - 1); i++) {
            tokens[i] = tokens[i + 1];
        }
        (*n_tokens)--;
    }

    // Add EOS if requested
    if (eos) {
        tokens[(*n_tokens)++] = 2;
    }

    free(str_buffer);
}
