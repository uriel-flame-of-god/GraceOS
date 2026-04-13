# GraceOS LLM Runtime

**Location**: `kernel/llm/`  
**Userland API**: `lib/libgrace/llm.c`  
**Syscall Range**: 150–162  
**Last Updated**: February 2026

---

## Overview

The LLM Runtime is an in-kernel large language model inference engine. It loads quantized models in GGUF format directly from BranchFS into SASY-managed segments, runs the transformer forward pass inside the kernel, and exposes results to userland through 13 dedicated syscalls.

No userland components are required for inference — all weight storage, KV-cache allocation, and forward-pass computation happen in kernel mode via the SASY segment allocator.

---

## Architecture

```
┌───────────────────────────────────────────────────────┐
│                   Userland Process                    │
│       llm_load_model()  →  llm_chat_generate()        │
│              (lib/libgrace/llm.c)                     │
└───────────────┬───────────────────────────────────────┘
                │ INT 0x80  (SYS_LLM_*, 150–162)
┌───────────────▼───────────────────────────────────────┐
│              LLM Runtime (kernel/llm/)                │
│                                                       │
│  runtime.c          — syscall dispatcher              │
│  ├── llama-io       — GGUF v2/v3 file reader          │
│  ├── llama-mmap     — segment-backed weight map       │
│  ├── llama-arch     — architecture metadata           │
│  ├── llama-quant    — quantization dequantizers       │
│  ├── llama-context  — forward pass, KV-cache, RoPE    │
│  ├── llama-sampler  — next-token sampling             │
│  ├── llama-chat     — chat template engine            │
│  ├── llama-adapter  — LoRA weight loading             │
│  ├── llama-impl     — math helpers, logging           │
│  ├── unicode        — BPE / SentencePiece tokenizer   │
│  └── unicode-data   — Unicode category tables         │
└───────────────┬───────────────────────────────────────┘
                │ sasy_create / sasy_lock / sasy_unlock
┌───────────────▼───────────────────────────────────────┐
│           SASY Segment Allocator (kernel/mm/sasy/)    │
└───────────────────────────────────────────────────────┘
```

---

## Source Files

| File | Purpose |
|------|---------|
| `runtime.c/h` | Subsystem entry point; wires 13 syscall handlers into the dispatcher |
| `llama-arch.c/h` | Architecture type → tensor name mappings (LLaMA, Mistral, Phi, …) |
| `llama-io.c/h` | GGUF v2/v3 binary reader; parses header, tensor metadata, KV store |
| `llama-mmap.c/h` | Maps GGUF tensor data into SASY segments; owns weight segment handles |
| `llama-quant.c/h` | Dequantization kernels for Q4_0, Q4_1, Q5_0, Q8_0, Q4_K, Q5_K, Q6_K |
| `llama-context.c/h` | Inference context: KV-cache, RoPE positional encoding, GQA, SwiGLU |
| `llama-sampler.c/h` | Sampling strategies applied to final logits |
| `llama-chat.c/h` | Chat template rendering into token sequences |
| `llama-adapter.c/h` | LoRA adapter file loading and weight delta merging |
| `llama-impl.c/h` | f16↔f32 conversion, SiLU/GELU activations, `llm_snprintf`, log macros |
| `unicode.c/h` | BPE tokenizer, SentencePiece-compatible encode/decode, byte fallback |
| `unicode-data.c/h` | Unicode codepoint category and normalization tables |

---

## GGUF Loading

GGUF is the standard model-exchange format produced by `llama.cpp`. The runtime supports v2 and v3 magic.

Loading sequence:

```
sys_llm_load_model(path)
   │
   ├─ llama_io_open()        — read GGUF header from BranchFS file
   ├─ llama_arch_detect()    — identify architecture from general.architecture key
   ├─ sasy_create(weight_sz) — allocate one SASY segment for all tensor data
   ├─ llama_mmap_load()      — copy tensor blobs into the segment
   └─ returns model_id (0–3)
```

Up to **4 models** may be loaded concurrently (slots 0–3). Each model's weights are held in a single SASY segment for the lifetime of the model.

---

## Inference Contexts

A context holds the KV-cache and sampler state for one active generation. Up to **8 contexts** may be open simultaneously (slots 0–7), each associated with a loaded model.

```
sys_llm_create_context(model_id, ctx_len)
   │
   ├─ sasy_create(kv_cache_sz)  — KV-cache segment (K and V tensors)
   ├─ sasy_create(logit_sz)     — logit buffer segment
   └─ returns ctx_id (0–7)
```

