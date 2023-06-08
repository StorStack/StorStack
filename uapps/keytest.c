/*
 * StorStack klib test
 * run this to generate a new secret key in klib
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
    __ss_init_klib();
    u8 seckey[SECKEY_LEN];
    ss_gen_seckey(seckey);
    __ss_fini_klib();
    return 0;
}