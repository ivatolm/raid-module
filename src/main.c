#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Tolmachev");
MODULE_DESCRIPTION("RAID module");

static int __init init(void)
{
    printk(KERN_INFO "init\n");
    return 0;
}

static void __exit exit(void)
{
    printk(KERN_INFO "exit\n");
}

module_init(init);
module_exit(exit);
