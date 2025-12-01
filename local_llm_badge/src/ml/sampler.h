// sampler.h - Token sampling (greedy, temperature, top-p)

#ifndef SAMPLER_H
#define SAMPLER_H

#include "llm_core.h"

// Probability index for top-p sampling
typedef struct {
    float prob;
    int index;
} ProbIndex;

// Sampler structure
typedef struct {
    int vocab_size;
    ProbIndex* probindex;
    float temperature;
    float topp;
    unsigned long long rng_state;
} Sampler;

// Functions
void build_sampler(Sampler* sampler, int vocab_size, float temperature, float topp, unsigned long long rng_seed);
void free_sampler(Sampler* sampler);
int sample(Sampler* sampler, v4sf* logits);

#endif // SAMPLER_H
