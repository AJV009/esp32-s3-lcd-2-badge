#!/usr/bin/env python3
"""
Fine-Tuning Example - Audio Semantic Model
==========================================

Quick script to fine-tune the audio semantic model with real voice recordings.

Usage:
    python finetune_example.py --real-voices real_recordings/ --epochs 100

Requirements:
    - Pre-trained model: models_xtts/audio_projection.h5
    - Real recordings: WAV files, 16kHz, mono
    - Labels: CSV file mapping audio files to query indices
"""

import os
import argparse
import numpy as np
import tensorflow as tf
import tensorflow_hub as hub
from sentence_transformers import SentenceTransformer
import librosa
import pandas as pd


# ============================================================================
# Configuration
# ============================================================================

YAMNET_URL = 'https://tfhub.dev/google/yamnet/1'
TEXT_MODEL = 'sentence-transformers/all-MiniLM-L6-v2'
EMBEDDING_DIM = 256


# ============================================================================
# Helper Functions
# ============================================================================

def load_audio_for_yamnet(file_path):
    """Load and prepare audio for YAMNet (16kHz, 3 seconds)"""
    audio, sr = librosa.load(file_path, sr=16000, mono=True)

    # Pad or trim to 3 seconds
    target_length = 3 * 16000
    if len(audio) < target_length:
        audio = np.pad(audio, (0, target_length - len(audio)))
    else:
        audio = audio[:target_length]

    return audio.astype(np.float32)


def check_audio_quality(audio, sr=16000):
    """Check if audio quality is acceptable"""
    # Check RMS level (should be 0.1-0.5)
    rms = np.sqrt(np.mean(audio**2))

    # Check for clipping (should be 0)
    clipped = np.sum(np.abs(audio) > 0.99)

    # Check duration
    duration = len(audio) / sr

    quality = {
        'rms': rms,
        'clipped_samples': clipped,
        'duration_sec': duration,
        'is_good': (0.05 < rms < 0.8) and (clipped < 10) and (duration >= 1.0)
    }

    return quality


def normalize_audio(audio):
    """Normalize audio to prevent clipping"""
    return audio / np.max(np.abs(audio)) * 0.8


# ============================================================================
# Contrastive Loss (same as training)
# ============================================================================

class ContrastiveLoss(tf.keras.losses.Loss):
    def __init__(self, temperature=0.2, **kwargs):
        super().__init__(**kwargs)
        self.temperature = temperature

    def call(self, audio_emb, text_emb):
        logits = tf.matmul(audio_emb, text_emb, transpose_b=True) / self.temperature
        batch_size = tf.shape(audio_emb)[0]
        labels = tf.range(batch_size)

        loss_a2t = tf.nn.sparse_softmax_cross_entropy_with_logits(labels=labels, logits=logits)
        loss_t2a = tf.nn.sparse_softmax_cross_entropy_with_logits(labels=labels, logits=tf.transpose(logits))

        return (tf.reduce_mean(loss_a2t) + tf.reduce_mean(loss_t2a)) / 2


class AudioTextModel(tf.keras.Model):
    def __init__(self, audio_proj, text_proj):
        super().__init__()
        self.audio_proj = audio_proj
        self.text_proj = text_proj
        self.loss_fn = ContrastiveLoss()

    def call(self, inputs):
        audio_emb, text_emb = inputs
        audio_out = self.audio_proj(audio_emb)
        text_out = self.text_proj(text_emb)
        return audio_out, text_out

    def train_step(self, data):
        audio_emb, text_emb = data

        with tf.GradientTape() as tape:
            audio_out, text_out = self([audio_emb, text_emb], training=True)
            loss = self.loss_fn(audio_out, text_out)

        gradients = tape.gradient(loss, self.trainable_variables)
        self.optimizer.apply_gradients(zip(gradients, self.trainable_variables))

        return {"loss": loss}


# ============================================================================
# Main Fine-Tuning Function
# ============================================================================

