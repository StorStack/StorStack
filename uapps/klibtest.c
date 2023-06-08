/*
 * StorStack klib test
 * overall test for klib, this will generate a key in klib, 
 * then get a token from klib and check it inside userspace
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ulibss.h"
#include "klibss.h"
#include "hmac/hmac_sha2.h"

#include "spdk/nvme_spec.h"

int gen_token(u8 *key, u8* token)
{
    user_cred cred = {
        .uid = getuid(), 
        .gid = getgid(),
    };
    user_cred_converter cvt;
    cvt.cred = cred;
    int i;
    printf("cred %ld bytes: ", sizeof(cvt.bytes));
    for (i = 0; i < sizeof(cvt.bytes); i++)
    {
        printf("%02x ", cvt.bytes[i]);
    }
    printf("\n"); 
    printf("uid %d gid %d \n", cred.uid, cred.gid);
    hmac_sha256(key, SECKEY_LEN, cvt.bytes, sizeof(cvt.bytes), token, TOKEN_LEN);
    printf("Token in userspace: ");

    for (i = 0; i < TOKEN_LEN; i++)
    {
        printf("%02x ", token[i]);
    }
    printf("\n"); 
}

int main() 
{
    // printf("%d\n", ss_open(0, 0, 0));
    // printf("%d\n", ss_pwrite(0, 0, 0, 0));
    // int tmp = -123;
    // size_t tmp1 = tmp;
    // printf("%lu\n", tmp1);
    __ss_init_klib();
    u8 seckey[SECKEY_LEN];
    u8 k_token[TOKEN_LEN];
    u8 u_token[TOKEN_LEN];
    // from kernel
    ss_gen_seckey(seckey);
    ss_get_token(k_token);
    printf("StorStack secret: ");
    int i;
    for (i = 0; i < SECKEY_LEN; i++)
    {
        printf("%02x ", seckey[i]);
    }
    printf("\n"); 
    printf("StorStack token: ");
    for (i = 0; i < TOKEN_LEN; i++)
    {
        printf("%02x ", k_token[i]);
    }
    printf("\n"); 
    printf("\n"); 

    // from userspace
    gen_token(seckey, u_token);
    int f = 0;
    for (i = 0; i < TOKEN_LEN; i++)
    {
        if (k_token[i] != u_token[i]) f++;
    }
    if (f) printf("token not same\n");
    else printf("same token, check passed\n");
    __ss_fini_klib();
    return 0;
}