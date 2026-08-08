idf_component_register(
    SRCS "main.c"
         "Net/base64.c"
         "Net/cJSON.c"
         "Net/hmac_sha1.c"
         "Net/MQTT.c"
         "Net/onenet.c"
         "APP/sx1268_radio.c"
         "APP/telemetry_protocol.c"
         "APP/lora_receiver.c"
         "APP/oled_display.c"
         "APP/scheduler.c"
    INCLUDE_DIRS "."
                 "Net"
                 "APP"
    REQUIRES esp_driver_spi
             esp_driver_gpio
             esp_driver_i2c
             esp_timer
             esp_wifi
             esp_event
             nvs_flash
             lwip)