def finetune_model(args):
    """Fine-tune audio semantic model with real voice recordings"""

    print("="*70)
    print("Audio Semantic Model Fine-Tuning")
    print("="*70)

    # ------------------------------------------------------------------------
    # 1. Load Pre-trained Models
    # ------------------------------------------------------------------------

    print("\n[1/6] Loading pre-trained models...")

    # Load YAMNet
    yamnet_model = hub.load(YAMNET_URL)
    print("  ✓ YAMNet loaded")

    # Load text encoder
    text_encoder = SentenceTransformer(TEXT_MODEL)
    print("  ✓ Text encoder loaded")

    # Load pre-trained projection heads
    if not os.path.exists(args.base_model):
        raise FileNotFoundError(f"Base model not found: {args.base_model}")

    audio_projection = tf.keras.models.load_model(args.base_model, compile=False)
    print(f"  ✓ Audio projection loaded from {args.base_model}")

    # Load or create text projection (assuming same architecture)
    text_projection = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(384,)),
        tf.keras.layers.BatchNormalization(),
        tf.keras.layers.Dense(EMBEDDING_DIM),
        tf.keras.layers.Lambda(lambda x: tf.nn.l2_normalize(x, axis=1))
    ], name='text_projection')


    # ------------------------------------------------------------------------
    # 2. Load Real Voice Recordings
    # ------------------------------------------------------------------------

    print("\n[2/6] Loading real voice recordings...")

    # Load labels CSV (format: filename,query_idx,text)
    labels_path = os.path.join(args.real_voices, 'labels.csv')
    if not os.path.exists(labels_path):
        raise FileNotFoundError(
            f"Labels file not found: {labels_path}\n"
            f"Create a CSV with columns: filename,query_idx,text"
        )

    labels_df = pd.read_csv(labels_path)
    print(f"  ✓ Loaded {len(labels_df)} recordings")

    # Extract embeddings from real recordings
    real_audio_embeddings = []
    real_texts = []
    skipped = 0

    for idx, row in labels_df.iterrows():
        audio_path = os.path.join(args.real_voices, row['filename'])

        if not os.path.exists(audio_path):
            print(f"  ⚠ Skipping {row['filename']} (not found)")
            skipped += 1
            continue

        # Load and check quality
        audio = load_audio_for_yamnet(audio_path)
        quality = check_audio_quality(audio)

        if not quality['is_good']:
            print(f"  ⚠ Skipping {row['filename']} (quality: RMS={quality['rms']:.3f}, clipped={quality['clipped_samples']})")
            skipped += 1
            continue

        # Normalize
        audio = normalize_audio(audio)

        # Extract YAMNet embedding
        _, embeddings, _ = yamnet_model(audio)
        avg_embedding = tf.reduce_mean(embeddings, axis=0)
        real_audio_embeddings.append(avg_embedding.numpy())
        real_texts.append(row['text'])

        if (idx + 1) % 10 == 0:
            print(f"  Processed {idx + 1}/{len(labels_df)}...")

    real_audio_embeddings = np.array(real_audio_embeddings)
    print(f"  ✓ Extracted embeddings for {len(real_audio_embeddings)} recordings")
    if skipped > 0:
        print(f"  ⚠ Skipped {skipped} recordings (quality issues or not found)")


    # ------------------------------------------------------------------------
    # 3. Generate Text Embeddings
    # ------------------------------------------------------------------------

    print("\n[3/6] Generating text embeddings...")

    real_text_embeddings = text_encoder.encode(real_texts)
    print(f"  ✓ Generated {len(real_text_embeddings)} text embeddings")


    # ------------------------------------------------------------------------
    # 4. Combine with XTTS Data (Optional)
    # ------------------------------------------------------------------------

    if args.mix_with_xtts and os.path.exists('xtts_embeddings.npz'):
        print("\n[4/6] Mixing with XTTS data...")

        # Load XTTS embeddings (saved from training notebook)
        xtts_data = np.load('xtts_embeddings.npz')
        xtts_audio_emb = xtts_data['audio']
        xtts_text_emb = xtts_data['text']

        # Combine (50:50 ratio or custom)
        combined_audio = np.concatenate([xtts_audio_emb, real_audio_embeddings])
        combined_text = np.concatenate([xtts_text_emb, real_text_embeddings])

        print(f"  ✓ Combined: {len(xtts_audio_emb)} XTTS + {len(real_audio_embeddings)} real = {len(combined_audio)} total")
    else:
        print("\n[4/6] Using real voice data only...")
        combined_audio = real_audio_embeddings
        combined_text = real_text_embeddings


    # ------------------------------------------------------------------------
    # 5. Fine-Tune Model
    # ------------------------------------------------------------------------

    print("\n[5/6] Fine-tuning model...")
    print(f"  Learning rate: {args.learning_rate}")
    print(f"  Epochs: {args.epochs}")
    print(f"  Batch size: {args.batch_size}")

    # Create dataset
    dataset_train = tf.data.Dataset.from_tensor_slices(
        (combined_audio.astype(np.float32), combined_text.astype(np.float32))
    ).shuffle(len(combined_audio)).batch(args.batch_size)

    # Create model
    model = AudioTextModel(audio_projection, text_projection)
    model.compile(optimizer=tf.keras.optimizers.Adam(args.learning_rate))

    # Learning rate callback
    lr_callback = tf.keras.callbacks.ReduceLROnPlateau(
        monitor='loss',
        factor=0.5,
        patience=20,
        min_lr=1e-6,
        verbose=1
    )

    # Checkpoint callback
    checkpoint_callback = tf.keras.callbacks.ModelCheckpoint(
        filepath=args.output.replace('.h5', '_checkpoint_{epoch:03d}.h5'),
        save_weights_only=False,
        save_freq='epoch',
        period=50,  # Save every 50 epochs
        verbose=1
    )

    # Train
    history = model.fit(
        dataset_train,
        epochs=args.epochs,
        verbose=1,
        callbacks=[lr_callback, checkpoint_callback]
    )

    print(f"\n  ✓ Training complete!")
    print(f"  Final loss: {history.history['loss'][-1]:.4f}")
    print(f"  Best loss: {min(history.history['loss']):.4f}")


    # ------------------------------------------------------------------------
    # 6. Save Fine-Tuned Model
    # ------------------------------------------------------------------------

    print("\n[6/6] Saving fine-tuned model...")

    # Save audio projection
    audio_projection.save(args.output)
    print(f"  ✓ Saved to {args.output}")

    # Convert to TFLite if requested
    if args.convert_tflite:
        tflite_path = args.output.replace('.h5', '.tflite')

        converter = tf.lite.TFLiteConverter.from_keras_model(audio_projection)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]

        # Quantization
        def representative_dataset():
            for i in range(min(50, len(combined_audio))):
                yield [combined_audio[i:i+1].astype(np.float32)]

        converter.representative_dataset = representative_dataset
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.float32
        converter.inference_output_type = tf.float32

        tflite_model = converter.convert()

        with open(tflite_path, 'wb') as f:
            f.write(tflite_model)

        print(f"  ✓ Converted to TFLite: {tflite_path} ({len(tflite_model)/1024:.2f} KB)")

    print("\n" + "="*70)
    print("Fine-tuning complete! 🎉")
    print("="*70)
    print(f"\nNext steps:")
    print(f"  1. Test the model: python test_model.py --model {args.output}")
    print(f"  2. Deploy to ESP32: Upload {args.output.replace('.h5', '.tflite')}")
    print(f"  3. Collect feedback and iterate!")


