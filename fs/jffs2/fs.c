/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include <linux/delay.h>
#include "nodelist.h"
#include "os-linux.h"
#include "los_crc32.h"
#include "jffs2_hash.h"
#include "capability_type.h"
#include "capability_api.h"

int jffs2_setattr (struct jffs2_inode *inode, struct IATTR *attr)
{
	struct jffs2_full_dnode *old_metadata, *new_metadata;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_raw_inode *ri;
	unsigned int ivalid;
	mode_t tmp_mode;
	uint c_uid = OsCurrUserGet()->effUserID;
	uint c_gid = OsCurrUserGet()->effGid;
	uint32_t alloclen;
	int ret;
	int alloc_type = ALLOC_NORMAL;

	jffs2_dbg(1, "%s(): ino #%lu\n", __func__, inode->i_ino);
	ri = jffs2_alloc_raw_inode();
	if (!ri) {
		return -ENOMEM;
	}

	ret = jffs2_reserve_space(c, sizeof(*ri), &alloclen, ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_inode(ri);
		return ret;
	}
	mutex_lock(&f->sem);
	ivalid = attr->attr_chg_valid;
	tmp_mode = inode->i_mode;

	ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri->nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
	ri->totlen = cpu_to_je32(sizeof(*ri));
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, (sizeof(struct jffs2_unknown_node)-4)));

	ri->ino = cpu_to_je32(inode->i_ino);
	ri->version = cpu_to_je32(++f->highest_version);
	ri->uid = cpu_to_je16(inode->i_uid);
	ri->gid = cpu_to_je16(inode->i_gid);

	if (ivalid & CHG_UID) {
		if (((c_uid != inode->i_uid) || (attr->attr_chg_uid != inode->i_uid)) && (!IsCapPermit(CAP_CHOWN))) {
			jffs2_complete_reservation(c);
			jffs2_free_raw_inode(ri);
			mutex_unlock(&f->sem);
			return -EPERM;
		} else {
			ri->uid = cpu_to_je16(attr->attr_chg_uid);
		}
	}

	if (ivalid & CHG_GID) {
		if (((c_gid != inode->i_gid) || (attr->attr_chg_gid != inode->i_gid)) && (!IsCapPermit(CAP_CHOWN))) {
			jffs2_complete_reservation(c);
			jffs2_free_raw_inode(ri);
			mutex_unlock(&f->sem);
			return -EPERM;
		} else {
			ri->gid = cpu_to_je16(attr->attr_chg_gid);
		}
	}

	if (ivalid & CHG_MODE) {
		if (!IsCapPermit(CAP_FOWNER) && (c_uid != inode->i_uid)) {
			jffs2_complete_reservation(c);
			jffs2_free_raw_inode(ri);
			mutex_unlock(&f->sem);
			return -EPERM;
		} else {
			attr->attr_chg_mode  &= ~S_IFMT; // delete file type
			tmp_mode &= S_IFMT;
			tmp_mode = attr->attr_chg_mode | tmp_mode; // add old file type
		}
	}

	if (ivalid & CHG_ATIME) {
		if ((c_uid != inode->i_uid) || (attr->attr_chg_uid != inode->i_uid)) {
			return -EPERM;
		} else {
			ri->atime = cpu_to_je32(attr->attr_chg_atime);
		}
	} else {
		ri->atime = cpu_to_je32(inode->i_atime);
	}

	if (ivalid & CHG_MTIME) {
		if ((c_uid != inode->i_uid) || (attr->attr_chg_uid != inode->i_uid)) {
			return -EPERM;
		} else {
			ri->mtime = cpu_to_je32(attr->attr_chg_mtime);
		}
	} else {
		ri->mtime = cpu_to_je32(Jffs2CurSec());
	}
	ri->mode = cpu_to_jemode(tmp_mode);

	ri->isize = cpu_to_je32((ivalid & CHG_SIZE) ? attr->attr_chg_size : inode->i_size);
	ri->ctime = cpu_to_je32(Jffs2CurSec());

	ri->offset = cpu_to_je32(0);
	ri->csize = ri->dsize = cpu_to_je32(0);
	ri->compr = JFFS2_COMPR_NONE;
	if (ivalid & CHG_SIZE && inode->i_size < attr->attr_chg_size) {
		/* It's an extension. Make it a hole node */
		ri->compr = JFFS2_COMPR_ZERO;
		ri->dsize = cpu_to_je32(attr->attr_chg_size - inode->i_size);
		ri->offset = cpu_to_je32(inode->i_size);
	} else if (ivalid & CHG_SIZE && !attr->attr_chg_size) {
		/* For truncate-to-zero, treat it as deletion because
		   it'll always be obsoleting all previous nodes */
		alloc_type = ALLOC_DELETION;
	}
	ri->node_crc = cpu_to_je32(crc32(0, ri, (sizeof(*ri)-8)));
	ri->data_crc = cpu_to_je32(0);
	new_metadata = jffs2_write_dnode(c, f, ri, NULL, 0, alloc_type);
	if (IS_ERR(new_metadata)) {
		jffs2_complete_reservation(c);
		jffs2_free_raw_inode(ri);
		mutex_unlock(&f->sem);
		return PTR_ERR(new_metadata);
	}
	/* It worked. Update the inode */
	inode->i_atime = je32_to_cpu(ri->atime);
	inode->i_ctime = je32_to_cpu(ri->ctime);
	inode->i_mtime = je32_to_cpu(ri->mtime);
	inode->i_mode = jemode_to_cpu(ri->mode);
	inode->i_uid = je16_to_cpu(ri->uid);
	inode->i_gid = je16_to_cpu(ri->gid);

	old_metadata = f->metadata;
	if (ivalid & CHG_SIZE && inode->i_size > attr->attr_chg_size)
		jffs2_truncate_fragtree (c, &f->fragtree, attr->attr_chg_size);

	if (ivalid & CHG_SIZE && inode->i_size < attr->attr_chg_size) {
		jffs2_add_full_dnode_to_inode(c, f, new_metadata);
		inode->i_size = attr->attr_chg_size;
		f->metadata = NULL;
	} else {
		f->metadata = new_metadata;
	}
	if (old_metadata) {
		jffs2_mark_node_obsolete(c, old_metadata->raw);
		jffs2_free_full_dnode(old_metadata);
	}
	jffs2_free_raw_inode(ri);

	mutex_unlock(&f->sem);
	jffs2_complete_reservation(c);

	/* We have to do the truncate_setsize() without f->sem held, since
	   some pages may be locked and waiting for it in readpage().
	   We are protected from a simultaneous write() extending i_size
	   back past iattr->ia_size, because do_truncate() holds the
	   generic inode semaphore. */
	if (ivalid & CHG_SIZE && inode->i_size > attr->attr_chg_size) {
		inode->i_size = attr->attr_chg_size; // truncate_setsize
	}

	return 0;
}

