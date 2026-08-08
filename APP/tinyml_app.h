#ifndef TINYML_APP_H
#define TINYML_APP_H

#include <stdint.h>

typedef enum
{
    TINYML_STATE_WARMING_UP = 0,
    TINYML_STATE_COLLECTING_WINDOW,
    TINYML_STATE_NORMAL,
    TINYML_STATE_WARNING,
    TINYML_STATE_UNCERTAIN,
    TINYML_STATE_ERROR
} tinyml_state_t;

typedef struct
{
    float normal_score;
    float warning_score;
    float confidence;
    uint32_t timestamp_ms;
    tinyml_state_t state;
    uint8_t valid;
} tinyml_result_t;

void TinyML_App_Init(void);
void TinyML_App_Task(void);
const tinyml_result_t *TinyML_App_GetLatestResult(void);
const char *TinyML_App_StateString(tinyml_state_t state);
uint32_t TinyML_App_GetWarmupRemainingSeconds(void);
uint32_t TinyML_App_GetWindowSamplesCollected(void);

#endif /* TINYML_APP_H */
