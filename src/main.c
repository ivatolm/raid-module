#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#define KERNEL_SECTOR_SIZE 512

static int dev_cnt;
module_param(dev_cnt, int, 0);

static char *dev_names[16];
module_param_array(dev_names, charp, NULL, 0);

static int level;
module_param(level, int, 0);

static struct rm_data {
	struct gendisk *gd;

	size_t stripe_size;
	size_t level; // TODO: Replace with enum

	size_t disks_cnt;
	struct block_device *disks[16];
} rm;

static void rm_submit_raid_level0(struct bio *bio)
{
	sector_t chunk_size = rm.stripe_size / rm.disks_cnt;

	sector_t sector = bio->bi_iter.bi_sector;
	sector_t size = bio->bi_iter.bi_size / KERNEL_SECTOR_SIZE;

	while (size > 0) {
		size_t disk_index = (sector / chunk_size) % rm.disks_cnt;
		size_t clone_size = min(size, chunk_size);

		struct bio *clone;
		if (clone_size < bio_sectors(bio)) {
			clone = bio_split(bio, clone_size, GFP_KERNEL, bio->bi_pool);
			bio_chain(clone, bio);
		} else {
			clone = bio;
		}

		bio_set_dev(clone, rm.disks[disk_index]);
		submit_bio(clone);

		sector += clone_size;
		size -= clone_size;
	}
}

static void rm_submit_raid_level1(struct bio *bio)
{
	// Assuming there is at least 1 disk
	if (bio_data_dir(bio) == READ)
		goto submit_parent;

	// Starting from the second disk, because the first one will be the parent
	for (size_t i = 1; i < rm.disks_cnt; i++) {
		struct bio *clone = bio_alloc_clone(rm.disks[i], bio, GFP_KERNEL, bio->bi_pool);
		bio_chain(clone, bio);
		submit_bio(clone);
	}

submit_parent:
	bio_set_dev(bio, rm.disks[0]);
	submit_bio(bio);
}

static void rm_submit_bio(struct bio *bio)
{
	sector_t sector = bio->bi_iter.bi_sector;
	sector_t size = bio->bi_iter.bi_size;
	
	pr_info("sector: %llu, size: %llu\n", sector, size / KERNEL_SECTOR_SIZE);

	// Processing bio request with the RAID level 1 handler
	switch (rm.level) {
		case 0:
			rm_submit_raid_level0(bio);
			break;
		case 1:
			rm_submit_raid_level1(bio);
			break;
	}
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

	rm.level = level;
	if (level != 0 && level != 1) {
		printk(KERN_NOTICE "invalid raid level\n");
		return -1;
	}

	rm.disks_cnt = dev_cnt;
	for (size_t i = 0; i < dev_cnt; i++) {
		rm.disks[i] = blkdev_get_by_path(dev_names[i],
				FMODE_READ | FMODE_WRITE, NULL, NULL);
	}

	rm.stripe_size = 16384 / KERNEL_SECTOR_SIZE;

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
