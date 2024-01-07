#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#define KERNEL_SECTOR_SIZE 512

static struct rm_data {
	struct gendisk *gd;
} rm;

static void rm_submit_bio(struct bio *bio)
{
	sector_t sector, size;	
	
	sector = bio->bi_iter.bi_sector;
	size = bio->bi_iter.bi_size;

	pr_info("sector: %llu, size: %llu\n", sector, size);

	bio_endio(bio);
}

struct block_device_operations rm_ops = {
	.owner = THIS_MODULE,
	.submit_bio = rm_submit_bio,
};

static int rm_create_device(void)
{
	rm.gd = blk_alloc_disk(1);
	if (!rm.gd) {
		printk(KERN_NOTICE "unable to allocate disk\n");
		return -ENOMEM;
	}
	
	rm.gd->major = 0;
	rm.gd->first_minor = 0;
	rm.gd->fops = &rm_ops;
	snprintf(rm.gd->disk_name, 32, "rm_raid");
	set_capacity(rm.gd, 1024);

	int status = add_disk(rm.gd);

	return status;
}

static void rm_delete_device(void)
{
	if (rm.gd) {
		del_gendisk(rm.gd);
		put_disk(rm.gd);
	}
}

static int rm_create(void)
{
	int status;

	status = rm_create_device();
	if (status) {
		return status;
	}

	return 0;
}

static void rm_delete(void)
{
	rm_delete_device();
}

static int __init rm_init(void)
{
	int status;

	pr_info("Raid module initialization...\n");
	status = rm_create();
	if (status) {
		pr_warn("Raid module initialization failed\n");
		rm_delete();
	} else {
		pr_info("Raid module initialization completed\n");
	}

   	return status;
}

static void __exit rm_exit(void)
{
	pr_info("Raid module exiting...\n");
	rm_delete();
}

module_init(rm_init);
module_exit(rm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Tolmachev");
MODULE_DESCRIPTION("RAID module");
