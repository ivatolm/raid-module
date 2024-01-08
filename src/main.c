#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#define KERNEL_SECTOR_SIZE 512

static struct rm_data {
	struct gendisk *gd;

	size_t stripe_size;

	size_t disks_cnt;
	struct block_device *disks[2];
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
	rm_submit_raid_level0(bio);
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

	// Capture from sysfs
	rm.disks[0] = blkdev_get_by_path("/dev/sdb", FMODE_READ | FMODE_WRITE, NULL, NULL);
	rm.disks[1] = blkdev_get_by_path("/dev/sdc", FMODE_READ | FMODE_WRITE, NULL, NULL);
	rm.disks_cnt = 2;

	rm.stripe_size = 16 * 1024 / 512;

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
