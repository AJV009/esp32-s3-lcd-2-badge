#!/usr/bin/env python3
"""Combine all section CSV files into one full.csv dataset"""

import csv
from pathlib import Path

# Get all CSV files except full.csv and this script's output
dataset_files = [
    "personal_details.csv",
    "professional_experience.csv",
    "education_qualifications.csv",
    "technical_skills.csv",
    "notable_projects.csv",
    "certifications_achievements.csv",
    "research_interests.csv",
    "personal_interests_hobbies.csv"
]

output_file = Path(__file__).parent / "full.csv"
all_rows = []

# Read all CSV files
for csv_file in dataset_files:
    filepath = Path(__file__).parent / csv_file
    if filepath.exists():
        with open(filepath, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                all_rows.append({
                    'question': row['question'],
                    'answer': row['answer']
                })
        print(f"✓ Loaded {csv_file}: {len(list(csv.DictReader(open(filepath))))} rows")

# Write combined CSV with sequential IDs
with open(output_file, 'w', encoding='utf-8', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['id', 'question', 'answer'])

    for idx, row in enumerate(all_rows, 1):
        writer.writerow([idx, row['question'], row['answer']])

print(f"\n✓ Combined {len(all_rows)} QnA pairs into full.csv")
print(f"✓ Saved to: {output_file}")
