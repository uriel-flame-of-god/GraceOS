#ifndef GRACE_LLM_SAMPLER_H
#define GRACE_LLM_SAMPLER_H

/*
 * llama-sampler.h — Token sampling subsystem
 *
 * Implements a composable chain of samplers that transforms the raw logit
 * vector from the model into a single sampled token.
 *
 * Available samplers:
 *   greedy       — argmax
 *   temperature  — scale logits by 1/T before converting to probabilities
 *   top_k        — keep only top-K highest probability tokens
 *   top_p        — nucleus sampling (cumulative prob threshold)
 *   min_p        — minimum probability relative to top token
 *   mirostat     — target surprise-based sampling (Mirostat v2)
 *   repetition   — penalty for recently seen tokens
 *   seed         — simple LCG PRNG for reproducible sampling
 *
 * Usage:
 *   llm_sampler_chain_t chain;
 *   llm_sampler_chain_init(&chain);
 *   llm_sampler_chain_add(&chain, llm_sampler_temperature(0.8f));
 *   llm_sampler_chain_add(&chain, llm_sampler_top_k(40));
 *   llm_sampler_chain_add(&chain, llm_sampler_top_p(0.95f));
 *   token = llm_sampler_chain_sample(&chain, logits, n_vocab);
 *   llm_sampler_chain_accept(&chain, token); // update repetition state
 */

#include "../../lib/libc/int.h"
#include "llama-context.h"

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
typedef struct llm_sampler llm_sampler_t;

/* -------------------------------------------------------------------------
 * Sampler vtable
 * ------------------------------------------------------------------------- */
typedef struct {
    /* Apply sampler to logits/probs (n_vocab elements).
     * May modify the array in-place or fill the sorted indices array. */
    void (*apply)(llm_sampler_t* s, float* logits, int n_vocab);
    /* Notify sampler that token 'tok' was accepted. */
    void (*accept)(llm_sampler_t* s, llama_token tok);
    /* Reset sampler state (e.g. clear repetition history). */
    void (*reset)(llm_sampler_t* s);
    /* Human-readable name for debugging. */
    const char* name;
} llm_sampler_vt_t;

/* -------------------------------------------------------------------------
 * Sampler instance
 * (uses a stack-friendly fixed-size params union — no heap/SASY needed)
 * ------------------------------------------------------------------------- */

#define LLM_SAMPLER_REP_HISTORY  64   /* max tokens tracked for repetition */

typedef struct llm_sampler {
    const llm_sampler_vt_t* vt;
    union {
        struct { float temp; }         temperature;
        struct { int k; }              top_k;
        struct { float p; float min_keep; } top_p;
        struct { float p; }            min_p;
        struct { float tau; float eta; float mu; int m; } mirostat;
        struct { float alpha_freq; float alpha_pres; float penalty;
                 int32_t last[LLM_SAMPLER_REP_HISTORY];
                 int32_t n_last; }      repetition;
        struct { uint64_t state; }     seed;
    } params;
} llm_sampler_t;

/* -------------------------------------------------------------------------
 * Sampler chain
 * Fixed-capacity array of samplers; no heap allocation.
 * ------------------------------------------------------------------------- */
#define LLM_SAMPLER_CHAIN_MAX  8

typedef struct {
    llm_sampler_t  samplers[LLM_SAMPLER_CHAIN_MAX];
    int            count;
} llm_sampler_chain_t;

/* -------------------------------------------------------------------------
 * Chain lifecycle
 * ------------------------------------------------------------------------- */

void llm_sampler_chain_init(llm_sampler_chain_t* chain);

/* Add a sampler (by value copy) to the chain.  Returns 0 on success. */
int  llm_sampler_chain_add(llm_sampler_chain_t* chain, llm_sampler_t s);

/* Apply all samplers in order, then draw one token.
 * logits: raw model output, n_vocab floats (modified in-place).
 * Returns the sampled token id. */
llama_token llm_sampler_chain_sample(llm_sampler_chain_t* chain,
                                     float* logits, int n_vocab);

/* Notify all samplers that 'tok' was accepted. */
void llm_sampler_chain_accept(llm_sampler_chain_t* chain, llama_token tok);

/* Reset all sampler states in the chain. */
void llm_sampler_chain_reset(llm_sampler_chain_t* chain);

/* -------------------------------------------------------------------------
 * Sampler constructors (return by value for stack/chain storage)
 * ------------------------------------------------------------------------- */

llm_sampler_t llm_sampler_greedy(void);
llm_sampler_t llm_sampler_temperature(float temp);
llm_sampler_t llm_sampler_top_k(int k);
llm_sampler_t llm_sampler_top_p(float p, float min_keep);
llm_sampler_t llm_sampler_min_p(float p);
llm_sampler_t llm_sampler_mirostat_v2(float tau, float eta);
llm_sampler_t llm_sampler_repetition(float penalty, float alpha_freq,
                                      float alpha_pres);
llm_sampler_t llm_sampler_seed(uint64_t seed);

/* -------------------------------------------------------------------------
 * Direct sampling helpers (bypass chain for simple use cases)
 * ------------------------------------------------------------------------- */

/* Greedy: return token with highest logit. */
llama_token llm_sample_greedy(const float* logits, int n_vocab);

/* Temperature + top-k + top-p + greedy sample (all-in-one, no chain). */
llama_token llm_sample_full(float* logits, int n_vocab,
                             float temperature, int top_k, float top_p,
                             uint64_t* rng_state);

#endif /* GRACE_LLM_SAMPLER_H */
