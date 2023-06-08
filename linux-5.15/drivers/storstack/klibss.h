#ifndef KLIBSS_H
#define KLIBSS_H

#include <linux/ioctl.h>

typedef unsigned char u8;

#define STORSTACK_IOCTL_BASE 'S'

#define SSIOC_UPDATE_SECKEY     _IOR(STORSTACK_IOCTL_BASE, 0, u8*)  // update the secret key then return it
#define SSIOC_GENERATE_TOKEN    _IOR(STORSTACK_IOCTL_BASE, 1, u8*)  // generate HMAC token for a process

#define STORSTACK_NAME      "StorStack_klib"
#define STORSTACK_DEVNAME   "/dev/StorStack_klib"

#define SECKEY_LEN  256 >> 3
#define TOKEN_LEN   256 >> 3
#define CRYPTO_ALG  "hmac(sha256)"

typedef struct __attribute__ ((__packed__)) user_cred_s
{
    uid_t uid;
    gid_t gid;
} user_cred;

typedef union user_cred_u
{
    user_cred cred;
    u8 bytes[sizeof(user_cred)];
} user_cred_converter;

#endif