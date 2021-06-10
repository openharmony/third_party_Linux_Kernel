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

#ifndef __JFFS2_OS_LINUX_H__
#define __JFFS2_OS_LINUX_H__

#include <dirent.h>
#include "fs/fs.h"
#include "jffs2.h"
#include "jffs2_fs_sb.h"


/* jffs2 debug output opion */
#define CONFIG_JFFS2_FS_DEBUG       0  /* 1 or 2 */

/* jffs2 gc thread section */
#define JFFS2_GC_THREAD_PRIORITY  10 /* GC thread's priority */

/* zlib section*/
#define CONFIG_JFFS2_ZLIB
#define CONFIG_JFFS2_RTIME
#define CONFIG_JFFS2_RUBIN

/* JFFS2 uses Linux mode bits natively -- no need for conversion */
#define os_to_jffs2_mode(x) (x)
#define jffs2_to_os_mode(x) (x)

#ifndef BUG_ON
#define BUG_ON(x) do {if (unlikely(x)) BUG();} while (0)
#endif

struct kstatfs;
struct kvec;


#define JFFS2_INODE_INFO(i) (&(i)->jffs2_i)
#define OFNI_EDONI_2SFFJ(f)  \
        ((struct jffs2_inode *) (((char *)f) - ((char *)(&((struct jffs2_inode *)NULL)->jffs2_i))))

#define JFFS2_F_I_SIZE(f) (OFNI_EDONI_2SFFJ(f)->i_size)
#define JFFS2_F_I_MODE(f) (OFNI_EDONI_2SFFJ(f)->i_mode)
#define JFFS2_F_I_UID(f) (OFNI_EDONI_2SFFJ(f)->i_uid)
#define JFFS2_F_I_GID(f) (OFNI_EDONI_2SFFJ(f)->i_gid)
#define JFFS2_F_I_CTIME(f) (OFNI_EDONI_2SFFJ(f)->i_ctime)
#define JFFS2_F_I_MTIME(f) (OFNI_EDONI_2SFFJ(f)->i_mtime)
#define JFFS2_F_I_ATIME(f) (OFNI_EDONI_2SFFJ(f)->i_atime)

#define ITIME(sec) ((struct timespec){sec, 0})
#define I_SEC(tv) ((tv).tv_sec)

#define sleep_on_spinunlock(wq, sl) do {spin_unlock(sl); msleep(100);} while (0)

#define jffs2_is_readonly(c) (0)

#define SECTOR_ADDR(x) ( (((unsigned long)(x) / c->sector_size) * c->sector_size) )

#ifndef CONFIG_JFFS2_FS_WRITEBUFFER

#ifdef CONFIG_JFFS2_SUMMARY
#define jffs2_can_mark_obsolete(c) (0)
#else
#define jffs2_can_mark_obsolete(c) (1)
#endif

#define jffs2_is_writebuffered(c) (0)
#define jffs2_cleanmarker_oob(c) (0)
#define jffs2_write_nand_cleanmarker(c,jeb) (-EIO)

#define jffs2_flash_write(c, ofs, len, retlen, buf) jffs2_flash_direct_write(c, ofs, len, retlen, buf)
#define jffs2_flash_read(c, ofs, len, retlen, buf) jffs2_flash_direct_read(c, ofs, len, retlen, buf)
#define jffs2_flush_wbuf_pad(c) (c=c)
#define jffs2_flush_wbuf_gc(c, i) ({ do{} while(0); (void)(c), (void) i, 0; })
#define jffs2_write_nand_badblock(c,jeb,p) (0)
#define jffs2_nand_flash_setup(c) (0)
#define jffs2_nand_flash_cleanup(c) do {} while(0)
#define jffs2_wbuf_dirty(c) (0)
#define jffs2_flash_writev(a,b,c,d,e,f) jffs2_flash_direct_writev(a,b,c,d,e)
#define jffs2_wbuf_timeout NULL
#define jffs2_wbuf_process NULL
#define jffs2_dataflash(c) (0)
#define jffs2_dataflash_setup(c) (0)
#define jffs2_dataflash_cleanup(c) do {} while (0)
#define jffs2_nor_wbuf_flash(c) (0)
#define jffs2_nor_wbuf_flash_setup(c) (0)
#define jffs2_nor_wbuf_flash_cleanup(c) do {} while (0)
#define jffs2_ubivol(c) (0)
#define jffs2_ubivol_setup(c) (0)
#define jffs2_ubivol_cleanup(c) do {} while (0)
#define jffs2_dirty_trigger(c) do {} while (0)

#else /* NAND and/or ECC'd NOR support present */

/* current not support */
#define jffs2_is_writebuffered(c) (0)

#ifdef CONFIG_JFFS2_SUMMARY
#define jffs2_can_mark_obsolete(c) (0)
#else
#define jffs2_can_mark_obsolete(c) (c->mtd->flags & (MTD_BIT_WRITEABLE))
#endif

#define jffs2_cleanmarker_oob(c) (c->mtd->type == MTD_NANDFLASH)

