set shell := ["zsh", "-f", "-eu", "-o", "pipefail", "-c"]
set positional-arguments

architecture := replace(replace(arch(), "aarch64", "arm64"), "x86_64", "x64")
system := if os() == "macos" { "osx" } else { os() }
triplet := f'{{architecture}}-{{system}}'
cxx := env("CXX", if os() == "macos" { "/usr/bin/clang++" } else { "clang++" })

_:
    @just --list

deps:
    vcpkg install --triplet={{triplet}} --x-install-root=build/deps --x-wait-for-lock

configure configuration="Debug": deps
    CXX="{{cxx}}" cmake -S . -B build/cmake/{{configuration}} -G Ninja \
        -DCMAKE_BUILD_TYPE={{configuration}} \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DVCPKG_INSTALLED_DIR="{{justfile_directory()}}/build/deps" \
        -DVCPKG_TARGET_TRIPLET={{triplet}} \
        -DENABLE_SANITIZERS={{if configuration == "Debug" { "ON" } else { "OFF" }}}

test: (configure "Debug")
    cmake --build build/cmake/Debug --parallel 2
    ctest --test-dir build/cmake/Debug --output-on-failure --no-tests=error

build: (configure "Release")
    cmake --build build/cmake/Release --parallel 2
    ctest --test-dir build/cmake/Release --output-on-failure --no-tests=error

lint: (configure "Debug")
    clang-tidy -p build/cmake/Debug src/main.cpp tests/test_main.cpp

test-mcp: build
    node scripts/test-mcp.mjs build/bin/personal-log-mcp add,get,search,update,merge,delete,sql,schema,runs,import,backup,mine,status

inspect:
    npx --yes @modelcontextprotocol/inspector@2.14.0 ./build/bin/personal-log-mcp mcp

run *args:
    ./build/bin/personal-log-mcp "$@"

clean:
    rm -rf build
