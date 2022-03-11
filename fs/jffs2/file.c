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
#include "los_vm_common.h"

#include "nodelist.h"
#include "vfs_jffs2.h"


static unsigned char gc_buffer[PAGE_SIZE];	//avoids malloc when user may be under memory pressure


unsigned char *jffs2_gc_fetch_page(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f,
				   unsigned long offset,
				   unsigned long *priv)
{
	/* FIXME: This works only with one file system mounted at a time */
	int ret;

	ret = jffs2_read_inode_range(c, f, gc_buffer,
			 offset & ~(PAGE_SIZE-1), PAGE_SIZE);
	if (ret)
		return ERR_PTR(ret);

	return gc_buffer;
}
void jffs2_gc_release_page(struct jffs2_sb_info *c,
			   unsigned char *ptr,
			   unsigned long *priv)
{
	/* Do nothing */
}

