#ifndef __HMAC_SHA1_H__
#define __HMAC_SHA1_H__

#include "net_common.h"

#define MAX_MESSAGE_LENGTH      1024

void mqtt_hmac_sha1(
    unsigned char *key,
    int key_length,
    unsigned char *data,
    int data_length,
    unsigned char *digest
);
void mqtt_sha1(
    unsigned char *message,
    int message_length,
    unsigned char *digest
);

#endif
