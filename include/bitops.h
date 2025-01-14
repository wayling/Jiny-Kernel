#ifndef _X86_64_BITOPS_H
#define _X86_64_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */


/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock ; "
#else
#define LOCK_PREFIX ""
#endif

#define ADDR (*(volatile long *) addr)

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void set_bit(long nr, volatile void * addr)
{
	__asm__ __volatile__( LOCK_PREFIX
		"btsq %1,%0"
		:"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __inline__ void __set_bit(long nr, volatile void * addr)
{
	__asm__(
		"btsq %1,%0"
		:"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */
static __inline__ void clear_bit(long nr, volatile void * addr)
{
	__asm__ __volatile__( LOCK_PREFIX
		"btrq %1,%0"
		:"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
}
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __inline__ void __change_bit(long nr, volatile void * addr)
{
	__asm__ __volatile__(
		"btcq %1,%0"
		:"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void change_bit(long nr, volatile void * addr)
{
	__asm__ __volatile__( LOCK_PREFIX
		"btcq %1,%0"
		:"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline__ int test_and_set_bit(long nr, volatile void * addr)
{
        long oldbit;

	__asm__ __volatile__( LOCK_PREFIX
		"btsq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR) : "memory");
	return oldbit;
}

/**
 * __test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.  
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __inline__ int __test_and_set_bit(long nr, volatile void * addr)
{
	long oldbit;

	__asm__(
		"btsq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
	return oldbit;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline__ int test_and_clear_bit(long nr, volatile void * addr)
{
	long oldbit;

	__asm__ __volatile__( LOCK_PREFIX
		"btrq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR) : "memory");
	return oldbit;
}

/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.  
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __inline__ int __test_and_clear_bit(long nr, volatile void * addr)
{
	long oldbit;

	__asm__(
		"btrq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR));
	return oldbit;
}

/* WARNING: non atomic and it can be reordered! */
static __inline__ int __test_and_change_bit(long nr, volatile void * addr)
{
	long oldbit;

	__asm__ __volatile__(
		"btcq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR) : "memory");
	return oldbit;
}

/**
 * test_and_change_bit - Change a bit and return its new value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline__ int test_and_change_bit(long nr, volatile void * addr)
{
	long oldbit;

	__asm__ __volatile__( LOCK_PREFIX
		"btcq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit),"=m" (ADDR)
		:"dIr" (nr), "m" (ADDR) : "memory");
	return oldbit;
}

#if 0 /* Fool kernel-doc since it doesn't do macros yet */
/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static int test_bit(int nr, const volatile void * addr);
#endif

static __inline__ int constant_test_bit(long nr, const volatile void * addr)
{
	return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static __inline__ int variable_test_bit(long nr, volatile void * addr)
{
	long oldbit;

	__asm__ __volatile__(
		"btq %2,%1\n\tsbbq %0,%0"
		:"=r" (oldbit)
		:"m" (ADDR),"dIr" (nr));
	return oldbit;
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 constant_test_bit((nr),(addr)) : \
 variable_test_bit((nr),(addr)))

/**
 * find_first_zero_bit - find the first zero bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum bitnumber to search
 *
 * Returns the bit-number of the first zero bit, not the number of the byte
 * containing a bit. -1 when none found.
 */
static __inline__ int find_first_zero_bit(void * addr, unsigned size)
{
	int d0, d1, d2;
	int res;

	if (!size)
		return 0;
	__asm__ __volatile__(
		"movl $-1,%%eax\n\t"
		"xorl %%edx,%%edx\n\t"
		"repe; scasl\n\t"
		"je 1f\n\t"
		"xorl -4(%%rdi),%%eax\n\t"
		"subq $4,%%rdi\n\t"
		"bsfl %%eax,%%edx\n"
		"1:\tsubq %%rbx,%%rdi\n\t"
		"shlq $3,%%rdi\n\t"
		"addq %%rdi,%%rdx"
		:"=d" (res), "=&c" (d0), "=&D" (d1), "=&a" (d2)
		:"1" ((size + 31) >> 5), "2" (addr), "b" (addr) : "memory");
	return res;
}

/**
 * find_next_zero_bit - find the first zero bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
static __inline__ int find_next_zero_bit (void * addr, int size, int offset)
{
	unsigned int * p = ((unsigned int *) addr) + (offset >> 5);
	int set = 0, bit = offset & 31, res;
	
	if (bit) {
		/*
		 * Look for zero in first byte
		 */
		__asm__("bsfl %1,%0\n\t"
			"jne 1f\n\t"
			"movl $32, %0\n"
			"1:"
			: "=r" (set)
			: "r" (~(*p >> bit)));
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * No zero yet, search remaining full bytes for a zero
	 */
	res = find_first_zero_bit (p, size - 32 * (p - (unsigned int *) addr));
	return (offset + set + res);
}

/* 
 * Find string of zero bits in a bitmap. -1 when not found.
 */ 
extern unsigned long 
find_next_zero_string(unsigned long *bitmap, long start, long nbits, int len);

static inline void set_bit_string(unsigned long *bitmap, unsigned long i, 
				  int len) 
{ 
	unsigned long end = i + len; 
	while (i < end) {
		__set_bit(i, bitmap); 
		i++;
	}
} 

static inline void clear_bit_string(unsigned long *bitmap, unsigned long i, 
				    int len) 
{ 
	unsigned long end = i + len; 
	while (i < end) {
		clear_bit(i, bitmap); 
		i++;
	}
} 

/**
 * ffz - find first zero in word.
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static __inline__ unsigned long ffz(unsigned long word)
{
	__asm__("bsfq %1,%0"
		:"=r" (word)
		:"r" (~word));
	return word;
}

#ifdef __KERNEL__

/**
 * ffs - find first bit set
 * @x: the word to search
 *
 * This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */
static __inline__ int ffs(int x)
{
	int r;

	__asm__("bsfl %1,%0\n\t"
		"jnz 1f\n\t"
		"movl $-1,%0\n"
		"1:" : "=r" (r) : "g" (x));
	return r+1;
}

/**
 * hweightN - returns the hamming weight of a N-bit word
 * @x: the word to weigh
 *
 * The Hamming Weight of a number is the total number of bits set in it.
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif /* __KERNEL__ */

#ifdef __KERNEL__

#define ext2_set_bit                 __test_and_set_bit
#define ext2_clear_bit               __test_and_clear_bit
#define ext2_test_bit                test_bit
#define ext2_find_first_zero_bit     find_first_zero_bit
#define ext2_find_next_zero_bit      find_next_zero_bit

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr) __test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) __set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) __test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#endif /* _X86_64_BITOPS_H */