static void jffs2_clear_inode (struct jffs2_inode *inode)
{
	/* We can forget about this inode for now - drop all
	 *  the nodelists associated with it, etc.
	 */
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);

	jffs2_do_clear_inode(c, f);
}

static struct jffs2_inode *ilookup(struct super_block *sb, uint32_t ino)
{
	struct jffs2_inode *node = NULL;

	if (sb->s_root == NULL) {
		return NULL;
	}

	// Check for this inode in the cache
	Jffs2NodeLock();
	(void)Jffs2HashGet(&sb->s_node_hash_lock, &sb->s_node_hash[0], sb, ino, &node);
	Jffs2NodeUnlock();
	return node;
}

struct jffs2_inode *new_inode(struct super_block *sb)
{
	struct jffs2_inode *inode = NULL;

	inode = zalloc(sizeof (struct jffs2_inode));
	if (inode == NULL)
		return 0;

	D2(PRINTK("malloc new_inode %x ####################################\n",
		inode));

	inode->i_sb = sb;
	inode->i_ino = 1;
	inode->i_nlink = 1;    // Let JFFS2 manage the link count
	inode->i_size = 0;
	LOS_ListInit((&(inode->i_hashlist)));

	return inode;
}

struct jffs2_inode *jffs2_iget(struct super_block *sb, uint32_t ino)
{
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_inode latest_node;
	struct jffs2_inode *inode;
	int ret;

