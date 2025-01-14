#define DEBUG_ENABLE 1
#include "common.h"
#include "pci.h"
#include "mm.h"
#include "vfs.h"
#include "task.h"
#include "interface.h"

#define __XEN_INTERFACE_VERSION__ 0x00030205
#define TRAP_INSTR "syscall"

#define mb()    __asm__ __volatile__ ("mfence":::"memory")

#define HOST_XEN_SH_ADDR 0xe1000000

#include <xen/features.h>
#include <xen/evtchn.h>
#include <xen/xen.h>
#include <xen/memory.h>
#include <xen/hvm/params.h>
#include <xen/event_channel.h>
#include <xen/io/xs_wire.h>
#include <xen/grant_table.h>

#define __HYPERVISOR_dummycall 32

#define __STR(x) #x
#define STR(x) __STR(x)
char hypercall_page[PAGE_SIZE];


#define _hypercall2(type, name, a1, a2)                         \
({                                                              \
        long __res, __ign1, __ign2;                             \
        asm volatile (                                          \
                "call hypercall_page + ("STR(__HYPERVISOR_##name)" * 32)"\
                : "=a" (__res), "=D" (__ign1), "=S" (__ign2)    \
                : "1" ((long)(a1)), "2" ((long)(a2))            \
                : "memory" );                                   \
        (type)__res;                                            \
})
#define _hypercall3(type, name, a1, a2, a3)                     \
({                                                              \
        long __res, __ign1, __ign2, __ign3;                     \
        asm volatile (                                          \
                "call hypercall_page + ("STR(__HYPERVISOR_##name)" * 32)"\
                : "=a" (__res), "=D" (__ign1), "=S" (__ign2),   \
                "=d" (__ign3)                                   \
                : "1" ((long)(a1)), "2" ((long)(a2)),           \
                "3" ((long)(a3))                                \
                : "memory" );                                   \
        (type)__res;                                            \
})


#define wrmsr(msr,val1,val2) \
      __asm__ __volatile__("wrmsr" \
                           : /* no outputs */ \
                           : "c" (msr), "a" (val1), "d" (val2))

#define wrmsrl(msr,val) wrmsr(msr,(uint32_t)((uint64_t)(val)),((uint64_t)(val))>>32)


static inline int HYPERVISOR_memory_op(unsigned int cmd, void *arg) {
	return _hypercall2(int, memory_op, cmd, arg);
}
static inline int  HYPERVISOR_hvm_op(int op, void *arg) {
	return _hypercall2(unsigned long, hvm_op, op, arg);
}

static inline int HYPERVISOR_event_channel_op(int cmd, void *op)
{
  //  return _hypercall2(int, event_channel_op, cmd, op);
    return _hypercall2(int, dummycall, cmd, op);
}


static inline int HYPERVISOR_grant_table_op(
        unsigned int cmd, void *uop, unsigned int count)
{
        return _hypercall3(int, grant_table_op, cmd, uop, count);
}




static __inline__ int synch_test_and_set_bit(int nr, volatile void * addr)
{
    int oldbit;
    __asm__ __volatile__ (
        "lock btsl %2,%1\n\tsbbl %0,%0"
        : "=r" (oldbit), "=m" (ADDR) : "Ir" (nr) : "memory");
    return oldbit;
}

static __inline__ void synch_set_bit(int nr, volatile void * addr)
{
    __asm__ __volatile__ (
        "lock btsl %1,%0"
        : "=m" (ADDR) : "Ir" (nr) : "memory" );
}

static __inline__ void synch_clear_bit(int nr, volatile void * addr)
{
    __asm__ __volatile__ (
        "lock btrl %1,%0"
        : "=m" (ADDR) : "Ir" (nr) : "memory" );
}
static __inline__ int synch_const_test_bit(int nr, const volatile void * addr)
{
    return ((1UL << (nr & 31)) &
            (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}
#define active_evtchns(cpu,sh,idx)              \
    ((sh)->evtchn_pending[idx] &                \
     ~(sh)->evtchn_mask[idx])
#define wmb()   __asm__ __volatile__ ("sfence" ::: "memory") /* From CONFIG_UNORDERED_IO (linux) */
#define xchg(ptr,v) ((__typeof__(*(ptr)))__xchg((unsigned long)(v),(ptr),sizeof(*(ptr))))
#define __xg(x) ((volatile long *)(x))
static __inline__ unsigned long __ffs(unsigned long word)
{
        __asm__("bsfq %1,%0"
                :"=r" (word)
                :"rm" (word));
        return word;
}

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
        switch (size) {
                case 1:
                        __asm__ __volatile__("xchgb %b0,%1"
                                :"=q" (x)
                                :"m" (*__xg(ptr)), "0" (x)
                                :"memory");
                        break;
                case 2:
                        __asm__ __volatile__("xchgw %w0,%1"
                                :"=r" (x)
                                :"m" (*__xg(ptr)), "0" (x)
                                :"memory");
                        break;
                case 4:
                        __asm__ __volatile__("xchgl %k0,%1"
                                :"=r" (x)
                                :"m" (*__xg(ptr)), "0" (x)
                                :"memory");
                        break;
                case 8:
                        __asm__ __volatile__("xchgq %0,%1"
                                :"=r" (x)
                                :"m" (*__xg(ptr)), "0" (x)
                                :"memory");
                        break;
        }
        return x;
}

static __inline__ int synch_var_test_bit(int nr, volatile void * addr)
{
    int oldbit;
    __asm__ __volatile__ (
        "btl %2,%1\n\tsbbl %0,%0"
        : "=r" (oldbit) : "m" (ADDR), "Ir" (nr) );
    return oldbit;
}

#define barrier() __asm__ __volatile__("": : :"memory")

#define synch_test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 synch_const_test_bit((nr),(addr)) : \
 synch_var_test_bit((nr),(addr)))


extern shared_info_t *g_sharedInfoArea;
static inline int notify_remote_via_evtchn(evtchn_port_t port)
{
    evtchn_send_t op;
    op.port = port;
    DEBUG("xen : new Notifying remote  :%d\n",port);
    return HYPERVISOR_event_channel_op(EVTCHNOP_send, &op);
}
/********************************************************************/
struct __synch_xchg_dummy { unsigned long a[100]; };

#define __synch_xg(x) ((struct __synch_xchg_dummy *)(x))



static inline unsigned long __synch_cmpxchg(volatile void *ptr,
        unsigned long old,
        unsigned long new, int size)
{
    unsigned long prev;
    switch (size) {
        case 1:
            __asm__ __volatile__("lock; cmpxchgb %b1,%2"
                    : "=a"(prev)
                    : "q"(new), "m"(*__synch_xg(ptr)),
                    "0"(old)
                    : "memory");
            return prev;
        case 2:
            __asm__ __volatile__("lock; cmpxchgw %w1,%2"
                    : "=a"(prev)
                    : "r"(new), "m"(*__synch_xg(ptr)),
                    "0"(old)
                    : "memory");
            return prev;
#ifdef __x86_64__
        case 4:
            __asm__ __volatile__("lock; cmpxchgl %k1,%2"
                    : "=a"(prev)
                    : "r"(new), "m"(*__synch_xg(ptr)),
                    "0"(old)
                    : "memory");
            return prev;
        case 8:
            __asm__ __volatile__("lock; cmpxchgq %1,%2"
                    : "=a"(prev)
                    : "r"(new), "m"(*__synch_xg(ptr)),
                    "0"(old)
                    : "memory");
            return prev;
#else
        case 4:
            __asm__ __volatile__("lock; cmpxchgl %1,%2"
                    : "=a"(prev)
                    : "r"(new), "m"(*__synch_xg(ptr)),
                    "0"(old)
                    : "memory");
            return prev;
#endif
    }
    return old;
}
#ifndef rmb
#define rmb()  __asm__ __volatile__ ("lock; addl $0,0(%%esp)": : :"memory")
#endif


#define synch_cmpxchg(ptr, old, new) \
((__typeof__(*(ptr)))__synch_cmpxchg((ptr),\
                                     (unsigned long)(old), \
                                     (unsigned long)(new), \
                                     sizeof(*(ptr))))


/****************************************/

#define set_xen_guest_handle_raw(hnd, val)  do { (hnd).p = val; } while (0)

#define set_xen_guest_handle(hnd, val) set_xen_guest_handle_raw(hnd, val)

