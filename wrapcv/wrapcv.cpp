
#include "wrapcv.h"

#include "shared/math.h"
//#include "shared/image.h"
//#include "shared/dict.h"

void BlurImageAlpha(uint8_t* pixelsAlpha,int width,int height,int radius) {
	cv::Mat img=cv::Mat(height,width,CV_8UC1,(char*)pixelsAlpha);
	cv::GaussianBlur(img,img,cv::Size(radius,radius),0,0);
}

void BlurImageAlpha(float* pixelsAlpha,int width,int height,int radius) {
	cv::Mat img=cv::Mat(height,width,CV_32FC1,pixelsAlpha);
	cv::GaussianBlur(img,img,cv::Size(radius,radius),0,0);
}

/*
void SaveImagePNG(const char* fileName,IplImage* image,float scale) {
	if(image->depth==8) {
		ImageWritePNG(fileName,image->width,image->height,image->nChannels,(uint8*)image->imageData,image->widthStep);
	}else
	if(image->depth==32) {
		if(image->nChannels!=1)
			FATAL("INT3;");
		float* src=(float*)image->imageData;
		int step=image->widthStep/sizeof(float);
		if(scale<0) {
			float maxf=src[0];
			for(int y=0;y!=image->height;y++) {
				for(int x=0;x!=image->width;x++) {
					if(maxf<src[y*step+x])maxf=src[y*step+x];
				}
			}
			scale=255.0f/maxf;
		}
		uint8* dst=new uint8[image->width*image->height*image->nChannels];
		for(int y=0;y!=image->height;y++) {
			for(int x=0;x!=image->width;x++) {
				dst[y*image->width+x]=(uint8)CLAMP((src[y*step+x]*scale),0,255);
			}
		}
		ImageWritePNG(fileName,image->width,image->height,image->nChannels,dst,image->width);
		delete[] dst;
	}else{
			FATAL("INT3;");
	}
}
*/
void DrawLineAlpha(uint8_t* pixelsAlpha,const V2& st,const V2& en,int thickness,float color,int width,int height) {
	VI2 p=VI2((int)(st.x*256.0f-128.0f),(int)(st.y*256.0f-128.0f));
	VI2 p1=VI2((int)(en.x*256.0f-128.0f),(int)(en.y*256.0f-128.0f));
	cv::Mat img=cv::Mat(height,width,CV_8UC1,(char*)pixelsAlpha);
	cv::Scalar cvcolor=cv::Scalar(color,color,color);
	cv::line(img,cv::Point(p.x,p.y),cv::Point(p1.x,p1.y),cvcolor,thickness,cv::LINE_AA,8);
}

//void DrawLineAlpha(uint8_t* pixelsAlpha,const V2& st,const V2& en,int thickness,float color,int width,int height){
//	FATAL("DrawLineAlpha");
//}
void DrawLineRGB(uint8_t* pixelsRGB,const V2& st,const V2& en,int thickness,uint8_t (&color)[3],int width,int height){
	FATAL("DrawLineRGB");
}
void DrawSquareAlpha(uint8_t* pixelsAlpha,const V2& st,const V2& en,int thickness,float color,int width,int height){
	//FATAL("DrawSquareAlpha");
	VI2 p=VI2((int)(st.x*256.0f-128.0f),(int)(st.y*256.0f-128.0f));
	VI2 p1=VI2((int)(en.x*256.0f-128.0f),(int)(en.y*256.0f-128.0f));
	cv::Mat img=cv::Mat(height,width,CV_8UC1,(char*)pixelsAlpha);
	cv::Scalar cvcolor=cv::Scalar(color,color,color);
	cv::rectangle(img,cv::Point(p.x,p.y),cv::Point(p1.x,p1.y),cvcolor,thickness,cv::LINE_AA,8);
}
/*
bool DrawEditsRGB(uint8_t* pixelsOutRGB,const Dict& edits,int skipIndices,const uint8_t* pixelsInRGB,int width,int height){
	FATAL("DrawEditsRGB");
	return false;
}
bool DrawEditsAlpha(uint8_t* pixelsOutAlpha,const Dict& edits,int skipIndices,const uint8_t* pixelsInAlpha,int width,int height){
	FATAL("DrawEditsAlpha");
	return false;
}
*/
void ScaleImageRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn) {
	cv::Mat img=cv::Mat(heightIn,widthIn,CV_8UC3,(char*)pixelsInRGB);
	cv::Size s;
	s.width=widthOut;
	s.height=heightOut;
	cv::Mat imgOut=cv::Mat(heightOut,widthOut,CV_8UC3,(char*)pixelsOutRGB);
	cv::resize(img,imgOut,s,0.0,0.0,1);
}

