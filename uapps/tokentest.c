/*
 * StorStack klib test
 * run this to get a new token from klib
 */
#include <stdio.h>
#include "ulibss.h"
#include "klibss.h"

#include "spdk/nvme_spec.h"

int main() 
{
    // printf("%d\n", ss_open(0, 0, 0));
    // printf("%d\n", ss_pwrite(0, 0, 0, 0));
    // int tmp = -123;
    // size_t tmp1 = tmp;
    // printf("%lu\n", tmp1);
    ss_init_klib();
    u8 token[TOKEN_LEN];
    ss_get_token(token);
    ss_fini_klib();
    return 0;
}