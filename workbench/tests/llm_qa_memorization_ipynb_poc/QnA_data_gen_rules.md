# Q&A Generation & Variation Rules

## **Core Formatting Rules:**
1. **Output format:** `<|user|>{question}<|assistant|>{answer}<|end|>`
2. **Answer style:** Short, factual, direct - no explanations unless essential
3. **Answer length:** 1-15 words preferred, max 25 words for complex answers
4. **No conversational fluff:** Avoid "Well," "Actually," "Sure," etc.
5. **Avoid punctuation:** no full stops or commas and such

## **Pronoun Alternation:**
5. **50/50 mix rule:** Alternate between "Alphons" and pronouns (he/his/him)
6. **Within questions:** Use either "Alphons" OR pronoun, not both
7. **Within answers:** Prefer facts over pronouns (e.g., "Python, PHP, Rust" not "He knows Python, PHP, Rust")
8. **Exception:** Use full name "Alphons Jaimon" only for identity questions

## **Question Variation Patterns:**
9. **Question word diversity:** Rotate through: What/Where/When/Who/Which/Does/Is/Can/Has/Tell
10. **Phrasing styles (rotate equally):**
    - Direct: "What is the email?"
    - Possessive: "What is Alphons's current role?"
    - Action-based: "Where does he work?"
    - Confirmation: "Does he know Python?"

11. **Question length variation:**
    - Short (3-5 words): 40%
    - Medium (6-9 words): 50%
    - Longer (10-15 words): 10%

## **Answer Variation Patterns:**
12. **For factual data (dates/names/places):** Give exact value only
    - Good: "May 2023"
    - Bad: "He joined in May 2023"

13. **For lists (skills/languages/tools):** Comma-separated, no "and" before last item unless <4 items
    - Good: "Python, PHP, Rust, Javascript"
    - Acceptable: "AWS, Azure and GCP" (only 3 items)

14. **For yes/no questions:** Answer "Yes" or "No" + minimal fact
    - Good: "Yes, Python, PHP, and Rust"
    - Bad: "Yes, he knows Python, PHP, and Rust"

15. **For role/title questions:** State position, optionally + company
    - Good: "GenAI Engineer at Etherwise"
    - Also good: "GenAI Engineer"

## **Difficulty Levels:**
16. **Difficulty levels (rotate):**
    - Easy recall (email, name, current job): 40%
    - Medium detail (specific dates, past roles): 40%
    - Specific facts (project names, tool lists): 20%

## **Paraphrasing Rules:**
17. **Create 3-5 variations per fact** with different:
    - Question words (What/Where/Which)
    - Phrasings (current job / where work / current role)
    - Pronoun vs name usage

18. **Temporal variations for time-based facts:**
    - "When did Alphons join X?"
    - "What year did he start at X?"
    - "How long has Alphons worked at X?"

19. **Avoid redundancy:** Don't repeat exact same question structure consecutively

## **Quality Control:**
20. **No multi-hop reasoning:** One fact per Q&A pair
21. **No inference required:** All answers must be explicitly in source material
22. **No ambiguity:** Questions must have single clear answer
23. **Consistency:** Same fact should have same answer across variations
24. **Completeness:** For list questions, include all items from source (unless specifically "some examples")
