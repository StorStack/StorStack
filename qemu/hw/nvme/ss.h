#include "nvme.h"
// #include "crypto/hmac.h"

// extern QCryptoHmac *nvme_ss_hmac;

void     nvme_ss_init();

uint16_t nvme_ss_adm_part_init(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_adm_skey(NvmeCtrl *n, NvmeRequest *req);

// TODO: the following two cmd are for test, remove them
uint16_t nvme_ss_hellow(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_hellor(NvmeCtrl *n, NvmeRequest *req);

uint16_t nvme_ss_open(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_close(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_write(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_read(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_pwrite(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_pread(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_lseek(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_unlink(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_fsync(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_stat(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_mkdir(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_rmdir(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_lat_with_ret(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_lat_with_ret_r(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_lat_with_ret_w(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_lat_no_ret(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_ftruncate(NvmeCtrl *n, NvmeRequest *req);
uint16_t nvme_ss_fallocate(NvmeCtrl *n, NvmeRequest *req);

uint16_t nvme_ss_rename(NvmeCtrl *n, NvmeRequest *req); // TODO: need to be tested