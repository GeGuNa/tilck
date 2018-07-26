
/*
 * This is a TEMPLATE. The actual "generated_config.h" is generated my CMake
 * and put in project's build directory.
 */


#pragma once

#define PROJ_BUILD_DIR         "@CMAKE_BINARY_DIR@"
#define BUILDTYPE_STR          "@CMAKE_BUILD_TYPE@"

#define BL_ST2_DATA_SEG        (@BL_ST2_DATA_SEG@)
#define BL_BASE_ADDR           (@BL_BASE_ADDR@)
#define KERNEL_BASE_VA         (@KERNEL_BASE_VA@)
#define KERNEL_PADDR           (@KERNEL_PADDR@)
#define LINEAR_MAPPING_MB      (@LINEAR_MAPPING_MB@)
#define USER_STACK_PAGES       (@USER_STACK_PAGES@)

#define KERNEL_FILE_PATH       "/@KERNEL_FATPART_PATH@"
#define KERNEL_FILE_PATH_EFI   "\\@KERNEL_FATPART_PATH_EFI@"

#define TIMER_HZ               (@TIMER_HZ@)

/* enabled by default */
#cmakedefine01 KERNEL_TRACK_NESTED_INTERRUPTS
#cmakedefine01 TERM_PERF_METRICS
#cmakedefine01 PANIC_SHOW_STACKTRACE

/* disabled by default */
#cmakedefine01 PANIC_SHOW_REGS
#cmakedefine01 KMALLOC_FREE_MEM_POISONING
#cmakedefine01 KMALLOC_SUPPORT_DEBUG_LOG
#cmakedefine01 KMALLOC_SUPPORT_LEAK_DETECTOR
#cmakedefine01 KMALLOC_HEAPS_CREATION_DEBUG
#cmakedefine01 BOOTLOADER_POISON_MEMORY
#cmakedefine01 DEBUG_CHECKS_IN_RELEASE_BUILD

#if DEBUG_CHECKS_IN_RELEASE_BUILD
   #ifdef NDEBUG
      #undef NDEBUG
      #define DEBUG
   #endif
#endif