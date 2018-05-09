#ifndef NSTACK_UTIL_H
#define NSTACK_UTIL_H

#ifndef __GLOBL1
#define __GLOBL1(sym) __asm__(".globl " #sym)
#define __GLOBL(sym) __GLOBL1(sym)
#endif

#ifndef __used
#define __used __attribute__((__used__))
#endif

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#ifndef __section
#define __section(x) __attribute__((__section__(x)))
#endif

#define __constructor __attribute__((constructor))
#define __destructor __attribute__((destructor))

/**
 * Returns a container of ptr, which is a element in some struct.
 * @param ptr       is a pointer to a element in struct.
 * @param type      is the type of the container struct.
 * @param member    is the name of the ptr in container struct.
 * @return Pointer to the container of ptr.
 */
#define container_of(ptr, type, member) \
    ((type *) ((uint8_t *) (ptr) -offsetof(type, member)))

#define num_elem(x) (sizeof(x) / sizeof(*(x)))

static inline int imax(int a, int b)
{
    return (a > b ? a : b);
}

static inline int imin(int a, int b)
{
    return (a < b ? a : b);
}

static inline long lmax(long a, long b)
{
    return (a > b ? a : b);
}

static inline long lmin(long a, long b)
{
    return (a < b ? a : b);
}

static inline unsigned int max(unsigned int a, unsigned int b)
{
    return (a > b ? a : b);
}

static inline unsigned int min(unsigned int a, unsigned int b)
{
    return (a < b ? a : b);
}

static inline unsigned long ulmax(unsigned long a, unsigned long b)
{
    return (a > b ? a : b);
}

static inline unsigned long ulmin(unsigned long a, unsigned long b)
{
    return (a < b ? a : b);
}

static inline unsigned int smin(size_t a, size_t b)
{
    return (a < b ? a : b);
}

static inline unsigned int uround_up(unsigned n, unsigned s)
{
    return ((n + s - 1) / s) * s;
}

#endif /* NSTACK_UTIL */
