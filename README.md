# VTracerCpp

A pure C++ port of the [vtracer](https://github.com/visioncortex/vtracer) library, originally written in Rust by [visioncortex](https://github.com/visioncortex). 

**VTracerCpp** is a fast, dependency-light tool and library for converting raster images (like PNG or JPEG) into vector graphics (SVG). This repository aims to bring the exact same high-quality, hierarchical clustering and spline-fitting algorithms to the C++ ecosystem, paired with a lightweight graphical interface.

Replicates the hierarchical clustering, path simplification, and spline generation of the original Rust implementation.

## Build command on Linux
```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## Build command on Windows
```
$ mkdir build
$ cd build
$ cmake -DCMAKE_GENERATOR_PLATFORM=x64 ..
$ Open vtracercpp.sln project in Visual Studio and build
```

Alternative open as folder
