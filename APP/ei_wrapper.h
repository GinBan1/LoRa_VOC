#ifndef EI_WRAPPER_H
#define EI_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

// 暴露给 main.c 调用的初始化与推理函数
void ei_model_init(void);
float ei_predict_30m_delta(float temp, float hum, float press, float sraw_voc);

#ifdef __cplusplus
}
#endif

#endif