	Jffs2NodeLock();
	inode = ilookup(sb, ino);
	if (inode) {
		Jffs2NodeUnlock();
		return inode;
	}
	inode = new_inode(sb);
	if (inode == NULL) {
		Jffs2NodeUnlock();
		return (struct jffs2_inode *)-ENOMEM;
	}

	inode->i_ino = ino;
	f = JFFS2_INODE_INFO(inode);
	c = JFFS2_SB_INFO(inode->i_sb);

	(void)mutex_init(&f->sem);
	(void)mutex_lock(&f->sem);

	ret = jffs2_do_read_inode(c, f, inode->i_ino, &latest_node);
	if (ret) {
		(void)mutex_unlock(&f->sem);
        inode->i_nlink = 0;
        free(inode);
		Jffs2NodeUnlock();
		return (struct jffs2_inode *)ret;
	}

	inode->i_mode = jemode_to_cpu(latest_node.mode);
	inode->i_uid = je16_to_cpu(latest_node.uid);
	inode->i_gid = je16_to_cpu(latest_node.gid);
	inode->i_size = je32_to_cpu(latest_node.isize);
	inode->i_atime = je32_to_cpu(latest_node.atime);
	inode->i_mtime = je32_to_cpu(latest_node.mtime);
	inode->i_ctime = je32_to_cpu(latest_node.ctime);
	inode->i_nlink = f->inocache->pino_nlink;

	(void)mutex_unlock(&f->sem);

	(void)Jffs2HashInsert(&sb->s_node_hash_lock, &sb->s_node_hash[0], inode, ino);

	jffs2_dbg(1, "jffs2_read_inode() returning\n");
	Jffs2NodeUnlock();

	return inode;
}


// -------------------------------------------------------------------------
// Decrement the reference count on an inode. If this makes the ref count
// zero, then this inode can be freed.

int jffs2_iput(struct jffs2_inode *i)
{
	// Called in jffs2_find
	// (and jffs2_open and jffs2_ops_mkdir?)
	// super.c jffs2_fill_super,
	// and gc.c jffs2_garbage_collect_pass
	struct jffs2_inode_info *f = NULL;

	Jffs2NodeLock();
	if (!i || i->i_nlink) {
		// and let it fault...
		Jffs2NodeUnlock();
		return -EBUSY;
	}

	jffs2_clear_inode(i);
	f = JFFS2_INODE_INFO(i);
	(void)mutex_destroy(&(f->sem));
	(void)Jffs2HashRemove(&i->i_sb->s_node_hash_lock, i);
	(void)memset_s(i, sizeof(*i), 0x5a, sizeof(*i));
	free(i);
	Jffs2NodeUnlock();

	return 0;
}


/* jffs2_new_inode: allocate a new inode and inocache, add it to the hash,
   fill in the raw_inode while you're at it. */
struct jffs2_inode *jffs2_new_inode (struct jffs2_inode *dir_i, int mode, struct jffs2_raw_inode *ri)
{
	struct jffs2_inode *inode;
	struct super_block *sb = dir_i->i_sb;
	struct jffs2_sb_info *c;
	struct jffs2_inode_info *f;
	int ret;

	c = JFFS2_SB_INFO(sb);

	Jffs2NodeLock();
	inode = new_inode(sb);

	if (!inode)
		return (struct jffs2_inode *)-ENOMEM;

	f = JFFS2_INODE_INFO(inode);
	(void)mutex_init(&f->sem);
	(void)mutex_lock(&f->sem);;

	memset(ri, 0, sizeof(*ri));
	/* Set OS-specific defaults for new inodes */
	ri->uid = cpu_to_je16(OsCurrUserGet()->effUserID);
	ri->gid = cpu_to_je16(OsCurrUserGet()->effGid);