# ============================================================================
# CLI
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Fine-tune audio semantic model with real voice recordings'
    )

    parser.add_argument(
        '--base-model',
        type=str,
        default='models_xtts/audio_projection.h5',
        help='Path to pre-trained audio projection model'
    )

    parser.add_argument(
        '--real-voices',
        type=str,
        required=True,
        help='Directory containing real voice recordings and labels.csv'
    )

    parser.add_argument(
        '--output',
        type=str,
        default='models_xtts/audio_projection_finetuned.h5',
        help='Path to save fine-tuned model'
    )

    parser.add_argument(
        '--learning-rate',
        type=float,
        default=1e-4,
        help='Learning rate for fine-tuning (lower = more conservative)'
    )

    parser.add_argument(
        '--epochs',
        type=int,
        default=100,
        help='Number of training epochs'
    )

    parser.add_argument(
        '--batch-size',
        type=int,
        default=16,
        help='Batch size for training'
    )

    parser.add_argument(
        '--mix-with-xtts',
        action='store_true',
        help='Mix real voices with XTTS data (prevents forgetting)'
    )

    parser.add_argument(
        '--convert-tflite',
        action='store_true',
        help='Convert to TFLite after training'
    )

    args = parser.parse_args()

    # Run fine-tuning
    finetune_model(args)


if __name__ == '__main__':
    main()
