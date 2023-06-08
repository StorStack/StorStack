#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/cred.h>
#include <linux/crypto.h>
#include <linux/random.h>
#include <crypto/hash.h>
#include "klibss.h"

#ifdef DEBUG
#define SS_DEBUG
#endif

static dev_t dev = 0;
static struct class *dev_class;
static struct cdev ss_cdev;

static DEFINE_RWLOCK(seckey_rwlock);
static u8 secret_key[SECKEY_LEN];
static struct crypto_shash *shash_tfm;


static int ss_update_seckey(void) 
{
    int ret;
#ifdef SS_DEBUG
    int i;
#endif

    get_random_bytes(secret_key, sizeof(secret_key));
    if ((ret=crypto_shash_setkey(shash_tfm, secret_key, SECKEY_LEN)) < 0)
        return ret;

#ifdef SS_DEBUG
    pr_debug("StorStack new secret: ");
    for (i = 0; i < SECKEY_LEN; i++)
    { 
        pr_cont("%02x ", secret_key[i]);
    }
    pr_cont("\n"); 
#endif
    
    return 0;
}

static inline int ss_checkperm_update_seckey(void)
{
    // fail if the owner of the process is not root
    if (current_uid().val != 0)
        return -EACCES; 
    return ss_update_seckey();
}

static int ss_generate_token(u8 *token)
{
    int ret = 0;
#ifdef SS_DEBUG
    int i;
#endif
    user_cred cred = {
        .uid = current_uid().val,
        .gid = 0,
        // TODO: gid is not checked in qemu for now, support gid in the future
        // .gid = current_gid().val,
    };
    user_cred_converter cvt;
    struct shash_desc *desc;

    desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(shash_tfm), GFP_KERNEL);
	if (!desc) {
        ret = -ENOMEM;
		goto err;
    }
    desc->tfm = shash_tfm;

    // if ((ret=crypto_shash_setkey(shash_tfm, secret_key, SECKEY_LEN)) < 0)
    //     goto shash_err;
    if ((ret=crypto_shash_init(desc)) < 0)
        goto shash_err;
    cvt.cred = cred;
    if ((ret=crypto_shash_update(desc, cvt.bytes, sizeof(user_cred))) < 0)
        goto shash_err;
    if ((ret=crypto_shash_final(desc, token)) < 0)
        goto shash_err;

#ifdef SS_DEBUG
    pr_debug("StorStack token for u%d g%d: ", cred.uid, cred.gid);
    for (i = 0; i < TOKEN_LEN; i++)
    {
        pr_cont("%02x ", token[i]);
    }
    pr_cont("\n"); 
    pr_debug("cred %ld bytes: ", sizeof(cvt.bytes));
    for (i = 0; i < sizeof(cvt.bytes); i++)
    {
        pr_cont("%02x ", cvt.bytes[i]);
    }
    pr_cont("\n");
    pr_debug("size of uid: %ld cred: %ld\n", sizeof(cred.uid), sizeof(cred));
#endif
    
    kfree(desc);
    return 0;

shash_err:
    kfree(desc);
err:
    return ret;
}

static int klibss_open(struct inode *inode, struct file *file)
{
    return 0;
}

static long klibss_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    char token[TOKEN_LEN];

    switch (cmd)
    {
    case SSIOC_UPDATE_SECKEY:
        write_lock(&seckey_rwlock);
        ret = ss_checkperm_update_seckey();
        if (copy_to_user((u8 *)arg, secret_key, SECKEY_LEN))
            ret = -EFAULT;
        write_unlock(&seckey_rwlock);
        break;
    case SSIOC_GENERATE_TOKEN:
        read_lock(&seckey_rwlock);
        ret = ss_generate_token(token);
        if (copy_to_user((u8 *)arg, token, TOKEN_LEN))
            ret = -EFAULT;
        read_unlock(&seckey_rwlock);
        break;
    default:
        ret = -EPERM;
        break;
    }

    return ret;
}

struct file_operations fops = {
    .owner          = THIS_MODULE, 
    .open           = klibss_open,
    .unlocked_ioctl = klibss_ioctl
};

static int mode_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", S_IRUGO | S_IWUGO);
    return 0;
}

static int __init klibss_init(void)
{
    if ((alloc_chrdev_region(&dev, 0, 1, STORSTACK_NAME)) <0) {
        pr_err("Cannot alloc device number for StorStack klib.\n");
        goto err;
    }

    cdev_init(&ss_cdev, &fops);
    if ((cdev_add(&ss_cdev, dev, 1)) < 0) {
        pr_err("Cannot add StorStack klib device to the system.\n");
        goto class_err;
    }

    dev_class = class_create(THIS_MODULE, STORSTACK_NAME);
    if (IS_ERR(dev_class)) {
        pr_err("Cannot create the struct class for StorStack klib.\n");
        goto class_err;
    }
    dev_class->dev_uevent = mode_uevent;

    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, STORSTACK_NAME))) {
        pr_err("Cannot create the device for StorStack klib.\n");
        goto device_err;
    }

    shash_tfm = crypto_alloc_shash(CRYPTO_ALG, 0, 0);
    if (IS_ERR(shash_tfm)) {
        pr_err("Cannot alloc shash for StorStack klib.\n");
        goto tfm_err;
    }

    pr_info("StorStack k-lib driver initialized successfully.\n");
    return 0;

tfm_err:
    device_destroy(dev_class, dev);
device_err:
    class_destroy(dev_class);
class_err:
    unregister_chrdev_region(dev,1);
err:
    return -1;
}

static void __exit klibss_exit(void)
{
    crypto_free_shash(shash_tfm);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&ss_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info("StorStack k-lib driver exited.\n");
}

MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(klibss_init);
module_exit(klibss_exit);
