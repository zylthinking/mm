#if !(__x86_64__ || __i386__)
#error this header is for x86 only
#endif

static inline int __get_cpuid (unsigned int level, unsigned int *eax,
                               unsigned int *ebx, unsigned int *ecx,
                               unsigned int *edx) {
    asm("cpuid" : "=a"(*eax), "=b" (*ebx), "=c"(*ecx), "=d"(*edx) : "0"(level));
    return 1;
}