void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const V2& tl,const V2& tr,const V2& bl) {
	cv::Mat src=cv::Mat(heightIn,widthIn,CV_8UC3,(char*)pixelsInRGB);
	cv::Mat dst=cv::Mat(heightOut,widthOut,CV_8UC3,(char*)pixelsOutRGB);

	cv::Point2f srcTri[3];
	srcTri[0]=cv::Point2f(tl.x,tl.y);
	srcTri[1]=cv::Point2f(tr.x,tr.y);
	srcTri[2]=cv::Point2f(bl.x,bl.y);

	//srcTri[0]=cv::Point2f(0,0);
	//srcTri[1]=cv::Point2f((float)src.cols,0);
	//srcTri[2]=cv::Point2f((float)src.cols,(float)src.rows);
 
	cv::Point2f dstTri[3];
	dstTri[0]=cv::Point2f(0,0);
	dstTri[1]=cv::Point2f((float)dst.cols,0);
	dstTri[2]=cv::Point2f((float)dst.cols,(float)dst.rows);

	//dstTri[0]=cv::Point2f(tl.x,tl.y);
	//dstTri[1]=cv::Point2f(tr.x,tr.y);
	//dstTri[2]=cv::Point2f(bl.x,bl.y);
 
	cv::Mat warp_mat = cv::getAffineTransform(srcTri,dstTri);
	cv::Size s;
	s.width=widthOut;
	s.height=heightOut;
	cv::warpAffine(src,dst,warp_mat,s);
}

void WarpAlpha(uint8_t* pixelsOut,int widthOut,int heightOut,const uint8_t* pixelsIn,int widthIn,int heightIn,const M23& transform) {
	//int tx = 0; // Shift 100 pixels to the right
	//int ty = 0;  // Shift 50 pixels down
	// Create the translation matrix
	//float cosa=cosf(DEGREES_TO_RADIANS(angle));
	//float sina=sinf(DEGREES_TO_RADIANS(angle));

	//cosa*=scale;
	//sina*=scale;

	cv::Mat translation_matrix=(cv::Mat_<double>(2, 3)<<transform.m[0][0],transform.m[1][0],transform.m[2][0],transform.m[0][1],transform.m[1][1],transform.m[2][1]);

	//cv::Mat translation_matrix=(cv::Mat_<double>(2, 3)<<cosa,-sina,trans.x,sina,cosa,trans.y);
	//cv::Mat translation_matrix1=(cv::Mat_<double>(2, 3)<<1,0,trans.x,0,1,trans.y);
	cv::Mat img=cv::Mat(heightIn,widthIn,CV_8UC1,(char*)pixelsIn);
	cv::Size s;
	s.width=widthOut;
	s.height=heightOut;
	cv::Mat imgOut=cv::Mat(heightOut,widthOut,CV_8UC1,(char*)pixelsOut);
	cv::warpAffine(img,imgOut,translation_matrix,s);
}


void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const M23& transform) {
	//int tx = 0; // Shift 100 pixels to the right
	//int ty = 0;  // Shift 50 pixels down
	// Create the translation matrix
	//float cosa=cosf(DEGREES_TO_RADIANS(angle));
	//float sina=sinf(DEGREES_TO_RADIANS(angle));

	//cosa*=scale;
	//sina*=scale;

	cv::Mat translation_matrix=(cv::Mat_<double>(2, 3)<<transform.m[0][0],transform.m[1][0],transform.m[2][0],transform.m[0][1],transform.m[1][1],transform.m[2][1]);

	//cv::Mat translation_matrix=(cv::Mat_<double>(2, 3)<<cosa,-sina,trans.x,sina,cosa,trans.y);
	//cv::Mat translation_matrix1=(cv::Mat_<double>(2, 3)<<1,0,trans.x,0,1,trans.y);
	cv::Mat img=cv::Mat(heightIn,widthIn,CV_8UC3,(char*)pixelsInRGB);
	cv::Size s;
	s.width=widthOut;
	s.height=heightOut;
	cv::Mat imgOut=cv::Mat(heightOut,widthOut,CV_8UC3,(char*)pixelsOutRGB);
	cv::warpAffine(img,imgOut,translation_matrix,s);
}
/*
void WarpRGB(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const V2& trans,float angle,float scale) {
	//int tx = 0; // Shift 100 pixels to the right
	//int ty = 0;  // Shift 50 pixels down
	// Create the translation matrix
	float cosa=cosf(DEGREES_TO_RADIANS(angle));
	float sina=sinf(DEGREES_TO_RADIANS(angle));

	cosa*=scale;
	sina*=scale;

	cv::Mat translation_matrix=(cv::Mat_<double>(2, 3)<<cosa,-sina,trans.x,sina,cosa,trans.y);
	cv::Mat translation_matrix1=(cv::Mat_<double>(2, 3)<<1,0,trans.x,0,1,trans.y);
	cv::Mat img=cv::Mat(heightIn,widthIn,CV_8UC3,(char*)pixelsInRGB);
	cv::Size s;
	s.width=widthOut;
	s.height=heightOut;
	cv::Mat imgOut=cv::Mat(heightOut,widthOut,CV_8UC3,(char*)pixelsOutRGB);
	cv::warpAffine(img,imgOut,translation_matrix,s);
}
void WarpRGB1(uint8_t* pixelsOutRGB,int widthOut,int heightOut,const uint8_t* pixelsInRGB,int widthIn,int heightIn,const V2& center,float angle,float scale) {


	cv::Point2f center2f(center.x,center.y);
	auto M=cv::getRotationMatrix2D(center2f,angle,scale);
	//auto MI=M.inv();
	cv::Mat img=cv::Mat(heightIn,widthIn,CV_8UC3,(char*)pixelsInRGB);
	cv::Size s;
	s.width=widthOut;
	s.height=heightOut;
	cv::Mat imgOut=cv::Mat(heightOut,widthOut,CV_8UC3,(char*)pixelsOutRGB);
	cv::warpAffine(img,imgOut,M,s);
}
*/

