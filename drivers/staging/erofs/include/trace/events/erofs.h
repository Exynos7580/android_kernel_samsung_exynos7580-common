/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM erofs

#if !defined(_TRACE_EROFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EROFS_H

#include <linux/tracepoint.h>

#define show_dev(dev)		MAJOR(dev), MINOR(dev)
#define show_dev_ino(entry)	show_dev(entry->dev), (unsigned long)entry->ino

#define show_file_type(type)						\
	__print_symbolic(type,						\
		{ 0,		"FILE" },				\
		{ 1,		"DIR" })

TRACE_EVENT(erofs_lookup,

	TP_PROTO(struct inode *dir, struct dentry *dentry, unsigned int flags),

	TP_ARGS(dir, dentry, flags),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(const char *,	name)
		__field(unsigned int, flags)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->ino	= dir->i_ino;
		__entry->name	= dentry->d_name.name;
		__entry->flags	= flags;
	),

	TP_printk("dev = (%d,%d), pino = %lu, name:%s, flags:%u",
		show_dev_ino(__entry),
		__entry->name,
		__entry->flags)
);

TRACE_EVENT(erofs_readpage,

	TP_PROTO(struct page *page, bool raw),

	TP_ARGS(page, raw),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int, dir)
		__field(pgoff_t, index)
		__field(int, uptodate)
		__field(bool, raw)
	),

	TP_fast_assign(
		__entry->dev	= page->mapping->host->i_sb->s_dev;
		__entry->ino	= page->mapping->host->i_ino;
		__entry->dir	= S_ISDIR(page->mapping->host->i_mode);
		__entry->index	= page->index;
		__entry->uptodate = PageUptodate(page);
		__entry->raw = raw;
	),

	TP_printk("dev = (%d,%d), ino = %lu, %s, index = %lu, uptodate = %d "
		"raw = %d",
		show_dev_ino(__entry),
		show_file_type(__entry->dir),
		(unsigned long)__entry->index,
		__entry->uptodate,
		__entry->raw)
);

TRACE_EVENT(erofs_readpages,

	TP_PROTO(struct inode *inode, struct page *page, unsigned int nrpage,
		bool raw),

	TP_ARGS(inode, page, nrpage, raw),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(pgoff_t,	start)
		__field(unsigned int,	nrpage)
		__field(bool,		raw)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->start	= page->index;
		__entry->nrpage	= nrpage;
		__entry->raw	= raw;
	),

	TP_printk("dev = (%d,%d), ino = %lu, start = %lu nrpage = %u raw = %d",
		show_dev_ino(__entry),
		(unsigned long)__entry->start,
		__entry->nrpage,
		__entry->raw)
);
#endif /* _TRACE_EROFS_H */

 /* This part must be outside protection */
#include <trace/define_trace.h>
