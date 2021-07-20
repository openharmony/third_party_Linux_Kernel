/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */


#include "jffs2.h"
#include "nodelist.h"
#include "jffs2_fs_sb.h"
#include "mtd_dev.h"
#include "mtd_partition.h"
#include "compr.h"
#include "jffs2_hash.h"

static unsigned char jffs2_mounted_number = 0; /* a counter to track the number of jffs2 instances mounted */
struct MtdNorDev jffs2_dev_list[CONFIG_MTD_PATTITION_NUM];

/*
 * fill in the superblock
 */
int jffs2_fill_super(struct super_block *sb)
{
	int ret;
	struct jffs2_sb_info *c;
	struct MtdNorDev *device;

	c = JFFS2_SB_INFO(sb);
	device = (struct MtdNorDev*)(sb->s_dev);

	(void)mutex_init(&c->alloc_sem);
	(void)mutex_init(&c->erase_free_sem);
	spin_lock_init(&c->erase_completion_lock);
	spin_lock_init(&c->inocache_lock);

	/* sector size is the erase block size */
	c->sector_size = device->blockSize;
	c->flash_size  = (device->blockEnd - device->blockStart + 1) * device->blockSize;
	c->cleanmarker_size = sizeof(struct jffs2_unknown_node);

	ret = jffs2_do_mount_fs(c);
	if (ret) {
		(void)mutex_destroy(&c->alloc_sem);
		(void)mutex_destroy(&c->erase_free_sem);
		return ret;
	}
	D1(printk(KERN_DEBUG "jffs2_fill_super(): Getting root inode\n"));

	sb->s_root = jffs2_iget(sb, 1);

	if (IS_ERR(sb->s_root)) {
		D1(printk(KERN_WARNING "get root inode failed\n"));
		ret = PTR_ERR(sb->s_root);
		sb->s_root = NULL;
		jffs2_free_ino_caches(c);
		jffs2_free_raw_node_refs(c);
		free(c->blocks);
		(void)mutex_destroy(&c->alloc_sem);
		(void)mutex_destroy(&c->erase_free_sem);

		return ret;
	}
	return 0;
}

int jffs2_mount(int part_no, struct jffs2_inode **root_node, unsigned long mountflags)
{
	struct super_block *sb = NULL;
	struct jffs2_sb_info *c = NULL;
	LOS_DL_LIST *part_head = NULL;
	struct MtdDev *spinor_mtd = NULL;
	mtd_partition *mtd_part = GetSpinorPartitionHead();
	int ret;

	jffs2_dbg(1, "begin los_jffs2_mount:%d\n", part_no);

	sb = zalloc(sizeof(struct super_block));
	if (sb == NULL) {
		return -ENOMEM;
	}

	ret = Jffs2HashInit(&sb->s_node_hash_lock, &sb->s_node_hash[0]);
	if (ret) {
		free(sb);
		return ret;
	}
	part_head = &(GetSpinorPartitionHead()->node_info);
	LOS_DL_LIST_FOR_EACH_ENTRY(mtd_part,part_head, mtd_partition, node_info) {
		if (mtd_part->patitionnum == part_no)
			break;
	}
#ifndef LOSCFG_PLATFORM_QEMU_ARM_VIRT_CA7
	spinor_mtd = GetMtd("spinor");
#else
	spinor_mtd = (struct MtdDev *)LOS_DL_LIST_ENTRY(part_head->pstNext, mtd_partition, node_info)->mtd_info;
#endif
	if (spinor_mtd == NULL) {
		free(sb);
		return -EPERM;
	}
	jffs2_dev_list[part_no].blockEnd = mtd_part->end_block;
	jffs2_dev_list[part_no].blockSize = spinor_mtd->eraseSize;
	jffs2_dev_list[part_no].blockStart = mtd_part->start_block;
#ifndef LOSCFG_PLATFORM_QEMU_ARM_VIRT_CA7
	(void)FreeMtd(spinor_mtd);
#endif
	sb->jffs2_sb.mtd = mtd_part->mtd_info;
	sb->s_dev = &jffs2_dev_list[part_no];

	c = JFFS2_SB_INFO(sb);
	c->flash_size  = (mtd_part->end_block - mtd_part->start_block + 1) * spinor_mtd->eraseSize;
	c->inocache_hashsize = calculate_inocache_hashsize(c->flash_size);
	c->sector_size = spinor_mtd->eraseSize;

	jffs2_dbg(1, "C mtd_size:%d,mtd-erase:%d,blocks:%d,hashsize:%d\n",
		c->flash_size, c->sector_size, c->flash_size / c->sector_size, c->inocache_hashsize);

	c->inocache_list = zalloc(sizeof(struct jffs2_inode_cache *) * c->inocache_hashsize);
	if (c->inocache_list == NULL) {
		free(sb);
		return -ENOMEM;
	}
	if (jffs2_mounted_number++ == 0) {
		(void)jffs2_create_slab_caches(); // No error check, cannot fail
		(void)jffs2_compressors_init();
	}

	ret = jffs2_fill_super(sb);
	if (ret) {
		if (--jffs2_mounted_number == 0) {
			jffs2_destroy_slab_caches();
			(void)jffs2_compressors_exit();
		}

		free(sb);
		free(c->inocache_list);
		c->inocache_list = NULL;
		return ret;
	}

	if (!(mountflags & MS_RDONLY)) {
		jffs2_start_garbage_collect_thread(c);
	}

	sb->s_mount_flags = mountflags;
	*root_node = sb->s_root;
	return 0;
}

int jffs2_umount(struct jffs2_inode *root_node)
{
	struct super_block *sb = root_node->i_sb;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);
	struct jffs2_full_dirent *fd, *next;

	D2(PRINTK("Jffs2Umount\n"));

	// Only really umount if this is the only mount
	if (!(sb->s_mount_flags & MS_RDONLY)) {
		jffs2_stop_garbage_collect_thread(c);
	}

	// free directory entries
	for (fd = root_node->jffs2_i.dents; fd; fd = next) {
		next = fd->next;
		jffs2_free_full_dirent(fd);
	}

	free(root_node);

	// Clean up the super block and root_node inode
	jffs2_free_ino_caches(c);
	jffs2_free_raw_node_refs(c);
	free(c->blocks);
	c->blocks = NULL;
	free(c->inocache_list);
	c->inocache_list = NULL;
	(void)Jffs2HashDeinit(&sb->s_node_hash_lock);

	(void)mutex_destroy(&c->alloc_sem);
	(void)mutex_destroy(&c->erase_free_sem);
	free(sb);
	// That's all folks.
	D2(PRINTK("Jffs2Umount No current mounts\n"));

	if (--jffs2_mounted_number == 0) {
		jffs2_destroy_slab_caches();
		(void)jffs2_compressors_exit();
	}
	return 0;
}
