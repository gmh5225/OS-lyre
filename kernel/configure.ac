AC_INIT([Lyre], [m4_esyscmd([./version.sh])], [https://github.com/lyre-os/lyre/issues], [lyre])

AC_PREREQ([2.69])

AC_CONFIG_AUX_DIR([build-aux])

SRCDIR="$(cd "$srcdir" && pwd -P)"
BUILDDIR="$(pwd -P)"

AC_SUBST([SRCDIR])
AC_SUBST([BUILDDIR])

werror_state="no"
AC_ARG_ENABLE([werror],
    [AS_HELP_STRING([--enable-werror], [treat warnings as errors])],
    [werror_state="$enableval"])

m4_define([DEFAULT_ARCH], [x86_64])
AC_ARG_VAR([ARCH], [target architecture @<:@default: ]DEFAULT_ARCH[@:>@])
test "x$ARCH" = "x" && ARCH="DEFAULT_ARCH"

AC_DEFUN([PROG_ABSPATH], [
    case "$$1" in
        .*) $1="$(cd "$(dirname "$$1")" && pwd -P)/$(basename "$$1")" ;;
    esac
])

AC_PROG_MKDIR_P
PROG_ABSPATH([MKDIR_P])
AC_PROG_INSTALL
PROG_ABSPATH([INSTALL])
AC_PROG_GREP
PROG_ABSPATH([GREP])
AC_PROG_SED
PROG_ABSPATH([SED])
AC_PROG_AWK
PROG_ABSPATH([AWK])

AC_CHECK_PROG([FIND_FOUND], [find], [yes])
if ! test "x$FIND_FOUND" = "xyes"; then
    AC_MSG_ERROR([find not found, please install find before configuring])
fi

AC_ARG_VAR([FREESTANDING_TOOLCHAIN], [alternative freestanding toolchain prefix (or 'llvm', or 'gnu')])
AC_ARG_VAR([FREESTANDING_CC], [freestanding C compiler command])
AC_ARG_VAR([FREESTANDING_LD], [freestanding linker command])
AC_ARG_VAR([FREESTANDING_OBJDUMP], [freestanding objdump command])
AC_ARG_VAR([FREESTANDING_STRIP], [freestanding strip command])

rm -rf "$BUILDDIR/toolchain-files"
$MKDIR_P "$BUILDDIR/toolchain-files"
ARCHITECTURE="$ARCH" \
    WANT_FREESTANDING_CC=yes \
    WANT_FREESTANDING_LD=yes \
    WANT_FREESTANDING_STRIP=yes \
    WANT_FREESTANDING_OBJDUMP=yes \
    "$SRCDIR/freestanding-toolchain" >"$BUILDDIR/toolchain-files/$ARCH-toolchain.mk" || exit 1

AC_SUBST([TOOLCHAIN_FILE], [toolchain-files/$ARCH-toolchain.mk])

m4_define([DEFAULT_FREESTANDING_CFLAGS], [-g -O2 -pipe -Wall -Wextra])
AC_ARG_VAR([FREESTANDING_CFLAGS], [freestanding C flags @<:@default: ]DEFAULT_FREESTANDING_CFLAGS[@:>@])
test "x$FREESTANDING_CFLAGS" = "x" && FREESTANDING_CFLAGS="DEFAULT_FREESTANDING_CFLAGS"
if test "$werror_state" = "yes"; then
    FREESTANDING_CFLAGS="$FREESTANDING_CFLAGS -Werror"
fi

m4_define([DEFAULT_FREESTANDING_CPPFLAGS], [])
AC_ARG_VAR([FREESTANDING_CPPFLAGS], [freestanding C preprocessor flags @<:@default: ]DEFAULT_FREESTANDING_CPPFLAGS[@:>@])
test "x$FREESTANDING_CPPFLAGS" = "x" && FREESTANDING_CPPFLAGS="DEFAULT_FREESTANDING_CPPFLAGS"

m4_define([DEFAULT_FREESTANDING_LDFLAGS], [])
AC_ARG_VAR([FREESTANDING_LDFLAGS], [freestanding linker flags @<:@default: ]DEFAULT_FREESTANDING_LDFLAGS[@:>@])
test "x$FREESTANDING_LDFLAGS" = "x" && FREESTANDING_LDFLAGS="DEFAULT_FREESTANDING_LDFLAGS"

AC_ARG_VAR([LYRE_DEBUG], [debug mode])
test "x$LYRE_DEBUG" = "x" && LYRE_DEBUG="0"

AC_PREFIX_DEFAULT([/usr/local])

AC_CONFIG_FILES([GNUmakefile])
AC_OUTPUT