#define jffs2_wbuf_dirty(c) (!!(c)->wbuf_len)

/* wbuf.c */
int jffs2_flash_writev(struct jffs2_sb_info *c, const struct kvec *vecs, unsigned long count, loff_t to, size_t *retlen, uint32_t ino);
int jffs2_flash_write(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, const u_char *buf);
int jffs2_flash_read(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, u_char *buf);
int jffs2_check_oob_empty(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,int mode);
int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_badblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset);
void jffs2_wbuf_timeout(unsigned long data);
void jffs2_wbuf_process(void *data);
int jffs2_flush_wbuf_gc(struct jffs2_sb_info *c, uint32_t ino);
int jffs2_flush_wbuf_pad(struct jffs2_sb_info *c);
int jffs2_nand_flash_setup(struct jffs2_sb_info *c);
void jffs2_nand_flash_cleanup(struct jffs2_sb_info *c);

#define jffs2_dataflash(c) (c->mtd->type == MTD_DATAFLASH)
int jffs2_dataflash_setup(struct jffs2_sb_info *c);
void jffs2_dataflash_cleanup(struct jffs2_sb_info *c);
#define jffs2_ubivol(c) (c->mtd->type == MTD_UBIVOLUME)
int jffs2_ubivol_setup(struct jffs2_sb_info *c);
void jffs2_ubivol_cleanup(struct jffs2_sb_info *c);

#define jffs2_nor_wbuf_flash(c) (c->mtd->type == MTD_NORFLASH && ! (c->mtd->flags & MTD_BIT_WRITEABLE))
int jffs2_nor_wbuf_flash_setup(struct jffs2_sb_info *c);
void jffs2_nor_wbuf_flash_cleanup(struct jffs2_sb_info *c);
void jffs2_dirty_trigger(struct jffs2_sb_info *c);

#endif /* WRITEBUFFER */

/* background.c */
void jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c);
void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c);
void jffs2_garbage_collect_trigger(struct jffs2_sb_info *c);

/* dir.c */
struct jffs2_inode *jffs2_lookup(struct jffs2_inode *dir_i, const unsigned char *name, int namelen);
int jffs2_create(struct jffs2_inode *dir_i, const unsigned char *d_name, int mode, struct jffs2_inode **new_i);
int jffs2_mkdir (struct jffs2_inode *dir_i, const unsigned char *d_name, int mode, struct jffs2_inode **new_i);
int jffs2_link (struct jffs2_inode *old_d_inode, struct jffs2_inode *dir_i, const unsigned char *d_name);
int jffs2_symlink(struct jffs2_inode *dir_i, struct jffs2_inode **d_inode, const unsigned char *d_name, const char *target);
int jffs2_unlink(struct jffs2_inode *dir_i, struct jffs2_inode *d_inode, const unsigned char *d_name);
int jffs2_rmdir (struct jffs2_inode *dir_i, struct jffs2_inode *d_inode, const unsigned char *d_name);
int jffs2_rename (struct jffs2_inode *old_dir_i, struct jffs2_inode *d_inode, const unsigned char *old_d_name,
		  struct jffs2_inode *new_dir_i, const unsigned char *new_d_name);
int jffs2_readdir(struct jffs2_inode *inode, off_t *offset, off_t *int_off, struct dirent *ent);

/* fs.c */
int jffs2_setattr (struct jffs2_inode *inode, struct IATTR *attr);
struct jffs2_inode *jffs2_iget(struct super_block *sb, uint32_t ino);
int jffs2_iput(struct jffs2_inode * i);
struct jffs2_inode *jffs2_new_inode (struct jffs2_inode *dir_i, int mode, struct jffs2_raw_inode *ri);

void jffs2_gc_release_inode(struct jffs2_sb_info *c,
			    struct jffs2_inode_info *f);
struct jffs2_inode_info *jffs2_gc_fetch_inode(struct jffs2_sb_info *c,
					      int inum, int unlinked);

unsigned char *jffs2_gc_fetch_page(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f,
				   unsigned long offset,
				   unsigned long *priv);
void jffs2_gc_release_page(struct jffs2_sb_info *c,
			   unsigned char *pg,
			   unsigned long *priv);
void jffs2_flash_cleanup(struct jffs2_sb_info *c);

int calculate_inocache_hashsize(uint32_t flash_size);

/* writev.c */
int jffs2_flash_direct_writev(struct jffs2_sb_info *c, const struct kvec *vecs,
		       unsigned long count, loff_t to, size_t *retlen);
int jffs2_flash_direct_write(struct jffs2_sb_info *c, loff_t ofs, size_t len,
			size_t *retlen, const u_char *buf);
int jffs2_flash_direct_read(struct jffs2_sb_info *c, loff_t ofs, size_t len,
			size_t *retlen, const char *buf);

/* super.c */
int jffs2_fill_super(struct super_block *sb);
int jffs2_mount(int part_no, struct jffs2_inode **root_node);
int jffs2_umount(struct jffs2_inode *root_node);

#endif /* __JFFS2_OS_LINUX_H__ */