`ctx_len` sets the context window size in tokens and is clamped to the model's `context_length` metadata value.

---

## Quantization Formats

All dequantization is performed on-the-fly before matrix multiplication. No pre-dequantized copy of the weights is stored.

| Format | Bits/Weight | Block Size | Description |
|--------|-------------|------------|-------------|
| Q4_0 | 4 | 32 | 4-bit, zero-point = 0 |
| Q4_1 | 4 | 32 | 4-bit with non-zero minimum |
| Q5_0 | 5 | 32 | 5-bit, zero-point = 0 |
| Q8_0 | 8 | 32 | 8-bit, zero-point = 0 |
| Q4_K | 4 | 256 | 4-bit super-block (K-quants) |
| Q5_K | 5 | 256 | 5-bit super-block (K-quants) |
| Q6_K | 6 | 256 | 6-bit super-block (K-quants) |
| F16 | 16 | — | Native half-precision |
| F32 | 32 | — | Full precision |

---

## Transformer Forward Pass

The forward pass in `llama-context.c` implements the standard decoder-only LLaMA architecture:

1. **Token embedding** — lookup embedding matrix row for each input token
2. **RoPE** — apply rotary positional encoding to Q and K projections
3. **GQA** — grouped-query attention; K/V heads may be shared across Q head groups
4. **SwiGLU** — gated feed-forward block (`w1·gate ⊙ SiLU(w1·x)`) 
5. **RMSNorm** — applied before attention and before the FFN block
6. **KV-cache** — past keys and values stored in the SASY segment; filled incrementally

---

## Sampling Algorithms

After `sys_llm_eval`, call `sys_llm_sample` to draw the next token. The active strategy is configured when the context is created.

| Strategy | Description |
|----------|-------------|
| Greedy | `argmax(logits)` — deterministic, fastest |
| Temperature | Scale logits by `1/T` before softmax, then sample |
| Top-K | Sample from the K highest-probability tokens |
| Top-P (nucleus) | Sample from the smallest set with cumulative probability ≥ P |
| Min-P | Discard tokens with probability < P × max\_prob |
| Mirostat v2 | Adaptive sampling targeting a fixed perplexity budget |

---

## Chat Template Engine

`sys_llm_chat_begin` selects the template and optional system prompt. `sys_llm_chat_append` queues role/content pairs. `sys_llm_chat_generate` formats the full prompt, tokenizes it, runs `eval + sample` in a loop, and returns the decoded reply.

| Template ID | Name | Format |
|-------------|------|--------|
| 0 | `LLM_TMPL_CHATML` | `<\|im_start\|>role\ncontent<\|im_end\|>` |
| 1 | `LLM_TMPL_LLAMA2` | `[INST] <<SYS>>\n…\n<</SYS>>\n\n…[/INST]` |
| 2 | `LLM_TMPL_LLAMA3` | `<\|start_header_id\|>role<\|end_header_id\|>\n\ncontent<\|eot_id\|>` |
| 3 | `LLM_TMPL_MISTRAL` | `[INST] content [/INST]` |
| 4 | `LLM_TMPL_VICUNA` | `USER: content\nASSISTANT:` |
| 5 | `LLM_TMPL_ALPACA` | `### Instruction:\ncontent\n### Response:` |
| 6 | `LLM_TMPL_AUTO` | Kernel auto-detects from model metadata |

---

## LoRA Adapters

LoRA weight deltas are loaded from a separate GGUF file and merged into the base model weights at load time.

```c
// Pseudocode — not yet exposed as a dedicated syscall.
// Currently applied by providing the adapter path alongside the base model
// through the runtime.c adapter hooks.
```

The adapter loader (`llama-adapter.c`) reads `lora_A` / `lora_B` tensor pairs, computes `ΔW = A·B` scaled by `alpha/rank`, and adds the result to the appropriate base-model tensor.

---

## Syscall Reference

All LLM syscalls follow the standard GraceOS ABI (INT 0x80, register-based). Return value ≥ 0 means success; −1 means error.

