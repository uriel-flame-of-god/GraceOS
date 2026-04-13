/*
 * llama-sampler.c — Token sampling algorithms
 *
 * All samplers operate on stack buffers and avoid heap allocation.
 * The PRNG is a fast splitmix64 LCG seeded from the seed sampler.
 */

#include "llama-sampler.h"
#include "llama-impl.h"
#include "../log/klog.h"

/* -------------------------------------------------------------------------
 * PRNG (splitmix64)
 * ------------------------------------------------------------------------- */

static uint64_t splitmix64(uint64_t* state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* -------------------------------------------------------------------------
 * Greedy sampler
 * ------------------------------------------------------------------------- */

static void greedy_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    (void)s; (void)logits; (void)n_vocab;
    /* Greedy just reads the max; no transform needed. */
}

static void noop_accept(llm_sampler_t* s, llama_token tok) { (void)s; (void)tok; }
static void noop_reset (llm_sampler_t* s)                  { (void)s; }

static const llm_sampler_vt_t vt_greedy = {
    greedy_apply, noop_accept, noop_reset, "greedy"
};

llm_sampler_t llm_sampler_greedy(void)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_greedy;
    return s;
}

/* -------------------------------------------------------------------------
 * Temperature sampler: logits[i] /= temp
 * ------------------------------------------------------------------------- */

static void temperature_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    float temp = s->params.temperature.temp;
    if (temp <= 0.0f || temp == 1.0f) return;
    float inv = 1.0f / temp;
    for (int i = 0; i < n_vocab; i++) logits[i] *= inv;
}

static const llm_sampler_vt_t vt_temperature = {
    temperature_apply, noop_accept, noop_reset, "temperature"
};

llm_sampler_t llm_sampler_temperature(float temp)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_temperature;
    s.params.temperature.temp = temp;
    return s;
}

/* -------------------------------------------------------------------------
 * Top-K: zero out all but the K highest logits.
 * Uses a simple selection to find the K-th largest value — O(n_vocab * k)
 * but k is small.
 * ------------------------------------------------------------------------- */

static void top_k_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    int k = s->params.top_k.k;
    if (k <= 0 || k >= n_vocab) return;

    /* Find the k-th largest via partial selection */
    /* Use a small heap of size k */
#define TOP_K_MAX 200
    if (k > TOP_K_MAX) k = TOP_K_MAX;
    float kth[TOP_K_MAX];  /* min-heap */
    int   heap_sz = 0;

    for (int i = 0; i < n_vocab; i++) {
        if (heap_sz < k) {
            kth[heap_sz++] = logits[i];
            /* Simple insertion sort to maintain min at kth[0] */
            for (int j = heap_sz - 1; j > 0 && kth[j] < kth[j-1]; j--) {
                float tmp = kth[j]; kth[j] = kth[j-1]; kth[j-1] = tmp;
            }
        } else if (logits[i] > kth[0]) {
            kth[0] = logits[i];
            /* Sift down */
            for (int j = 0; j + 1 < k && kth[j] > kth[j+1]; j++) {
                float tmp = kth[j]; kth[j] = kth[j+1]; kth[j+1] = tmp;
            }
        }
    }

    float threshold = kth[0];
    /* Zero out logits below threshold (set to -1e38) */
    for (int i = 0; i < n_vocab; i++) {
        if (logits[i] < threshold)
            logits[i] = -1e38f;
    }
#undef TOP_K_MAX
}

static const llm_sampler_vt_t vt_top_k = {
    top_k_apply, noop_accept, noop_reset, "top_k"
};

llm_sampler_t llm_sampler_top_k(int k)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_top_k;
    s.params.top_k.k = k;
    return s;
}

/* -------------------------------------------------------------------------
 * Top-P (nucleus) sampler:
 *   Convert to softmax probabilities, sort descending, truncate at cumsum > p.
 * ------------------------------------------------------------------------- */

