#include "hmac_sha1.h"
#include <string.h>

#define SHA1_BLOCK_BYTES    64
#define SHA1_DIGEST_BYTES   20
#define SHA1_MAX_INPUT      256                     // 最大输入长度（HMAC 需要 64+data_len）

/*****************************/
/**** Static helpers *********/
/*****************************/

static unsigned long int ft(int t, unsigned long int x, unsigned long int y, unsigned long int z)
{
    if (t < 20)       return (x & y) ^ ((~x) & z);
    else if (t < 40)  return x ^ y ^ z;
    else if (t < 60)  return (x & y) | (x & z) | (y & z);
    else              return x ^ y ^ z;
}

static unsigned long int sha1_k(int t)
{
    if (t < 20)       return 0x5a827999;
    else if (t < 40)  return 0x6ed9eba1;
    else if (t < 60)  return 0x8f1bbcdc;
    else              return 0xca62c1d6;
}

static unsigned long int sha1_rotl(int bits, unsigned long int a)
{
    return ((a << bits) | (a >> (32 - bits))) & 0xffffffff;
}

/*----------------------------------------------
 *  sha1_core() — works on a 64‑byte block
 *--------------------------------------------*/
static void sha1_core(const unsigned char *block, unsigned long int h[5])
{
    unsigned long int w[80];
    int t;
    for (t = 0; t < 16; t++) {
        w[t]  = ((unsigned long int)block[t * 4 + 0] << 24);
        w[t] |= ((unsigned long int)block[t * 4 + 1] << 16);
        w[t] |= ((unsigned long int)block[t * 4 + 2] << 8);
        w[t] |= ((unsigned long int)block[t * 4 + 3]);
    }
    for (t = 16; t < 80; t++)
        w[t] = sha1_rotl(1, w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16]);

    unsigned long int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (t = 0; t < 80; t++) {
        unsigned long int temp = sha1_rotl(5, a) + ft(t, b, c, d) + e + sha1_k(t) + w[t];
        e = d; d = c; c = sha1_rotl(30, b); b = a; a = temp & 0xffffffff;
    }
    h[0] = (h[0] + a) & 0xffffffff;
    h[1] = (h[1] + b) & 0xffffffff;
    h[2] = (h[2] + c) & 0xffffffff;
    h[3] = (h[3] + d) & 0xffffffff;
    h[4] = (h[4] + e) & 0xffffffff;
}

/*----------------------------------------------
 *  mqtt_sha1 — 内部拷贝输入，不破坏调用者数据
 *--------------------------------------------*/
void mqtt_sha1(unsigned char *message, int message_length, unsigned char *digest)
{
    unsigned char buf[SHA1_MAX_INPUT + 64];   // 320字节，足够padding + 3 blocks
    unsigned long int h[5];
    int i, padded_len, num_blocks;

    /* copy message into local buffer (truncate if too large) */
    if (message_length > SHA1_MAX_INPUT) message_length = SHA1_MAX_INPUT;
    memset(buf, 0, sizeof(buf));
    memcpy(buf, message, message_length);

    /* padding */
    padded_len = message_length + 1 + 8;                     // data + 0x80 + length field
    num_blocks = (padded_len + 63) / 64;
    padded_len = num_blocks * 64;

    buf[message_length] = 0x80;

    unsigned long int bits = (unsigned long int)message_length * 8;
    buf[padded_len - 1] = (unsigned char)(bits);
    buf[padded_len - 2] = (unsigned char)(bits >> 8);
    buf[padded_len - 3] = (unsigned char)(bits >> 16);
    buf[padded_len - 4] = (unsigned char)(bits >> 24);

    /* initial hash */
    h[0] = 0x67452301;
    h[1] = 0xefcdab89;
    h[2] = 0x98badcfe;
    h[3] = 0x10325476;
    h[4] = 0xc3d2e1f0;

    /* process blocks */
    for (i = 0; i < num_blocks; i++)
        sha1_core(buf + i * 64, h);

    /* digest to big-endian */
    for (i = 0; i < 5; i++) {
        digest[i * 4 + 0] = (unsigned char)(h[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)(h[i]);
    }
}

/******************************************************/
/* hmac-sha1()                                        */
/* Performs the hmac-sha1 keyed secure hash algorithm */
/******************************************************/

void mqtt_hmac_sha1(
    unsigned char *key,   int key_length,
    unsigned char *data,  int data_length,
    unsigned char *digest)
{
    #define SHA1_B 64
    unsigned char k0[SHA1_B];
    unsigned char inner_buf[MAX_MESSAGE_LENGTH + SHA1_B];
    unsigned char inner_hash[SHA1_DIGEST_BYTES];
    unsigned char outer_buf[SHA1_B + SHA1_DIGEST_BYTES];
    int i;

    memset(k0, 0, sizeof(k0));

    if (key_length > SHA1_B) {
        mqtt_sha1(key, key_length, k0);
    } else {
        memcpy(k0, key, key_length);
    }

    /* inner = SHA1( (k0 ^ ipad) || data ) */
    for (i = 0; i < SHA1_B; i++) inner_buf[i] = k0[i] ^ 0x36;
    memcpy(inner_buf + SHA1_B, data, data_length);
    mqtt_sha1(inner_buf, SHA1_B + data_length, inner_hash);

    /* outer = SHA1( (k0 ^ opad) || inner_hash ) */
    for (i = 0; i < SHA1_B; i++)  outer_buf[i] = k0[i] ^ 0x5c;
    memcpy(outer_buf + SHA1_B, inner_hash, SHA1_DIGEST_BYTES);
    mqtt_sha1(outer_buf, SHA1_B + SHA1_DIGEST_BYTES, digest);

    #undef SHA1_B
}