| Number | Name | Arguments | Returns |
|--------|------|-----------|---------|
| 150 | SYS_LLM_LOAD_MODEL | `a1`=path | model_id or −1 |
| 151 | SYS_LLM_UNLOAD_MODEL | `a1`=model_id | 0 or −1 |
| 152 | SYS_LLM_CREATE_CONTEXT | `a1`=model_id, `a2`=ctx_len | ctx_id or −1 |
| 153 | SYS_LLM_DESTROY_CONTEXT | `a1`=ctx_id | 0 or −1 |
| 154 | SYS_LLM_TOKENIZE | `a1`=ctx_id, `a2`=text, `a3`=out_tokens, `a4`=max | n_tokens or −1 |
| 155 | SYS_LLM_DETOKENIZE | `a1`=ctx_id, `a2`=tokens, `a3`=n, `a4`=buf, `a5`=sz | bytes or −1 |
| 156 | SYS_LLM_EVAL | `a1`=ctx_id, `a2`=tokens, `a3`=n_tokens | 0 or −1 |
| 157 | SYS_LLM_SAMPLE | `a1`=ctx_id | token_id or −1 |
| 158 | SYS_LLM_CHAT_BEGIN | `a1`=ctx_id, `a2`=tmpl_id, `a3`=system_prompt | 0 or −1 |
| 159 | SYS_LLM_CHAT_APPEND | `a1`=ctx_id, `a2`=role, `a3`=content | 0 or −1 |
| 160 | SYS_LLM_CHAT_GENERATE | `a1`=ctx_id, `a2`=out_buf, `a3`=max_tokens, `a4`=buf_sz | bytes or −1 |
| 161 | SYS_LLM_CHAT_RESET | `a1`=ctx_id | 0 or −1 |
| 162 | SYS_LLM_GET_INFO | `a1`=model_id, `a2`=buf, `a3`=buf_sz | bytes or −1 |

---

## Userland API (libgrace)

Include `lib/libgrace/llm.h` and link against `lib/libgrace/llm.c`.

### One-shot helper

```c
char reply[1024];
int n = llm_ask("/models/phi3.gguf",
                "You are a helpful assistant.",
                "What is 2 + 2?",
                reply, sizeof(reply), 64);
if (n > 0)
    println(reply);
```

### Low-level API

```c
/* Load model */
llm_model_id_t model = llm_load_model("/models/llama3.gguf");
if (model == LLM_INVALID_ID) { /* error */ }

/* Create inference context with 512-token window */
llm_ctx_id_t ctx = llm_create_context(model, 512);

/* Start a chat session using ChatML template */
llm_chat_begin(ctx, LLM_TMPL_CHATML, "You are a helpful assistant.");

/* Add a user turn */
llm_chat_append(ctx, LLM_ROLE_USER, "What is the capital of France?");

/* Generate reply (up to 128 tokens, fits in 512-byte buffer) */
char out[512];
int bytes = llm_chat_generate(ctx, out, 128, sizeof(out));
if (bytes > 0)
    println(out);

/* Continue the conversation */
llm_chat_append(ctx, LLM_ROLE_USER, "And Germany?");
bytes = llm_chat_generate(ctx, out, 64, sizeof(out));
if (bytes > 0)
    println(out);

/* Reset context (keeps model loaded) */
llm_chat_reset(ctx);

/* Tear down */
llm_destroy_context(ctx);
llm_unload_model(model);
```

### Manual token-level loop

```c
llama_token tokens[256];
int n_tok = llm_tokenize(ctx, "Hello, world!", tokens, 256);

llm_eval(ctx, tokens, n_tok);

for (int i = 0; i < 50; i++) {
    llama_token next = llm_sample(ctx);
    if (next <= 0) break;          /* EOS or error */
    llm_eval(ctx, &next, 1);

    char piece[32];
    llm_detokenize(ctx, &next, 1, piece, sizeof(piece));
    print(piece);
}
```

---

## Memory Layout

All LLM allocations go through SASY. The segment types used are:

| Segment | Type | Contents |
|---------|------|----------|
| Weight segment | `SEG_TYPE_DATA` | Raw tensor data from GGUF file |
| KV-cache segment | `SEG_TYPE_DATA` | Key and Value tensors for past tokens |
| Logit buffer | `SEG_TYPE_DATA` | Output logit vector (vocab_size × f32) |

Segments are created at `llm_load_model` / `llm_create_context` time and freed automatically by `llm_unload_model` / `llm_destroy_context`.

---

## Constraints

| Limit | Value |
|-------|-------|
| Max concurrent models | 4 |
| Max concurrent contexts | 8 |
| Max context window | capped by model metadata |
| Supported GGUF versions | v2, v3 |
| Supported architectures | LLaMA, Mistral, Phi (extensible via `llama-arch.c`) |

---

## See Also

- [KERNEL.MD](KERNEL.MD) — Kernel subsystem overview
- [SASY.MD](SASY.MD) — Segment allocator details
- [MEMORY.MD](MEMORY.MD) — Memory management
- [API.MD](API.MD) — Full syscall reference
