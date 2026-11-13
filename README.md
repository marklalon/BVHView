# BVHView

BVHView is a simple [.bvh animation file format](https://research.cs.wisc.edu/graphics/Courses/cs-838-1999/Jeff/BVH.html) viewer built using [raylib](https://www.raylib.com/).



https://github.com/orangeduck/BVHView/assets/177299/9f976284-02c1-4a14-bca4-b8b3d7c9a774



* [Download (Windows)](https://theorangeduck.com/media/uploads/BVHView/bvhview.zip)
* [Project Page](https://theorangeduck.com/page/bvhview)
* [Web Demo](https://theorangeduck.com/media/uploads/BVHView/bvhview.html)

# Building

BVHView pulls its three dependencies in as [git submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules):

* [raylib](https://github.com/marklalon/raylib) → `build/raylib/raylib`
* [raygui](https://github.com/marklalon/raygui) → `build/raylib/raygui`
* [libwebp 1.6.0](https://github.com/webmproject/libwebp) → `build/libwebp`

The `raylib` and `raygui` submodules point at the [marklalon/raylib](https://github.com/marklalon/raylib) and [marklalon/raygui](https://github.com/marklalon/raygui) forks, which carry BVHView-specific patches — they are required to build.

Clone the repo together with its submodules:

```sh
git clone --recurse-submodules <this-repo-url>
```

If you already have a clone without them, initialize them once:

```sh
git submodule update --init --recursive
```

`build.bat` also runs that automatically if the submodules are missing. After the submodules are in place, build raylib first, then BVHView. When invoking `make` directly, pass `RAYLIB_DIR=build/raylib` so it finds the submodule copy (its default is `C:/raylib` or `~/raylib`). `WEBP_DIR` already defaults to `build/libwebp`.

## Windows

Download and install [MinGW](https://www.mingw-w64.org/) in some form. Perhaps [w64devkit](https://www.mingw-w64.org/downloads/#w64devkit) or [MSYS2](https://www.mingw-w64.org/downloads/#msys2).

Build raylib by going to `build/raylib/raylib/src` and running `make`.

Run `build.bat` (or `make RAYLIB_DIR=build/raylib`) in the main directory to build `bvhview.exe`.

To build a release version with optimizations enabled and no console window run `make BUILD_MODE=RELEASE RAYLIB_DIR=build/raylib` in the main directory instead.

## macOS

Install the Xcode command line tools (or an alternative toolchain such as Homebrew’s GCC/Clang) so `make` and `clang` are available.

Build raylib by going to `build/raylib/raylib/src` and running `make PLATFORM=PLATFORM_DESKTOP`.

Run `make PLATFORM_OS=Darwin RAYLIB_DIR=build/raylib` in the main directory to build `bvhview`.

To build a release version with optimizations enabled run `make BUILD_MODE=RELEASE PLATFORM_OS=Darwin RAYLIB_DIR=build/raylib` in the main directory instead.

## Linux

Follow [this guide](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux) to install any dependencies.

Build raylib by going to `build/raylib/raylib/src` and running `make`.

Run `make RAYLIB_DIR=build/raylib` in the main directory to build `bvhview`.

To build a release version with optimizations enabled run `make BUILD_MODE=RELEASE RAYLIB_DIR=build/raylib` in the main directory instead.

## Other

For other platforms you should be able to build BVHView by hacking the `Makefile` a bit. Contributions here welcome.
