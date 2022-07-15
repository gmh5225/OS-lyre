AC_INIT([Limine Bare Bones], m4_esyscmd([./version.sh]), [https://github.com/limine-bootloader/limine-barebones/issues], [limine], [https://limine-bootloader.org/])

AC_PREREQ(2.69)

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SUBDIRS([cross-detect])

SRCDIR="$(cd "$srcdir" && pwd -P)"
BUILDDIR="$(pwd -P)"

AC_SUBST(SRCDIR, [$SRCDIR])
AC_SUBST(BUILDDIR, [$BUILDDIR])

werror_state="no"
AC_ARG_ENABLE([werror],
    AS_HELP_STRING([--enable-werror], [treat warnings as errors]),
    werror_state="$enableval")

if test "$werror_state" = "yes"; then
    AC_SUBST(WERROR, [-Werror])
fi

AC_PROG_INSTALL

AC_CHECK_PROG([FIND_FOUND], [find], [yes])
if ! test "x$FIND_FOUND" = "xyes"; then
    AC_MSG_ERROR([find not found, please install find before configuring])
fi

m4_define([DEFAULT_ARCH], [x86_64])
AC_ARG_VAR(ARCH, [target architecture @<:@default: ]DEFAULT_ARCH[@:>@])
test "x$ARCH" = "x" && ARCH="DEFAULT_ARCH"

AC_ARG_VAR(CROSS_TOOLCHAIN, [alternative cross toolchain prefix (or 'llvm')])
AC_ARG_VAR(CROSS_CC, [cross C compiler command])
AC_ARG_VAR(CROSS_LD, [cross linker command])

rm -rf "$BUILDDIR/cross-files"
mkdir -p "$BUILDDIR/cross-files"
(
cd "$BUILDDIR/cross-files"
TRIPLET="$ARCH"-elf "$SRCDIR/cross-detect/configure"
) || exit 1

AC_SUBST([CROSS_FILE], [cross-files/$ARCH-toolchain.mk])

m4_define([DEFAULT_CROSS_CFLAGS], [-O3 -pipe -Wall -Wextra])
AC_ARG_VAR(CROSS_CFLAGS, [cross C flags @<:@default: ]DEFAULT_CROSS_CFLAGS[@:>@])
test "x$CROSS_CFLAGS" = "x" && CROSS_CFLAGS="DEFAULT_CROSS_CFLAGS"

m4_define([DEFAULT_CROSS_LDFLAGS], [])
AC_ARG_VAR(CROSS_LDFLAGS, [cross linker flags @<:@default: ]DEFAULT_CROSS_LDFLAGS[@:>@])
test "x$CROSS_LDFLAGS" = "x" && CROSS_LDFLAGS="DEFAULT_CROSS_LDFLAGS"

AC_CONFIG_FILES(GNUmakefile)
AC_OUTPUT
