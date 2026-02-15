#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>	// OutputDebugString
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>      // std::ofstream
#include <map>
#include <chrono>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_glfw.h"

#define STB_IMAGE_WRITE_STATIC
#define STBI_UNUSED_SYMBOLS
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "shared/stb_image.h"

#include"shared/file.h"
#include"shared/output.h"
#include"shared/math.h"
#include"shared/time.h"
#include"shared/queue.h"
#include"shared/std_ext.h"

#include "wrapcv/wrapcv.h"

inline uint32_t DistSquared(uint32_t v0,uint32_t v1) {
	if(v0==0xffffffff || v1==0xffffffff)
		return 0xffffffff;
	int16_t x0=(v0>>16)&0xffff;
	int16_t y0=v0&0xffff;
	int16_t x1=(v1>>16)&0xffff;
	int16_t y1=v1&0xffff;
	int16_t x=x1-x0;
	int16_t y=y1-y0;
	return (x*x)+(y*y);
}
inline uint32_t GetPixel(int x,int y,const uint32_t* pixels,int width,int height) {
	if(x<0 || x>width-1)
		return 0xffffffff;
	if(y<0 || y>height-1)
		return 0xffffffff;
	return pixels[y*width+x];
}

void draw_line(int stx,int sty,int ex,int ey,unsigned char* pixels,int width,int height,uint8_t color) {
	// Calculate absolute differences
	int dx=(ex>stx) ? (ex-stx) : (stx-ex);
	int dy=(ey>sty) ? (ey-sty) : (sty-ey);
	// Determine direction of movement
	int sx=(stx<ex) ? 1 : -1;
	int sy=(sty<ey) ? 1 : -1;
	// Initial error term
	int err=dx-dy;
	while(1){
		// Bounds checking: only draw if the pixel is within the buffer
		if(stx>=0&&stx<width&&sty>=0&&sty<height){
			pixels[sty*width+stx]=color;
		}
		// Break if we've reached the end point
		if(stx==ex&&sty==ey) break;
		int e2=2*err;
		// Adjust x coordinate
		if(e2>-dy){
			err-=dy;
			stx+=sx;
		}
		// Adjust y coordinate
		if(e2<dx){
			err+=dx;
			sty+=sy;
		}
	}
}

inline ImVec4 ImLoad(const V4& v){
	return ImVec4(v.x,v.y,v.z,v.w);
}
inline ImVec2 ImLoad(const V2& v){
	return ImVec2(v.x,v.y);
}
inline V2 VLoad(const ImVec2& v){
	return V2(v.x,v.y);
}

class GuiLog {
	public:
		enum PRIMITIVE_TYPE {
			NA=0,
			TEXT=1
		};
		struct Primitive {
			Primitive() : m_type(NA) {}
			Primitive(PRIMITIVE_TYPE type) : m_type(type){}
			PRIMITIVE_TYPE m_type;
			uint32_t m_color=0;
			std::string m_text;
		};
		void InitLog();
		void DrawLog();
		void AddText(const char* text);
		std::mutex m_lock;
		std::vector<Primitive> m_primitives;
		int m_maxSize=0;
		int m_tail = 0;
};

void GuiLog::AddText(const char* text) {
	const std::lock_guard<std::mutex> lock(m_lock);
	const int size = int(m_primitives.size());
	Primitive* primitive = nullptr;
	if(size < m_maxSize || m_maxSize < 1) {
		m_primitives.emplace_back();
		primitive = &m_primitives.back();
	}
	else {
		primitive = &m_primitives[m_tail];
	}
	if(m_maxSize > 0) {
		m_tail = (m_tail + 1) % m_maxSize;
	}
	primitive->m_type=TEXT;
	uint32_t color=0xc0c0c0ff;
	if(strstr(text,"ERROR")) {
		color=0xdc3545ff;
	}else
	if(strstr(text,"WARNING")) {
		color=0xffc107ff;
	}else
	if(strstr(text,"NOTIFY")) {
		color=0x007bffff;
	}
	primitive->m_color=color;
	primitive->m_text=text;
}

void GuiLog::InitLog() {
	m_primitives.reserve(m_maxSize);
}

