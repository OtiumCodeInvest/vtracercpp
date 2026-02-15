project(wrapcv)

set(files
	wrapcv.cpp
	wrapcv.h
	opencv2/core/src/algorithm.cpp
	opencv2/core/src/alloc.cpp
	opencv2/core/src/arithm.cpp
	opencv2/core/src/array.cpp
	opencv2/core/src/cmdparser.cpp
	opencv2/core/src/convert.cpp
	opencv2/core/src/copy.cpp
	opencv2/core/src/datastructs.cpp
	opencv2/core/src/drawing.cpp
	opencv2/core/src/dxt.cpp
	opencv2/core/src/glob.cpp
	opencv2/core/src/gl_core_3_1.cpp
	opencv2/core/src/gpumat.cpp
	opencv2/core/src/lapack.cpp
	opencv2/core/src/mathfuncs.cpp
	opencv2/core/src/matmul.cpp
	opencv2/core/src/matop.cpp
	opencv2/core/src/matrix.cpp
	opencv2/core/src/opengl_interop.cpp
	opencv2/core/src/opengl_interop_deprecated.cpp
	opencv2/core/src/out.cpp
	opencv2/core/src/parallel.cpp
	opencv2/core/src/persistence.cpp
	opencv2/core/src/rand.cpp
	opencv2/core/src/stat.cpp
	opencv2/core/src/system.cpp
	opencv2/core/src/tablescore.cpp
	opencv2/imgproc/src/accum.cpp
	opencv2/imgproc/src/approx.cpp
	opencv2/imgproc/src/canny.cpp
	opencv2/imgproc/src/clahe.cpp
	opencv2/imgproc/src/color.cpp
	opencv2/imgproc/src/contours.cpp
	opencv2/imgproc/src/convhull.cpp
	opencv2/imgproc/src/corner.cpp
	opencv2/imgproc/src/cornersubpix.cpp
	opencv2/imgproc/src/deriv.cpp
	opencv2/imgproc/src/distransform.cpp
	opencv2/imgproc/src/emd.cpp
	opencv2/imgproc/src/featureselect.cpp
	opencv2/imgproc/src/filter.cpp
	opencv2/imgproc/src/floodfill.cpp
	opencv2/imgproc/src/gabor.cpp
	opencv2/imgproc/src/generalized_hough.cpp
	opencv2/imgproc/src/geometry.cpp
	opencv2/imgproc/src/grabcut.cpp
	opencv2/imgproc/src/histogram.cpp
	opencv2/imgproc/src/hough.cpp
	opencv2/imgproc/src/imgwarp.cpp
	opencv2/imgproc/src/linefit.cpp
	opencv2/imgproc/src/matchcontours.cpp
	opencv2/imgproc/src/moments.cpp
	opencv2/imgproc/src/morph.cpp
	opencv2/imgproc/src/phasecorr.cpp
	opencv2/imgproc/src/pyramids.cpp
	opencv2/imgproc/src/rotcalipers.cpp
	opencv2/imgproc/src/samplers.cpp
	opencv2/imgproc/src/segmentation.cpp
	opencv2/imgproc/src/shapedescr.cpp
	opencv2/imgproc/src/smooth.cpp
	opencv2/imgproc/src/subdivision2d.cpp
	opencv2/imgproc/src/sumpixels.cpp
	opencv2/imgproc/src/tablesimgproc.cpp
	opencv2/imgproc/src/templmatch.cpp
	opencv2/imgproc/src/thresh.cpp
	opencv2/imgproc/src/undistort.cpp
	opencv2/imgproc/src/utils.cpp
    opencv2/core/affine.hpp
    opencv2/core/core.hpp
    opencv2/core/core_c.h
    opencv2/core/cuda_devptrs.hpp
    opencv2/core/devmem2d.hpp
    opencv2/core/eigen.hpp
    opencv2/core/gpumat.hpp
    opencv2/core/internal.hpp
    opencv2/core/mat.hpp
    opencv2/core/opengl_interop.hpp
    opencv2/core/opengl_interop_deprecated.hpp
    opencv2/core/operations.hpp
    opencv2/core/src/gl_core_3_1.hpp
    opencv2/core/src/precomp.hpp
    opencv2/core/types_c.h
    opencv2/core/version.hpp
    opencv2/core/wimage.hpp
    opencv2/dynamicuda/dynamicuda.hpp
    opencv2/imgproc/imgproc.hpp
    opencv2/imgproc/imgproc_c.h
    opencv2/imgproc/src/gcgraph.hpp
    opencv2/imgproc/src/precomp.hpp
    opencv2/imgproc/src/_geom.h
    opencv2/imgproc/src/_list.h
    opencv2/imgproc/types_c.h
)

source_group("files" FILES ${files})

add_definitions(-D_STATIC_=static)
add_library(${PROJECT_NAME} ${files})
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(${PROJECT_NAME} PUBLIC shared)

if(CMAKE_COMPILER_IS_GNUCC)
	# suppress warning spam from types_c.h
	add_definitions(-Wno-class-memaccess -Wno-misleading-indentation)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR} .)

