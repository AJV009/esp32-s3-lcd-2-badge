// tokenizer.h - BPE Tokenizer for LLM

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "llm_core.h"

// Token index for sorted vocabulary
typedef struct {
    char* str;
    int id;
} TokenIndex;

// Tokenizer structure
typedef struct {
    char** vocab;
    v4sf* vocab_scores;
    TokenIndex* sorted_vocab;
    int vocab_size;
    unsigned int max_token_length;
    unsigned char byte_pieces[512];
} Tokenizer;

// Functions
bool build_tokenizer(Tokenizer* t, const char* tokenizer_path, int vocab_size);
void free_tokenizer(Tokenizer* t);
char* decode(Tokenizer* t, int prev_token, int token);
void encode(Tokenizer* t, char* text, int8_t bos, int8_t eos, int* tokens, int* n_tokens);

#endif // TOKENIZER_H
