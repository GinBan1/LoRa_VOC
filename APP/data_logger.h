#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

/*
 * Set this to 1 while collecting TinyML training data.
 *
 * Collection mode runs only the environment sensor task and this logger.
 * LoRa and TinyML inference remain in the project, but are not scheduled, so
 * their diagnostic output cannot corrupt the captured CSV stream.
 */
#ifndef DATA_LOGGER_COLLECTION_MODE
#define DATA_LOGGER_COLLECTION_MODE 0U
#endif

#define DATA_LOGGER_WARMUP_MS       60000U
#define DATA_LOGGER_SAMPLE_PERIOD_MS 1000U

void DataLogger_App_Init(void);
void DataLogger_App_Task(void);

#endif /* DATA_LOGGER_H */
