#ifndef __INTERFACE_H__
#define __INTERFACE_H__
#include "common.h"
#include "mm.h"
#include "vfs.h"
#include "task.h"

/* scheduling */
int sc_wakeUp(struct wait_struct *waitqueue,struct task_struct * p);
int sc_wait(struct wait_struct *waitqueue,int ticks);
int sc_sleep(int ticks); /* each tick is 100HZ or 10ms */
int sc_fork(unsigned long clone_flags, unsigned long usp, int (*fn)(void *));
int sc_exit();
int sc_createThread(int (*fn)(void *));
void sc_schedule();

/* mm */
unsigned long mm_getFreePages(int gfp_mask, unsigned long order);
void mm_putFreePages(unsigned long addr, unsigned long order);

void kmem_cache_free (kmem_cache_t *cachep, void *objp);
extern kmem_cache_t *kmem_cache_create(const char *, long,long, unsigned long,void (*)(void *, kmem_cache_t *, unsigned long),void (*)(void *, kmem_cache_t *, unsigned long));
void *kmem_cache_alloc (kmem_cache_t *cachep, int flags);

/* vm */
void vm_printMmaps();
struct vm_area_struct *vm_findVma(struct mm_struct *mm,unsigned long addr, unsigned long len);
unsigned long vm_mmap(struct file *file, unsigned long addr, unsigned long len,
        unsigned long prot, unsigned long flags, unsigned long pgoff);
unsigned long vm_brk(unsigned long addr, unsigned long len);
int vm_munmap(struct mm_struct *mm, unsigned long addr, int len);

/* page cache */
int pc_init(unsigned char *start_addr,unsigned long len);
void pc_stats();
int pc_pageDirted(struct page *p);
int pc_pagecleaned(struct page *page);
struct page *pc_getInodePage(struct inode *inode,unsigned long offset);
int pc_insertInodePage(struct inode *inode,struct page *page);
int pc_putFreePage(struct page *page);
page_struct_t *pc_getFreePage();

/*vfs */
int fs_registerFileSystem(struct filesystem *fs);
struct inode *fs_getInode(char *filename);
int fs_printInodes();
struct file *fs_open(char *filename,int mode);
int fs_lseek(struct file *file ,unsigned long offset, int whence);
int fs_write(struct file *file ,unsigned char *buff ,unsigned long len);
int fs_read(struct file *file ,unsigned char *buff ,unsigned long len);
int fs_close(struct file *file);

/* Utilities */
int ut_strcmp(char *str1, char *str2);
void ut_printf (const char *format, ...);
void ut_memcpy(uint8_t *dest, uint8_t *src, long len);
void ut_memset(uint8_t *dest, uint8_t val, long len);
char *ut_strcpy(char *dest, const char *src);
char *ut_strcat(char *dest, const char *src);
int ut_strlen(const char * s);
unsigned long ut_atol(char *p);
unsigned int ut_atoi(char *p);

/* architecture */
void ar_registerInterrupt(uint8_t n, isr_t handler);
#endif