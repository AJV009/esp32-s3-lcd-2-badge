Its gonna be a full fledged app with a working cycle:


An ideal working loop:
1: Boots

2: The OAISYS LOGO loops
2.1: Device gyro/accel features:
- IF accelerometer stale for 5min then DEEP SLEEP
- IF accelerometer moves for large variations then WAKE (Like giving it a shake)
- continue looping logo
- IF gyro moved, move screen

3: Press Blue button OR activate with wake word
3.1: Listens for 3-5seconds, records and stores to SD memory

4: Process the audio

4.1: IF WiFi Connected: (Scans every 30 seconds)
4.1.1: Check for new embedding model version OR language Model version OR embedding match dataset on S3
4.1.2: If embedding model version OR language Model version OR embedding match dataset found with new date, then Download it, delete old files

4.2: UNLOAD wake-word model
4.3: LOAD Embedding Model
4.3.1: audio_in_1 -> YAMNet-256 -> embeddings
4.3.2: Check cosine similarity to roughly 300 [embeddings, string] dataset
4.3.3: Return similar_string
4.3.4: UNLOAD Embedding Model

4.4: The check logic:
4.4.1: [audio_in_1 + embedding + score + similar_string] (full array referred to as in_data)
4.4.2: If score more than 0.7 -> Send similar_string to LLM
4.4.3: If score less than 0.7 -> Stash in_data to memory -> Trigger TTS for a generic response saying "I'm sorry I couldn't get a transcription for that"

4.5: Stashed in_data management:
4.5.1: ESP32 connects to my WiFi (Scans every 30 seconds)
4.5.2: Once 10 audio files are stashed
4.5.3: Auto upload a packaged file of all the 10 in_data stashed to s3

4.5.4: A simple python Web App with a redis based queue for getting transcripts of 10 audio files all the background. (triggered due to the upload that just happened)
4.5.5: Update the main dataset with the in_data + high quality cloud based transcripts
4.5.6: The web page has a simple table with buttons, (correction_ui)
4.5.6.1: Allows one to play an audio file from the database, correct the transcription sentence, hit save to update the dataset.
4.5.6.2: Has one additional field for adding "Possible Responses" and a fine-tune button
4.5.6.3: If fine-tune triggered, the last model is picked as a checkpoint and continued training with the new data, thereby updating the checkpoint file in the server

5: IF LLM triggered (string matched from the embedding)
5.1: LOAD the LLM Model
5.2: Perform the inference
5.3: STOP looping logo, STREAM the response on screen (LOGO always loops until there is text to display, until then everything so far happened in the background)
5.4: Once stream completes, UNLOAD the LLM
5.5: speak it out using TTS from the speaker 
5.6: Hold text on display for next 10 seconds, continue LOGO looping

