

#define PIC
#define STACK_ALIGNMENT 16
#define fseek fseeko
#define ftell ftello
#define USE_AVXSYNTH 0

#define HAVE_THREAD 1
#define HAVE_GPL 1
#define HAVE_INTERLACED 1 // 隔行扫描

#if defined(__ANDROID__)
#define HAVE_LOG2F 0 // ndk-19 已有, 但我们使用的是14
#else
#define HAVE_LOG2F 1
#endif

#if defined(__APPLE__)
    #define SYS_MACOSX 1
    // 启用汇编函数的前下划线修饰
    // 苹果平台c编译器默认该行为
    #define PREFIX
#elif defined(_MSC_VER)
    #define SYS_WINDOWS 1
#else
    #define SYS_LINUX 1
#endif

#if defined(__linux__)
    // only linux provide memalign
    // x264 does not call _aligin_malloc. etc.
    #define HAVE_MALLOC_H 1
#else
    #define HAVE_MALLOC_H 0
#endif

#if defined(_MSC_VER)
    #define HAVE_MMX 1
    #define HAVE_OPENCL 1
    #define HAVE_WIN32THREAD 1
    #define HAVE_POSIXTHREAD 0
    #define HAVE_GETOPT_LONG 0
    // it is gnu extension
    #define HAVE_VECTOREXT 0
#else
    #define HAVE_MMX 0
    #define HAVE_GETOPT_LONG 1
    #define HAVE_POSIXTHREAD 1
    #define HAVE_VECTOREXT 1
    // opencl existing in iOS as private
    // not in android
    #define HAVE_OPENCL 0
    #define HAVE_WIN32THREAD 0
#endif

#if (defined(__arm__)) || (defined(__arm64__))
    #if defined(__arm64__)
        #define ARCH_AARCH64 1
        #define ARCH_ARM 0
        #define HAVE_ARMV6 0
        #define HAVE_ARMV6T2 0
    #else
        #define ARCH_AARCH64 0
        #define ARCH_ARM 1
        #define HAVE_ARMV6 1
        #define HAVE_ARMV6T2 1
    #endif

    #if (defined(__ARM_ARCH_7A__) || defined(__arm64__))
        #define HAVE_NEON 1
    #else
        #define HAVE_NEON 0
    #endif
#else
    #define HAVE_ARMV6 0
    #define HAVE_ARMV6T2 0
#endif

// the two is PPC specific
#define HAVE_ALTIVEC 0
#define HAVE_ALTIVEC_H 0
// BEOS specific
#define HAVE_BEOSTHREAD 0

// external library support
#define HAVE_AVS 0
#define HAVE_SWSCALE 0
#define HAVE_LAVF 0
#define HAVE_FFMS 0
#define HAVE_GPAC 0
#define HAVE_LSMASH 0

#if defined(__linux__)
    #define HAVE_AS_FUNC 1
    // linux specific
    #define HAVE_CPU_COUNT 1
    #define HAVE_THP 1
#else
    // llvm 3.5 does not support it yet
    #define HAVE_AS_FUNC 0
    #define HAVE_CPU_COUNT 0
    #define HAVE_THP 0
#endif

// this is gnu asm on x86
// we will use microsoft compiler on windows
// so it can't benefit from this
#define HAVE_X86_INLINE_ASM 0

// intel icc compiler
#define HAVE_INTEL_DISPATCHER 0