	ret = jffs2_do_new_inode (c, f, mode, ri);
	if (ret) {
		mutex_unlock(&(f->sem));
		jffs2_clear_inode(inode);
		(void)mutex_destroy(&(f->sem));
		(void)memset_s(inode, sizeof(*inode), 0x6a, sizeof(*inode));
		free(inode);
		Jffs2NodeUnlock();
		return (struct jffs2_inode *)ret;

	}
	inode->i_nlink = 1;
	inode->i_ino = je32_to_cpu(ri->ino);
	inode->i_mode = jemode_to_cpu(ri->mode);
	inode->i_gid = je16_to_cpu(ri->gid);
	inode->i_uid = je16_to_cpu(ri->uid);
	inode->i_atime = inode->i_ctime = inode->i_mtime = Jffs2CurSec();
	ri->atime = ri->mtime = ri->ctime = cpu_to_je32(inode->i_mtime);

	inode->i_size = 0;

	(void)Jffs2HashInsert(&sb->s_node_hash_lock, &sb->s_node_hash[0], inode, inode->i_ino);
	Jffs2NodeUnlock();

	return inode;
}

int calculate_inocache_hashsize(uint32_t flash_size)
{
	/*
	 * Pick a inocache hash size based on the size of the medium.
	 * Count how many megabytes we're dealing with, apply a hashsize twice
	 * that size, but rounding down to the usual big powers of 2. And keep
	 * to sensible bounds.
	 */

	int size_mb = flash_size / 1024 / 1024;
	int hashsize = (size_mb * 2) & ~0x3f;

	if (hashsize < INOCACHE_HASHSIZE_MIN)
		return INOCACHE_HASHSIZE_MIN;
	if (hashsize > INOCACHE_HASHSIZE_MAX)
		return INOCACHE_HASHSIZE_MAX;

	return hashsize;
}

void jffs2_gc_release_inode(struct jffs2_sb_info *c,
		   	    struct jffs2_inode_info *f)
{
	struct jffs2_inode *node = OFNI_EDONI_2SFFJ(f);
	jffs2_iput(node);
}

struct jffs2_inode_info *jffs2_gc_fetch_inode(struct jffs2_sb_info *c,
					      int inum, int unlinked)
{
	struct jffs2_inode *inode;
	struct jffs2_inode_cache *ic;

	if (unlinked) {
		/* The inode has zero nlink but its nodes weren't yet marked
		   obsolete. This has to be because we're still waiting for
		   the final (close() and) iput() to happen.

		   There's a possibility that the final iput() could have
		   happened while we were contemplating. In order to ensure
		   that we don't cause a new read_inode() (which would fail)
		   for the inode in question, we use ilookup() in this case
		   instead of iget().

		   The nlink can't _become_ zero at this point because we're
		   holding the alloc_sem, and jffs2_do_unlink() would also
		   need that while decrementing nlink on any inode.
		*/
		inode = ilookup(OFNI_BS_2SFFJ(c), inum);
		if (!inode) {
			jffs2_dbg(1, "ilookup() failed for ino #%u; inode is probably deleted.\n",
				  inum);

			spin_lock(&c->inocache_lock);
			ic = jffs2_get_ino_cache(c, inum);
			if (!ic) {
				jffs2_dbg(1, "Inode cache for ino #%u is gone\n",
					  inum);
				spin_unlock(&c->inocache_lock);
				return NULL;
			}
			if (ic->state != INO_STATE_CHECKEDABSENT) {
				/* Wait for progress. Don't just loop */
				jffs2_dbg(1, "Waiting for ino #%u in state %d\n",
					  ic->ino, ic->state);
				sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			} else {
				spin_unlock(&c->inocache_lock);
			}

			return NULL;
		}
	} else {
		/* Inode has links to it still; they're not going away because
		   jffs2_do_unlink() would need the alloc_sem and we have it.
		   Just iget() it, and if read_inode() is necessary that's OK.
		*/
		inode = jffs2_iget(OFNI_BS_2SFFJ(c), inum);
		if (inode <= 0)
			return (struct jffs2_inode_info *)inode;
	}

	return JFFS2_INODE_INFO(inode);
}
