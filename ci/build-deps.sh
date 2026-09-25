#!/usr/bin/env bash
# Build the dependencies of trexio-validate into a prefix:
#   ci/build-deps.sh PREFIX libcint VERSION
#   ci/build-deps.sh PREFIX trexio  VERSION   (a release, from its tarball)
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
      git clone --quiet --depth 1 https://github.com/TREX-CoE/trexio.git "$work/src"
      git -C "$work/src" log -1 --format='TREXIO master at %h (%cd)'
    else
      curl -sSfL "https://github.com/TREX-CoE/trexio/releases/download/v$version/trexio-$version.tar.gz" \
        | tar xz -C "$work"
      mv "$work/trexio-$version" "$work/src"
    fi
    # TREXIO_TESTS is the name in 2.6, BUILD_TESTING in later versions.
    cmake -S "$work/src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$prefix" -DTREXIO_FORTRAN=OFF -DTREXIO_TESTS=OFF -DBUILD_TESTING=OFF
    ;;
  *)
    echo "unknown package $package" >&2
    exit 2
    ;;
esac
cmake --build "$work/build" -j "$jobs"
cmake --install "$work/build"