static void top_p_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    float p        = s->params.top_p.p;
    int   min_keep = (int)s->params.top_p.min_keep;
    if (p >= 1.0f) return;

    /* Compute softmax */
    float max_l = logits[0];
    for (int i = 1; i < n_vocab; i++) if (logits[i] > max_l) max_l = logits[i];
    float sum = 0.0f;
    for (int i = 0; i < n_vocab; i++) {
        logits[i] = llm_fast_expf(logits[i] - max_l);
        sum += logits[i];
    }
    float inv = 1.0f / sum;
    for (int i = 0; i < n_vocab; i++) logits[i] *= inv;

    /* Find minimum probability that keeps cumsum ≤ p */
    /* Use a threshold scan (O(n²) in worst case, but n_vocab on modern CPUs is fast) */
    float cumsum = 0.0f;
    float min_p_threshold = 0.0f;
    int   kept = 0;

    /* We need to scan in descending order but avoid sorting (no heap alloc).
     * Strategy: iterate passes.  For n_vocab up to ~32k this is acceptable. */
    for (int iter = 0; iter < n_vocab && cumsum < p; iter++) {
        /* Find max remaining */
        float cur_max = -1e38f;
        for (int i = 0; i < n_vocab; i++) {
            if (logits[i] > min_p_threshold && logits[i] > cur_max)
                cur_max = logits[i];
        }
        if (cur_max < 0.0f) break;
        cumsum += cur_max;
        kept++;
        min_p_threshold = cur_max - 1e-8f;
        if (kept >= min_keep && cumsum >= p) break;
    }

    /* Zero out tokens below min_p_threshold */
    for (int i = 0; i < n_vocab; i++) {
        if (logits[i] < min_p_threshold)
            logits[i] = 0.0f;
    }
}

static const llm_sampler_vt_t vt_top_p = {
    top_p_apply, noop_accept, noop_reset, "top_p"
};

llm_sampler_t llm_sampler_top_p(float p, float min_keep)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_top_p;
    s.params.top_p.p        = p;
    s.params.top_p.min_keep = min_keep < 1.0f ? 1.0f : min_keep;
    return s;
}

/* -------------------------------------------------------------------------
 * Min-P sampler: zero tokens below p * max_prob
 * ------------------------------------------------------------------------- */

static void min_p_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    float p = s->params.min_p.p;
    if (p <= 0.0f) return;

    /* Convert to probs */
    float max_l = logits[0];
    for (int i = 1; i < n_vocab; i++) if (logits[i] > max_l) max_l = logits[i];
    float sum = 0.0f;
    for (int i = 0; i < n_vocab; i++) {
        logits[i] = llm_fast_expf(logits[i] - max_l);
        sum += logits[i];
    }
    float inv = 1.0f / sum;
    /* Find max prob */
    float max_prob = 0.0f;
    for (int i = 0; i < n_vocab; i++) {
        logits[i] *= inv;
        if (logits[i] > max_prob) max_prob = logits[i];
    }
    float threshold = p * max_prob;
    for (int i = 0; i < n_vocab; i++) {
        if (logits[i] < threshold) logits[i] = 0.0f;
    }
}

static const llm_sampler_vt_t vt_min_p = {
    min_p_apply, noop_accept, noop_reset, "min_p"
};

llm_sampler_t llm_sampler_min_p(float p)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_min_p;
    s.params.min_p.p = p;
    return s;
}

/* -------------------------------------------------------------------------
 * Mirostat v2 sampler
 * Maintains a running estimate of surprisal (mu) and adjusts temperature.
 * ------------------------------------------------------------------------- */

static void mirostat_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    float tau = s->params.mirostat.tau;
    float eta = s->params.mirostat.eta;
    float mu  = s->params.mirostat.mu;

    /* Apply softmax */
    float max_l = logits[0];
    for (int i = 1; i < n_vocab; i++) if (logits[i] > max_l) max_l = logits[i];
    float sum = 0.0f;
    for (int i = 0; i < n_vocab; i++) {
        logits[i] = llm_fast_expf(logits[i] - max_l);
        sum += logits[i];
    }
    float inv = 1.0f / sum;
    for (int i = 0; i < n_vocab; i++) logits[i] *= inv;

    /* Compute surprisal of current distribution and truncate */
    /* For mirostat: keep tokens whose -log(p) <= mu */
    float ln2 = 0.693147f;
    for (int i = 0; i < n_vocab; i++) {
        if (logits[i] <= 0.0f) continue;
        float surprise = -ln2 * logits[i];  /* approximation */
        if (surprise > mu) logits[i] = 0.0f;
    }

    /* Update mu: mu = mu + eta * (observed_surprise - tau) */
    /* Use the H (entropy approx) of remaining distribution */
    float entropy = 0.0f;
    for (int i = 0; i < n_vocab; i++) {
        if (logits[i] > 0.0f)
            entropy -= logits[i] * (logits[i] < 1e-10f ? -23.0f
                        : llm_fast_expf(-logits[i]));  /* rough */
    }
    s->params.mirostat.mu = mu + eta * (entropy - tau);
}

