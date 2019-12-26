LOCAL_PATH:= $(call my-dir)

GMP_CFLAGS := -O2 -g -pedantic -fomit-frame-pointer -Wa,--noexecstack -ffunction-sections -funwind-tables -no-canonical-prefixes -fno-strict-aliasing

GMP_SRC_FILES := \
	gen-fib.c  assert.c     errno.c        gen-fac.c        gen-psqr.c \
	memory.c     mp_dv_tab.c    mp_set_fns.c  tal-debug.c \
	version.c    bootstrap.c  extract-dbl.c  \
	gen-trialdivtab.c  mp_bpl.c      mp_get_fns.c   nextprime.c \
	tal-notreent.c    compat.c     gen-bases.c    gen-jacobitab.c \
	invalid.c     mp_clz_tab.c  mp_minv_tab.c  primesieve.c  tal-reent.c


# columbia_gmp module
include $(CLEAR_VARS)
LOCAL_MODULE:= columbia_gmp
LOCAL_SRC_FILES := $(GMP_SRC_FILES)
LOCAL_CFLAGS := $(GMP_CFLAGS)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
include $(BUILD_STATIC_LIBRARY)
