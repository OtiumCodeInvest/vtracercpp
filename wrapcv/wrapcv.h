#pragma once

#include "shared/types.h"
#include "shared/math.h"

#include "wrapcv/opencv2/core/internal.hpp"
#include "wrapcv/opencv2/core/core.hpp"
#include "wrapcv/opencv2/imgproc/imgproc.hpp"
#include "wrapcv/opencv2/imgproc/imgproc_c.h"
/*
IplImage* LoadImageFromArchive(const char* calibrationFileName,const char* imageName);
IplImage* LoadImageFromArchive(const uint8* archive,int archiveLength,const char* imageName);
void SaveImagePNG(const char* fileName,IplImage* image,float scale=1.0f);
void ScaleImageRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn);
void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const V2& tl,const V2& tr,const V2& bl);
void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const V2& center,float angle,float scale);
void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const V2& trans,float angle);
void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const M23& transform);
void WarpAlpha(uint8_t* pixelsOut,int widthOut,int heightOut,const uint8_t* pixelsIn,int widthIn,int heightIn,const M23& transform);
void BlendBuffers(uint8_t* pixelsOut,int count,const uint8_t* pixelsInA,int scalarA,int scalarB);
*/
void BlurImageAlpha(uint8_t* pixelsAlpha,int width,int height,int radius);
void BlurImageAlpha(float* pixelsAlpha,int width,int height,int radius);


namespace cv {

enum LineTypes
{
    FILLED  = -1,
    LINE_4  = 4,
    LINE_8  = 8,
    LINE_AA = 16,
};

};