static const llm_sampler_vt_t vt_mirostat = {
    mirostat_apply, noop_accept, noop_reset, "mirostat_v2"
};

llm_sampler_t llm_sampler_mirostat_v2(float tau, float eta)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_mirostat;
    s.params.mirostat.tau = tau;
    s.params.mirostat.eta = eta;
    s.params.mirostat.mu  = tau * 2.0f;
    s.params.mirostat.m   = 100;
    return s;
}

/* -------------------------------------------------------------------------
 * Repetition penalty sampler
 * ------------------------------------------------------------------------- */

static void repetition_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    float penalty     = s->params.repetition.penalty;
    float alpha_freq  = s->params.repetition.alpha_freq;
    float alpha_pres  = s->params.repetition.alpha_pres;
    int   n_last      = s->params.repetition.n_last;
    int32_t* last     = s->params.repetition.last;

    if (penalty == 1.0f && alpha_freq == 0.0f && alpha_pres == 0.0f)
        return;

    for (int i = 0; i < n_last; i++) {
        int tok = (int)last[i];
        if (tok < 0 || tok >= n_vocab) continue;
        /* Repetition penalty (multiplicative) */
        if (logits[tok] > 0.0f) logits[tok] /= penalty;
        else                    logits[tok] *= penalty;
        /* Presence penalty (additive) */
        logits[tok] -= alpha_pres;
        /* Frequency penalty (proportional to count) */
        int freq = 0;
        for (int j = 0; j < n_last; j++) if (last[j] == last[i]) freq++;
        logits[tok] -= alpha_freq * (float)freq;
    }
}

static void repetition_accept(llm_sampler_t* s, llama_token tok)
{
    int32_t* last  = s->params.repetition.last;
    int*     n_last = &s->params.repetition.n_last;
    int      max   = LLM_SAMPLER_REP_HISTORY;
    if (*n_last < max) {
        last[(*n_last)++] = (int32_t)tok;
    } else {
        /* Shift left, append */
        for (int i = 0; i < max - 1; i++) last[i] = last[i+1];
        last[max - 1] = (int32_t)tok;
    }
}

static void repetition_reset(llm_sampler_t* s)
{
    s->params.repetition.n_last = 0;
}

static const llm_sampler_vt_t vt_repetition = {
    repetition_apply, repetition_accept, repetition_reset, "repetition"
};

llm_sampler_t llm_sampler_repetition(float penalty, float alpha_freq,
                                      float alpha_pres)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_repetition;
    s.params.repetition.penalty    = penalty;
    s.params.repetition.alpha_freq = alpha_freq;
    s.params.repetition.alpha_pres = alpha_pres;
    return s;
}

/* -------------------------------------------------------------------------
 * Seed (PRNG seeding) sampler  — used as the final random draw step
 * ------------------------------------------------------------------------- */

static void seed_apply(llm_sampler_t* s, float* logits, int n_vocab)
{
    (void)s; (void)logits; (void)n_vocab;
    /* The actual random draw happens in chain_sample via the seed state */
}

static const llm_sampler_vt_t vt_seed = {
    seed_apply, noop_accept, noop_reset, "seed"
};

llm_sampler_t llm_sampler_seed(uint64_t seed)
{
    llm_sampler_t s;
    llm_zero(&s, sizeof(s));
    s.vt = &vt_seed;
    s.params.seed.state = seed ? seed : 12345678901234567ULL;
    return s;
}

