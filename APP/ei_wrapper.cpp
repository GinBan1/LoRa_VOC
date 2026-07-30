#include "ei_wrapper.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// 准备推理输入缓存区
static float features[4]; // 对应 4 个输入特征：temperature, humidity, pressure, sraw_voc

static int get_feature_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = features[offset + i];
    }
    return 0;
}

extern "C" void ei_model_init(void) {
    // 可以在这里做必要的初始化
}

extern "C" float ei_predict_30m_delta(float temp, float hum, float press, float sraw_voc) {
    features[0] = temp;
    features[1] = hum;
    features[2] = press;
    features[3] = sraw_voc;

    signal_t signal;
    signal.total_length = 4;
    signal.get_data = &get_feature_data;

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        return -99999.0f; // 推理失败标志
    }

    // 回归预测的输出结果存放在 result.value 中
    return result.classification[0].value;
}