# 🧠 LLM Q&A Memorization PoC

## Overview

This is a **proof-of-concept notebook** that validates training a tiny instruction-tuned LLM for Q&A memorization that can run on ESP32-S3.

## Quick Start (Google Colab)

[![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/your-repo/badge/blob/main/workbench/tests/llm_qa_memorization_poc/qa_memorization_poc.ipynb)

1. Upload `qa_memorization_poc.ipynb` to Google Colab
2. Run all cells (Runtime → Run all)
3. Wait ~15-30 minutes for training to complete
4. Download the generated model files

## What This PoC Does

### Phase 1: Dataset Generation
- Creates synthetic Q&A pairs from a sample profile
- Generates ~50-100 Q&A pairs with 5x repetition
- Formats data with special tokens: `<Q>question<A>answer</A>`

### Phase 2: Custom Tokenizer
- Trains a small BPE tokenizer (vocab=512) optimized for the Q&A domain
- Includes special tokens for question/answer delimiters
- Exports to llama2.c-compatible binary format

### Phase 3: Model Training
- **Quick PoC Config**: dim=128, layers=4, vocab=512 (~500K params)
- Trains from scratch on Q&A pairs (pure memorization)
- Massive overfitting (50-100 epochs) - this is the goal!
- Training time: 10-20 minutes on Colab T4 GPU

### Phase 4: Quantization
- Exports to float32 format (~2MB)
- Quantizes to int8 format (~500KB) - 4x compression!
- Total package with tokenizer: <1MB

### Phase 5: Inference Testing
- Compiles llama2.c C inference code
- Tests both float32 and int8 models
- Validates memorization quality on test questions

## Expected Results

**Memory Footprint:**
- Model (int8): ~500KB
- Tokenizer: ~100-150KB
- **Total: <1MB** (leaves 7MB for runtime in 8MB PSRAM!)

**Quality:**
- 80-90% success on exact/similar questions from training
- Lower quality on heavily paraphrased questions (acceptable for PoC)

**Performance:**
- Training: 10-20 minutes
- Inference: ~25-30 tok/s (estimated on ESP32-S3)

## Scaling to Full Iteration

Once PoC is validated, scale up:

1. **Dataset**: 1000-2000 Q&A pairs generated from full profile using GPT-4/Claude
2. **Model**: dim=224, layers=6, vocab=768 (~3.5M params → 3.5MB int8)
3. **Training**:
   - Phase 1: Pretrain on TinyStories (5-10K examples, 6-12 hours)
   - Phase 2: Fine-tune on profile Q&A (1-2 hours)
4. **Total memory**: ~4.1MB (model + tokenizer)

## File Structure

```
llm_qa_memorization_poc/
├── qa_memorization_poc.ipynb   # Main PoC notebook
├── README.md                    # This file
└── (generated during execution)
    ├── qa_data/
    │   ├── train.txt            # Training text
    │   ├── val.txt              # Validation text
    │   ├── train.bin            # Tokenized training data
    │   ├── val.bin              # Tokenized validation data
    │   ├── qa_tokenizer.model   # SentencePiece model
    │   ├── qa_tokenizer.vocab   # Vocabulary
    │   └── qa_tokenizer.bin     # Binary tokenizer for llama2.c
    ├── qa_output/
    │   ├── ckpt.pt              # PyTorch checkpoint
    │   ├── model.bin            # Float32 model
    │   └── model_q80.bin        # Int8 quantized model
    └── esp32_package/
        ├── data/
        │   ├── qa_model.bin     # Final model for ESP32
        │   └── qa_tokenizer.bin # Final tokenizer for ESP32
        └── README.md            # Deployment instructions
```

## Next Steps After PoC

1. ✅ Validate concept works with tiny model
2. Generate full dataset from complete profile using GPT-4
3. Train larger model (3.5M params)
4. Test on actual ESP32-S3 hardware
5. Iterate on dataset quality based on inference results

## Notes

- This PoC uses a **minimal config** for fast iteration (500K params)
- Full iteration will use **3.5M params** (7x larger)
- Training from scratch without language pretraining (pure memorization)
- If quality is insufficient, add TinyStories pretraining phase

## Dependencies

All dependencies are installed automatically in the notebook:
- PyTorch
- SentencePiece
- NumPy
- tqdm

## References

- [llama2.c by Karpathy](https://github.com/karpathy/llama2.c)
- ESP32 LLM implementation: `workbench/working_protos/01_llm_inference_stories260k/`
- Blog post: `blog/0018-decontructing-llama2-c-and-exp32-llm/`