/* -------------------------------------------------------------------------
 * Sampler chain
 * ------------------------------------------------------------------------- */

void llm_sampler_chain_init(llm_sampler_chain_t* chain)
{
    llm_zero(chain, sizeof(*chain));
}

int llm_sampler_chain_add(llm_sampler_chain_t* chain, llm_sampler_t s)
{
    if (chain->count >= LLM_SAMPLER_CHAIN_MAX) {
        klog_warn("[llm-sampler] chain full");
        return -1;
    }
    chain->samplers[chain->count++] = s;
    return 0;
}

llama_token llm_sampler_chain_sample(llm_sampler_chain_t* chain,
                                     float* logits, int n_vocab)
{
    /* Apply all samplers */
    for (int i = 0; i < chain->count; i++)
        chain->samplers[i].vt->apply(&chain->samplers[i], logits, n_vocab);

    /* If a seed sampler is in the chain, use its PRNG for random draw */
    uint64_t* rng = (uint64_t*)0;
    for (int i = 0; i < chain->count; i++) {
        if (chain->samplers[i].vt == &vt_seed) {
            rng = &chain->samplers[i].params.seed.state;
            break;
        }
    }

    /* Normalize remaining probabilities and draw */
    float sum = 0.0f;
    for (int i = 0; i < n_vocab; i++) if (logits[i] > 0.0f) sum += logits[i];

    if (sum <= 0.0f || !rng) {
        /* Greedy fallback */
        return llm_sample_greedy(logits, n_vocab);
    }

    uint64_t rval  = splitmix64(rng);
    float    r_f   = (float)(rval >> 11) / (float)(1ULL << 53);  /* [0,1) */
    float    r_scaled = r_f * sum;
    float    cum = 0.0f;
    for (int i = 0; i < n_vocab; i++) {
        if (logits[i] <= 0.0f) continue;
        cum += logits[i];
        if (cum >= r_scaled) return (llama_token)i;
    }
    /* Fallback: return last non-zero token */
    for (int i = n_vocab - 1; i >= 0; i--)
        if (logits[i] > 0.0f) return (llama_token)i;
    return 0;
}

void llm_sampler_chain_accept(llm_sampler_chain_t* chain, llama_token tok)
{
    for (int i = 0; i < chain->count; i++)
        chain->samplers[i].vt->accept(&chain->samplers[i], tok);
}

void llm_sampler_chain_reset(llm_sampler_chain_t* chain)
{
    for (int i = 0; i < chain->count; i++)
        chain->samplers[i].vt->reset(&chain->samplers[i]);
}

/* -------------------------------------------------------------------------
 * Direct helpers
 * ------------------------------------------------------------------------- */

llama_token llm_sample_greedy(const float* logits, int n_vocab)
{
    int   best = 0;
    float best_val = logits[0];
    for (int i = 1; i < n_vocab; i++) {
        if (logits[i] > best_val) { best_val = logits[i]; best = i; }
    }
    return (llama_token)best;
}

llama_token llm_sample_full(float* logits, int n_vocab,
                             float temperature, int top_k, float top_p,
                             uint64_t* rng)
{
    llm_sampler_chain_t chain;
    llm_sampler_chain_init(&chain);
    if (temperature > 0.0f && temperature != 1.0f)
        llm_sampler_chain_add(&chain, llm_sampler_temperature(temperature));
    if (top_k > 0 && top_k < n_vocab)
        llm_sampler_chain_add(&chain, llm_sampler_top_k(top_k));
    if (top_p < 1.0f)
        llm_sampler_chain_add(&chain, llm_sampler_top_p(top_p, 1.0f));

    /* Inject RNG seed */
    uint64_t seed_val = rng ? *rng : 42ULL;
    llm_sampler_chain_add(&chain, llm_sampler_seed(seed_val));

    llama_token tok = llm_sampler_chain_sample(&chain, logits, n_vocab);
    /* Propagate updated RNG state back */
    if (rng) {
        for (int i = 0; i < chain.count; i++) {
            if (chain.samplers[i].vt == &vt_seed) {
                *rng = chain.samplers[i].params.seed.state;
                break;
            }
        }
    }
    return tok;
}