void GuiLog::DrawLog() {
	const std::lock_guard<std::mutex> lock(m_lock);
	const int numPrimitives = (int)m_primitives.size();
	for(int i=0;i!=numPrimitives;i++) {
		const int index = (m_tail + i) % numPrimitives;
		const Primitive* primitive = &m_primitives[index];
		switch(primitive->m_type) {
			case TEXT: {
				int color=primitive->m_color;
				ImGui::TextColored(ImLoad(uint322V4(color)),"%s",primitive->m_text.c_str());
				if(primitive->m_text.rfind('\n')==std::string::npos)
					ImGui::SameLine(0,0);
				break;
			}
			default:
				FATAL("Viewer::DrawLog");
		}
	}
	if(ImGui::GetScrollY()==ImGui::GetScrollMaxY()) {
		ImGui::SetScrollHereY(1.0f);
	}
	static V2 popupPosition;
	if(ImGui::IsWindowHovered() && ImGui::IsMouseReleased(1)) {
		popupPosition=VLoad(ImGui::GetMousePos());
		ImGui::OpenPopup("LogPopup");
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(4,4));
	ImGui::SetNextWindowPos(ImLoad(popupPosition));
	if(ImGui::BeginPopup("LogPopup",ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar)) {
		if(!ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
			ImGui::CloseCurrentPopup();
		}
		if(ImGui::Button("Clear log")) {
			m_primitives.clear();
			m_tail = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(1);
}
		//const char* input="D:/slet/tank-unit-preview.png";
		//const char* input="D:/slet/elijah-ekdahl-nt69AC1bSdg-unsplash-s.jpg";
		//const char* input="D:/slet/structures_crop.png";
		//const char* input="D:/slet/D:/slet/ashley-byrd-lywKTBRDV3I-unsplash.jpg";		//Food
		//const char* input="D:/slet/averie-woodard-4nulm-JUYFo-unsplash-s.jpg";		//Girl



class Viewer {
	public:
		void End();
		void Begin();
		void Run();
		void DrawProfilerDisplay(const std::vector<DisplayTimer>& profilerDisplay);
		
		void Draw(const V2& imagePos);
		std::atomic<bool> m_close=false;
		GuiLog m_log;
		float m_fps=0;
		V2 m_imagePos;
		int m_numberResults=0;

		void VTracerUpdateLoop();
	protected:
		struct Settings {
			bool m_splines=true;
			float m_cornerAngle=60.0f;
			float m_spliceAngle=45.0f;
			float m_lengthThreshold=4.0f;
			int m_deepenDiff=16;
			int m_mergeThreshold=8;
			std::string m_filename;//="D:/slet/structures_crop.png";
		};
		struct JobSettings : public Settings {
			int m_type;
		};
		FixedQueueMT<JobSettings,2> m_queue;

		std::mutex m_profilerLock;
		Profiler m_profiler;
		std::vector<DisplayTimer> m_profilerDisplay;
		int m_numberClusters=0;
		
		std::atomic<bool> m_restart={false};
		std::atomic<bool> m_pause={false};

		std::vector<uint8_t> m_pixelsRgb;
		std::vector<uint8_t> m_pixelsLoaded;

		std::mutex m_pixelsDisplayRgbLock;
		std::vector<uint8_t> m_pixelsDisplayRgb;

		int m_width=0;
		int m_height=0;
		std::atomic<bool> m_closing={false};
		std::thread m_updateThread;

		float m_progress=0.0f;
		Settings m_settingsGui;
};

static void glfw_error_callback(int error,const char* description){
	uprintf("Glfw Error %d: %s\n",error,description);
}

void LineBB(ImVec2& top_left,ImVec2& bottom_right){
	ImGuiWindow* window=ImGui::GetCurrentWindow();
	ImGuiContext& g=*GImGui;
	const ImVec2 size_arg(-1.0f,0);
	ImVec2 pos=window->DC.CursorPos;
	ImVec2 sz=ImGui::CalcItemSize(size_arg,ImGui::CalcItemWidth(),g.FontSize);
	ImRect bb(pos,ImVec2(pos.x+sz.x,pos.y+sz.y));
	top_left=bb.Min;
	bottom_right=bb.Max;
}
void LineMultiRect(float* positions,ImU32* colors,int count){
	ImGuiWindow* window=ImGui::GetCurrentWindow();
	if(window->SkipItems)
		return;
	ImGuiContext& g=*GImGui;
	const ImGuiStyle& style=g.Style;
	const ImVec2 size_arg(-1.0f,0);
	ImVec2 pos=window->DC.CursorPos;
	ImVec2 sz=ImGui::CalcItemSize(size_arg,ImGui::CalcItemWidth(),g.FontSize);
	ImRect bb(pos,ImVec2(pos.x+sz.x,pos.y+sz.y));
	ImGui::ItemSize(bb,style.FramePadding.y);
	if(!ImGui::ItemAdd(bb,0))
		return;
	ImGui::RenderFrame(bb.Min,bb.Max,ImGui::GetColorU32(ImGuiCol_FrameBg),false,0.0f);
	bb.Expand(ImVec2(-style.FrameBorderSize,-style.FrameBorderSize));
	for(int i=0;i!=count;i++){
		ImRect bb1=bb;
		bb1.Min.x=ImMin(bb.Min.x+positions[i*2+0],bb.Max.x);
		bb1.Max.x=ImMin(bb.Min.x+positions[i*2+1],bb.Max.x);
		ImGui::RenderFrame(bb1.Min,bb1.Max,colors[i],false,0.0f);
	}
}
void Viewer::Begin() {
	m_log.InitLog();
}
void Viewer::End() {
}

void Viewer::DrawProfilerDisplay(const std::vector<DisplayTimer>& profilerDisplay){
	for(int i=0;i!=(int)profilerDisplay.size();i++){
		ImGui::Text("%s","");
		ImGui::SameLine(20.0f+(float)profilerDisplay[i].m_depth*20.0f);
		ImGui::Text("%s",profilerDisplay[i].m_name.c_str());
		ImGui::SameLine(270);
		ImGui::Text("%d",profilerDisplay[i].m_count);
		ImGui::SameLine(290+40);
		ImGui::Text("%.2f",profilerDisplay[i].m_time);
		ImGui::SameLine(360+40);
		ImGui::SameLine(380+40);
		ImVec2 topLeft;
		ImVec2 bottomRight;
		LineBB(topLeft,bottomRight);
		float width=bottomRight.x-topLeft.x;
		int count=0;
		float positions[64*2];
		uint32_t colors[64];
		for(int j=0;j!=(int)profilerDisplay[i].m_bars.size();j++){
			positions[count*2+0]=profilerDisplay[i].m_bars[j].m_start*width;
			positions[count*2+1]=profilerDisplay[i].m_bars[j].m_end*width;
			if(positions[count*2+0]==positions[count*2+1]){
				positions[count*2+1]+=1;
			}
			colors[count++]=profilerDisplay[i].m_bars[j].m_color;
			if(count==countof(colors))
				break;
		}
		LineMultiRect(positions,colors,count);
	}
}


#include "vtracer.h"

//Helper: Convert internal PointF to OpenCV Point
cv::Point ToCvPoint(PointF p) {
	return cv::Point((int)std::round(p.x),(int)std::round(p.y));
}

// Helper: Convert internal Color to OpenCV Scalar (BGR)
cv::Scalar ToCvColor(Color c) {
	// OpenCV uses BGR by default
	return cv::Scalar(c.b,c.g,c.r,255);
}

bool ScaleImageToArray(uint8_t* pixelsOut,int widthOut,int heightOut,const cv::Mat& imageIn,float scaleFactor) {
	if(!pixelsOut||imageIn.empty()||widthOut<=0||heightOut<=0||scaleFactor<=0.0f){
		return false;
	}

	// 1. Ensure the input image is 8-bit (CV_8U)
	cv::Mat img8;
	int depth=imageIn.depth();
	if(depth!=CV_8U){
		double maxVal=(depth==CV_16U) ? 255.0/65535.0 : 255.0;
		imageIn.convertTo(img8,CV_8U,maxVal);
	} else{
		img8=imageIn;
	}

	// 2. Convert colorspace to RGB (3 channels)
	cv::Mat rgb;
	if(img8.channels()==1){
		cv::cvtColor(img8,rgb,cv::COLOR_GRAY2RGB);
	} else if(img8.channels()==3){
		cv::cvtColor(img8,rgb,cv::COLOR_BGR2RGB); // OpenCV defaults to BGR
	} else if(img8.channels()==4){
		cv::cvtColor(img8,rgb,cv::COLOR_BGRA2RGB); // Strip alpha channel
	} else{
		return false; // Unsupported number of channels
	}

	// 3. Apply uniform scaling
	cv::Mat scaledRgb;
	if(scaleFactor==1.0f){
		scaledRgb=rgb;
	} else{
		// Calculate new dimensions, ensuring they are at least 1x1
		int new_width=std::max(1,static_cast<int>(std::round(rgb.cols*scaleFactor)));
		int new_height=std::max(1,static_cast<int>(std::round(rgb.rows*scaleFactor)));

		// Optimize interpolation based on scale direction
		int interpolation=(scaleFactor<1.0f) ? cv::INTER_AREA : cv::INTER_LINEAR;
		cv::resize(rgb,scaledRgb,cv::Size(new_width,new_height),0,0,interpolation);
	}

	// 4. Wrap the pre-allocated output buffer as a 3-channel matrix (CV_8UC3)
	cv::Mat dst(heightOut,widthOut,CV_8UC3,pixelsOut);

	// 5. Initialize the destination with black padding (R=0, G=0, B=0)
	dst=cv::Scalar(0,0,0);

	// 6. Calculate the bounding box for Crop/Extend based on the NEW scaled dimensions
	int crop_w=std::min(widthOut,scaledRgb.cols);
	int crop_h=std::min(heightOut,scaledRgb.rows);

	// 7. Copy the intersecting region
	if(crop_w>0&&crop_h>0){
		cv::Mat roi_src=scaledRgb(cv::Rect(0,0,crop_w,crop_h));
		cv::Mat roi_dst=dst(cv::Rect(0,0,crop_w,crop_h));
		roi_src.copyTo(roi_dst);
	}

	return true;
}

bool DrawShapesOpenCV(BuilderImpl* b, uint8_t* pixelsOut, int widthOut, int heightOut, float scaleFactor) {
    if (!b || global_result_shapes.empty() || scaleFactor <= 0.0f) return false;

    // 1. Create Canvas at SCALED resolution
    int scaled_w = std::max(1, static_cast<int>(std::round(b->width * scaleFactor)));
    int scaled_h = std::max(1, static_cast<int>(std::round(b->height * scaleFactor)));
    cv::Mat canvas(scaled_h, scaled_w, CV_8UC3, cv::Scalar(255, 255, 255));

    // Sub-pixel precision for flawless vector rendering (1/256th of a pixel)
    const int shift = 8;
    const float multiplier = scaleFactor * static_cast<float>(1 << shift);

    auto toSubPixel = [multiplier](double x, double y) {
        return cv::Point(static_cast<int>(std::round(x * multiplier)), 
                         static_cast<int>(std::round(y * multiplier)));
    };

    // 2. Draw Shapes
    for (const auto& shape : global_result_shapes) {
        cv::Scalar color = ToCvColor(shape.color);

        // Collect ALL contours for this specific shape to properly render holes
        std::vector<std::vector<cv::Point>> all_contours;

        for (const auto& prim : shape.primitives) {
            if (prim.points.empty()) continue;

            std::vector<cv::Point> cv_pts;

            if (prim.is_spline) {
                cv_pts.reserve(prim.points.size() * 15);
                cv_pts.push_back(toSubPixel(prim.points[0].x, prim.points[0].y));
                
                size_t k = 1;
                while (k + 2 < prim.points.size()) {
                    PointF p0 = (k > 1) ? prim.points[k-1] : prim.points[0];
                    PointF p1 = prim.points[k];
                    PointF p2 = prim.points[k+1];
                    PointF p3 = prim.points[k+2];

                    double dist = std::hypot(p3.x - p0.x, p3.y - p0.y) * scaleFactor;
                    int num_steps = std::max(10, static_cast<int>(std::ceil(dist / 2.0)));

                    for (int step = 1; step <= num_steps; ++step) {
                        double t = static_cast<double>(step) / num_steps;
                        double t1 = 1.0 - t;
                        
                        double c0 = t1 * t1 * t1;
                        double c1 = 3 * t * t1 * t1;
                        double c2 = 3 * t * t * t1;
                        double c3 = t * t * t;

                        double x = c0 * p0.x + c1 * p1.x + c2 * p2.x + c3 * p3.x;
                        double y = c0 * p0.y + c1 * p1.y + c2 * p2.y + c3 * p3.y;
                        
                        cv_pts.push_back(toSubPixel(x, y));
                    }
                    k += 3;
                }
            } else {
                cv_pts.reserve(prim.points.size());
                for (const auto& p : prim.points) {
                    cv_pts.push_back(toSubPixel(p.x, p.y));
                }
            }

            if (!cv_pts.empty()) {
                all_contours.push_back(std::move(cv_pts));
            }
        }

        // DRAW THE ENTIRE SHAPE AT ONCE
        if (!all_contours.empty()) {
            std::vector<const cv::Point*> ppt;
            std::vector<int> npt;
            
            ppt.reserve(all_contours.size());
            npt.reserve(all_contours.size());

            for (const auto& contour : all_contours) {
                ppt.push_back(contour.data());
                npt.push_back(static_cast<int>(contour.size()));
            }

            // 1. Fill the shape (respecting holes via Even-Odd/Winding rules)
            cv::fillPoly(canvas, ppt.data(), npt.data(), static_cast<int>(all_contours.size()), color, cv::LINE_AA, shift);
            
            // 2. Stroke the shape outline to fix anti-aliasing gaps / thinning
            // A thickness of 1 adds 0.5 pixels of outward weight, matching SVG behavior.
            cv::polylines(canvas, all_contours, true, color, 1, cv::LINE_AA, shift);
        }
    }

    // 3. Finalize and Copy to Output Buffer
    return ScaleImageToArray(pixelsOut, widthOut, heightOut, canvas, 1.0f);
}

void Viewer::VTracerUpdateLoop(){
	m_profiler.m_rangeMilliseconds=1000;

	while(!m_closing) {
		//uint64_t t0=GetTimeEpochMicroseconds();
		JobSettings settings;
		if(!m_queue.Pop(10000,[&](JobSettings* ps)->void {	//Get job from queue or time out
			if(!ps)
				return;		//Closing					//Close signal
			settings=*ps;
		})){
			//uprintf("timeout\n");
			continue;
		};
		if(m_closing)
			break;
		auto time=std::chrono::system_clock::now();
		//loadok=true;
		m_progress=0.0f;
		START_TIMER(convertTimer,&m_profiler,"Load and convert",0x802faf);

		bool diagonal=false;
		uint32_t hierarchical=0xFFFFFFFF;	// UINT32_MAX
		int32_t keying_action=1;			// 1 = Discard (Stacked behavior)
		int32_t mode=settings.m_splines?1:0;			// Spline

		BatchConfig batch_conf;
		batch_conf.mode=mode;
		batch_conf.corner_threshold=DEGREES_TO_RADIANS(settings.m_cornerAngle);//60.0*(PI/180.0); // Convert deg to rad
		batch_conf.length_threshold=settings.m_lengthThreshold;	//4
		batch_conf.max_iterations=10;
		batch_conf.splice_threshold=DEGREES_TO_RADIANS(settings.m_spliceAngle);//=45.0*(PI/180.0); // Convert deg to rad

		std::string imageName=GetFileNameRemap(settings.m_filename.c_str());
#if 1
		START_TIMER(loadTimer,&m_profiler,"Load",0x2f80af);
		int w,h,c;
		uint8_t* pixels=stbi_load(imageName.c_str(),&w,&h,&c,4);
		END_TIMER(loadTimer,&m_profiler);
		if(pixels) {
			BuilderImpl* b=cpp_create_builder(pixels,(uint32_t)w,(uint32_t)h);
			stbi_image_free(pixels);
			m_progress=0.4f;
			START_TIMER(keyTimer,&m_profiler,"Key",0x6fffaf);
			cpp_key_image(b);
			cpp_set_builder_config(b,diagonal,hierarchical,keying_action);
			END_TIMER(keyTimer,&m_profiler);
			START_TIMER(clusterTimer,&m_profiler,"Clusters",0xf32faf);
			cpp_run_clustering(b,settings.m_deepenDiff,settings.m_mergeThreshold);
			END_TIMER(clusterTimer,&m_profiler);
			START_TIMER(vectorizeTimer,&m_profiler,"Vectorize",0x6f2f4f);
			cpp_batch_convert(b,batch_conf);
			END_TIMER(vectorizeTimer,&m_profiler);

			m_progress=0.8f;
			START_TIMER(renderTimer,&m_profiler,"Render",0x6f3f4f);
			float scaleX=(float)m_width/(float)b->width;
			float scaleY=(float)m_height/(float)b->height;
			float scale=MIN(scaleY,scaleX);
			DrawShapesOpenCV(b,m_pixelsRgb.data(),m_width,m_height,scale);
			END_TIMER(renderTimer,&m_profiler);

			START_TIMER(copyTimer,&m_profiler,"Copy",0xafef2f);
			m_pixelsDisplayRgbLock.lock();
			memcpy(m_pixelsDisplayRgb.data(),m_pixelsRgb.data(),m_pixelsRgb.size());
			m_pixelsDisplayRgbLock.unlock();
			END_TIMER(copyTimer,&m_profiler);
			cpp_destroy_builder(b);
		}
		m_progress=1.0f;

#else
		int32_t precision=2;
		const char* output="D:/slet/output_cpp.svg";
		int32_t result=cpp_convert_file_to_svg(input,output,diagonal,hierarchical,keying_action,batch_conf,precision);
		if(result==1){
			uprintf("Success! Saved to %s\n",output);
		} else{
			uprintf("Conversion failed\n");
		}
#endif
		m_numberResults=cpp_get_result_count();
		END_TIMER(convertTimer,&m_profiler);
		//std::this_thread::sleep_until(time+std::chrono::microseconds(1000000/10));		//limit frame rate
		//std::this_thread::sleep_for(std::chrono::microseconds(1000000));
		//uint64_t t1=GetTimeEpochMicroseconds();
		m_profilerLock.lock();
		m_profilerDisplay.clear();
		m_profiler.GetDisplayTimers(m_profilerDisplay);
		m_profilerLock.unlock();
	}
}

void ScaleImageBilinear(uint8_t* pixelsOut,int widthOut,int heightOut,const uint8_t* pixelsIn,int widthIn,int heightIn) {
	// Safety checks
	if(!pixelsOut||!pixelsIn||widthOut<=0||heightOut<=0||widthIn<=0||heightIn<=0)
		return;

	// Calculate scaling ratios
	// We use (widthIn - 1) to ensure the last pixel maps exactly to the last pixel
	float xRatio=(float)(widthIn-1)/(float)widthOut;
	float yRatio=(float)(heightIn-1)/(float)heightOut;

	for(int y=0; y<heightOut; ++y){
		// Calculate the Y coordinate in the source image
		float srcY=y*yRatio;
		int y0=(int)srcY;
		int y1=y0+1;

		// Clamp Y to bounds (handling floating point edge cases)
		if(y1>=heightIn) y1=heightIn-1;

		// Vertical interpolation weight
		float yDiff=srcY-(float)y0;
		float yInv=1.0f-yDiff;

		// Pre-calculate row pointers for efficiency
		const uint8_t* row0=pixelsIn+(y0*widthIn);
		const uint8_t* row1=pixelsIn+(y1*widthIn);
		uint8_t* rowOut=pixelsOut+(y*widthOut);

		for(int x=0; x<widthOut; ++x){
			// Calculate the X coordinate in the source image
			float srcX=x*xRatio;
			int x0=(int)srcX;
			int x1=x0+1;

			// Clamp X to bounds
			if(x1>=widthIn) x1=widthIn-1;

			// Horizontal interpolation weight
			float xDiff=srcX-(float)x0;
			float xInv=1.0f-xDiff;

			// Fetch the four neighboring pixels
			// A(x0,y0)  B(x1,y0)
			// C(x0,y1)  D(x1,y1)
			float a=(float)row0[x0];
			float b=(float)row0[x1];
			float c=(float)row1[x0];
			float d=(float)row1[x1];

			// Interpolate Top Row (A -> B) and Bottom Row (C -> D)
			float top=(a*xInv)+(b*xDiff);
			float bot=(c*xInv)+(d*xDiff);

			// Interpolate vertically between Top and Bottom
			float finalVal=(top*yInv)+(bot*yDiff);

			// Write output
			rowOut[x]=(uint8_t)finalVal;
		}
	}
}

void Viewer::Run(){
	glfwSetErrorCallback(glfw_error_callback);
	if(!glfwInit()){
		uprintf("glfwInit failed\n");
		return;
	}

	glfwWindowHint(GLFW_CLIENT_API,GLFW_OPENGL_ES_API);
	const char* glsl_version="#version 300 es";//#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);

	GLFWwindow* window=glfwCreateWindow(2200,1200,"VTracer CPP",NULL,NULL);
	if(!window){
		uprintf("Unable to create window\n");
		return;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	bool err=glewInit()!=GLEW_OK;
	if(err){
		FATAL("Failed to initialize OpenGL loader!");
		return;
	}
	uprintf("glsl_version %s\n",glsl_version);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io=ImGui::GetIO(); (void)io;
	io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;           // Enable Docking
	io.ConfigFlags|=ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

	ImGui::StyleColorsDark();

	ImGuiStyle& style=ImGui::GetStyle();
	if(io.ConfigFlags&ImGuiConfigFlags_ViewportsEnable){
		style.WindowRounding=0.0f;
		style.Colors[ImGuiCol_WindowBg].w=1.0f;
	}

	ImGui_ImplGlfw_InitForOpenGL(window,true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	io.Fonts->AddFontFromFileTTF(GetFileNameRemap("$(DATA)/fonts/inconsolata/InconsolataGo-Regular.ttf").c_str(),20.0f);

	std::mutex profilerDisplayLock;
	std::vector<DisplayTimer> profilerDisplay;

	m_width=1024;
	m_height=1024;

	m_pixelsRgb.resize(m_width*m_height*3);
	m_pixelsDisplayRgb.resize(m_width*m_height*3);
	std::vector<uint8_t> pixelsDisplayRgb;
	pixelsDisplayRgb.resize(m_width*m_height*3);

	GLuint m_textureId=0;
	glGenTextures(1,&m_textureId);
	glBindTexture(GL_TEXTURE_2D,m_textureId);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP); 
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP); 
	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,m_width,m_height,0,GL_RGB,GL_UNSIGNED_BYTE,m_pixelsDisplayRgb.data());

	int tick=0;

	m_updateThread=std::thread([&](){
		VTracerUpdateLoop();
	});

	while(!glfwWindowShouldClose(window)){
		auto time=std::chrono::system_clock::now();
		uint64_t t0=GetTimeEpochMicroseconds();

		Profiler profiler;
		profiler.m_rangeMilliseconds=100;
		START_TIMER(mainTimer,&profiler,"main",0xffff20);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGuiID dockspace_id=ImGui::GetID("EditDockSpace");
		if(!tick||ImGui::DockBuilderGetNode(dockspace_id)==NULL){
			ImGui::DockBuilderRemoveNode(dockspace_id);
			ImVec2 dockspace_size=ImGui::GetMainViewport()->Size;
			ImGui::DockBuilderAddNode(dockspace_id,ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace_id,dockspace_size);
			ImGuiID dock_video_id=dockspace_id;
			ImGuiID dock_prop_id=ImGui::DockBuilderSplitNode(dock_video_id,ImGuiDir_Left,0.75f,NULL,&dock_video_id);
			ImGuiID dock_status_id=ImGui::DockBuilderSplitNode(dock_video_id,ImGuiDir_Down,0.15f,NULL,&dock_video_id);
			ImGui::DockBuilderDockWindow("Frame",dock_prop_id);
			ImGui::DockBuilderDockWindow("Settings",dock_video_id);
			ImGui::DockBuilderDockWindow("Log",dock_status_id);
			ImGui::DockBuilderFinish(dockspace_id);
		}
		ImGuiViewport* viewport=ImGui::GetMainViewport();
		const ImGuiWindowClass* window_class=0;
		ImVec2 p=viewport->Pos;
		ImVec2 s=viewport->Size;

		ImGui::SetNextWindowPos(p);
		ImGui::SetNextWindowSize(s);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowBgAlpha(0);

		ImGuiDockNodeFlags dockspace_flags=0;
		ImGuiWindowFlags host_window_flags=0;
		host_window_flags|=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBackground;
		host_window_flags|=ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus|ImGuiWindowFlags_MenuBar;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0.0f,0.0f));
		ImGui::Begin("MainWindow",NULL,host_window_flags);
		ImGui::DockSpace(dockspace_id,ImVec2(0.0f,0.0f),dockspace_flags,window_class);
		ImGui::PopStyleVar(3);

		ImGui::Begin("Frame",0,ImGuiWindowFlags_HorizontalScrollbar);//ImGuiWindowFlags_AlwaysVerticalScrollbar|ImGuiWindowFlags_AlwaysHorizontalScrollbar);

		V2 mouse=VLoad(ImGui::GetMousePos());
		V2 imageSize=V2((float)m_width,(float)m_height);

		float displayTexturScale=1.0f;

		imageSize*=displayTexturScale;

		V2 imageTopLeft=VLoad(ImGui::GetCursorScreenPos());
		V2 imageMousePos=mouse-imageTopLeft;

		V2 imagePos=imageMousePos/displayTexturScale;
		imagePos.x=MIN((float)m_width,imagePos.x);
		imagePos.x=MAX((float)0,imagePos.x);
		imagePos.y=MIN((float)m_height,imagePos.y);
		imagePos.y=MAX((float)0,imagePos.y);
		m_imagePos=imagePos;

		//int px=(int)imagePos.x;
		//int py=(int)imagePos.y;

		START_TIMER(updateTextureTimer,&profiler,"Texture update",0x208f7f);

		m_pixelsDisplayRgbLock.lock();
		memcpy(pixelsDisplayRgb.data(),m_pixelsDisplayRgb.data(),pixelsDisplayRgb.size());
		m_pixelsDisplayRgbLock.unlock();

		glBindTexture(GL_TEXTURE_2D,m_textureId);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,m_width,m_height,0,GL_RGB,GL_UNSIGNED_BYTE,pixelsDisplayRgb.data());

		END_TIMER(updateTextureTimer,&profiler);

		ImDrawList* dl=ImGui::GetWindowDrawList();
		ImGui::Dummy(ImLoad(imageSize));
		dl->AddImage((ImTextureID)(uint64_t)m_textureId,ImLoad(imageTopLeft),ImLoad(imageTopLeft+imageSize),ImVec2(0,0),ImVec2(1,1),0xffffffff);
		dl->PushClipRect(ImLoad(imageTopLeft),ImLoad(imageTopLeft+imageSize),true);
		dl->PopClipRect();
		ImGui::End();
		ImGui::Begin("Settings");
		ImGui::Text("FPS windows %.2f",m_fps);

		ImGui::SeparatorText("Settings");
		ImGui::Checkbox("Splines",&m_settingsGui.m_splines);
		ImGui::SliderFloat("Corner angle",&m_settingsGui.m_cornerAngle,0.0f,180.0f,"%.1f%%");
		ImGui::SliderFloat("Splice angle",&m_settingsGui.m_spliceAngle,0.0f,90.0f,"%.1f%%");
		ImGui::SliderFloat("Length threshold",&m_settingsGui.m_lengthThreshold,0.0f,10.0f,"%.1fpx");
		ImGui::SliderInt("Deepen difference",&m_settingsGui.m_deepenDiff,4,32);
		ImGui::SliderInt("Merge threshold",&m_settingsGui.m_mergeThreshold,1,256);

		std::string path="$(DATA)";

		std::vector<std::string> files;
		GetDirectoryFiles(&files,0,path.c_str());

		static int index=-1;
		std::vector<const char*> items;
		for(const std::string& filename:files) {
			items.push_back(filename.c_str());
		}
		if(index==-1) {
			index=0;
			m_settingsGui.m_filename=path+"/"+files[index];
		}
		if(ImGui::Combo("Files",&index,&items[0],(int)items.size())) {
			m_settingsGui.m_filename=path+"/"+files[index];
		}

		if(ImGui::Button("Run")) {
			JobSettings settings;
			m_queue.Push([&](JobSettings* ps)->void {
				ps->m_type=0;
				*((Settings*)ps)=m_settingsGui;
			});
		}

		ImGui::SeparatorText("Results");
		ImGui::ProgressBar(m_progress);
		if(m_progress==1.0f) {
			ImGui::Text("Resulting shapes %d",m_numberResults);
		}

		ImGui::SeparatorText("Profiler");

		DrawProfilerDisplay(profilerDisplay);
		m_profilerLock.lock();
		DrawProfilerDisplay(m_profilerDisplay);
		m_profilerLock.unlock();

		ImGui::End();
		ImGui::Begin("Log");
		m_log.DrawLog();
		ImGui::End();
		ImGui::End();
		ImGui::Render();

		int windowWidth,windowHeight;
		glfwGetWindowSize(window,&windowWidth,&windowHeight);

		glViewport(0,0,windowWidth,windowHeight);
		glClearColor(.1,.1,.1,1);
		glClear(GL_COLOR_BUFFER_BIT);

		END_TIMER(mainTimer,&profiler);
		profilerDisplay.clear();
		profilerDisplayLock.lock();
		profiler.GetDisplayTimers(profilerDisplay);
		profilerDisplayLock.unlock();

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if(io.ConfigFlags&ImGuiConfigFlags_ViewportsEnable){
			GLFWwindow* backup_current_context=glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}
		if(glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS)
			glfwSetWindowShouldClose(window,true);
		glfwPollEvents();

		glfwSwapBuffers(window);
		//std::this_thread::sleep_until(time+std::chrono::microseconds(1000000/60));		//limit frame rate
		uint64_t t1=GetTimeEpochMicroseconds();
		m_fps=(float)(1000000L/(t1-t0));

		tick++;
	}
	m_closing=true;
	m_queue.Close();
	m_updateThread.join();

	if(m_textureId)
		glDeleteTextures(1,&m_textureId);
	m_textureId=0;

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
}

Viewer* g_viewer=0;

void PrintCallback(const char* str) {
#ifdef _WIN32
	OutputDebugString(str);
#else
	::printf("%s",str);
#endif
	if(g_viewer)
		g_viewer->m_log.AddText(str);
}

int main(){
	SetPrintCallback(PrintCallback);
#ifdef CMAKE_SOURCE_DIR
	AddFilePathRemap("$(DATA)",std::string(CMAKE_SOURCE_DIR)+"/data");
#else
	AddFilePathRemap("$(DATA)",GetExecutablePath()+"/data");
#endif
	Viewer d;
	d.Begin();
	g_viewer=&d;
	std::thread tr=std::thread([&](){
	});
	d.Run();
	d.m_close=true;
	tr.join();
	d.End();
	g_viewer=0;
	return 0;
}

#ifdef _WIN32
#undef APIENTRY
#include<windows.h>
#include"debugapi.h"
#include<crtdbg.h>
int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,PSTR lpCmdLine,INT nCmdShow){
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(167);
	return main();
}
#endif
