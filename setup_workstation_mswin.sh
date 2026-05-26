#!/bin/sh
CONFIG_SHELL=/bin/sh
export CONFIG_SHELL

SUFFIX=_mswin

configure_target() {
  TARGET="$1"

  PREFIX="$(realpath --no-symlinks "$PWD/../gcc-thomas-healy${SUFFIX}$2/")"
  SYSROOT="/usr/${TARGET}/"
  BUILD_DIR="../gcc-thomas-healy${SUFFIX}$2_build/"

  echo "==================================================================="
  echo "Configuring target: $TARGET"
  echo "  PREFIX    : $PREFIX"
  echo "  SYSROOT   : $SYSROOT"
  echo "  BUILD_DIR : $BUILD_DIR"
  echo "==================================================================="

  # System headers will be looked for in $SYSROOT/mingw/include instead
  # of $SYSROOT/include, therefore create a symbolic link if needed.
  if [ ! -e "/usr/${TARGET}/mingw" ]; then
    echo "Creating symbolic link from /usr/${TARGET}/mingw to /usr/${TARGET}/"
    sudo ln -sf "/usr/${TARGET}/" "/usr/${TARGET}/mingw"
  fi

  # Must create symbolic links to tools like the assembler and linker.
  mkdir -p "$PREFIX/$TARGET"
  if [ ! -e "$PREFIX/$TARGET/bin" ]; then
    echo "Creating symbolic link from $PREFIX/$TARGET/bin to /usr/${TARGET}/bin/"
    sudo ln -sf "/usr/${TARGET}/bin/" "$PREFIX/$TARGET/bin"
  fi

  cd "$BUILD_DIR"

  if [ -z "$(find . -maxdepth 1 -type f -name Makefile -print -quit)" ]; then
    echo "==================================================================="
    echo "Creating build directory and configuring build for $TARGET:"
    echo "  ../gcc-thomas-healy_source/configure \\"
    echo "    --prefix=${PREFIX} \\"
    echo "    --enable-languages=c,c++ \\"
    echo "    --disable-multilib \\"
    echo "    --target=${TARGET} \\"
    echo "    --with-sysroot=${SYSROOT} \\"
    echo "    --with-build-sysroot=${SYSROOT} \\"
    echo "    --with-native-system-header-dir=/include/"
    echo "==================================================================="

    ../gcc-thomas-healy_source/configure \
      --prefix="$PREFIX" \
      --enable-languages=c,c++ \
      --disable-multilib \
      --target="$TARGET" \
      --with-sysroot="$SYSROOT" \
      --with-build-sysroot="$SYSROOT" \
      --with-native-system-header-dir=/include/
  else
    echo "Build directory for $TARGET already contains files; skipping configure."
  fi

  cd -
}

configure_target x86_64-w64-mingw32 64
configure_target i686-w64-mingw32   32

echo "==================================================================="
echo "ALL DONE"
echo "==================================================================="
