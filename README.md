# tkf25

This project depends on libcurl and libunifex. These libraries are supposed to be installed using vcpkg. Please set up vcpkg in advance.

## Preparing visual studio solution
cmake -H. -Bbuild -G "Visual Studio 17 2022"  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

## Opening visual studio solution
open build/tkf25.sln
