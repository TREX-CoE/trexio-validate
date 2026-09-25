#!/usr/bin/env bash
# Build the dependencies of trexio-validate into a prefix:
#   ci/build-deps.sh PREFIX libcint VERSION
#   ci/build-deps.sh PREFIX trexio  VERSION   (a release, from its tarball;
#                                              needs a Fortran compiler)
#   ci/build-deps.sh PREFIX trexio  master    (the development version, from git;
#                                              needs Emacs to generate the sources)
set -euo pipefail

prefix=$1
package=$2
version=$3
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

case "$package" in
  libcint)
    git clone --quiet --depth 1 --branch "v$version" https://github.com/sunqm/libcint.git "$work/src"
    cmake -S "$work/src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$prefix" -DENABLE_TEST=OFF
    ;;
  trexio)
    if [ "$version" = master ]; then
      # The development version is built with CMake, which also installs its
      # CMake package configuration.
      git clone --quiet --depth 1 https://github.com/TREX-CoE/trexio.git "$work/src"
      git -C "$work/src" log -1 --format='TREXIO master at %h (%cd)'
      cmake -S "$work/src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DTREXIO_FORTRAN=OFF -DBUILD_TESTING=OFF
    else
      # Releases are built with Autotools, which builds every release from its
      # tarball (the CMake build of 2.1 needs the org sources) and installs a
      # pkg-config file.
      curl -sSfL "https://github.com/TREX-CoE/trexio/releases/download/v$version/trexio-$version.tar.gz" \
        | tar xz -C "$work"
      cd "$work/trexio-$version"
      ./configure --prefix="$prefix" --enable-silent-rules
      make -j "$jobs"
      make install
      exit 0
    fi
    ;;
  *)
    echo "unknown package $package" >&2
    exit 2
    ;;
esac
cmake --build "$work/build" -j "$jobs"
cmake --install "$work/build"
