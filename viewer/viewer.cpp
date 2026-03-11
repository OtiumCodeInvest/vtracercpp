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
#include <array>
#include <algorithm>

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
#include "vtracer.h"
#include "earcut.hpp" // Required for GPU Vector Triangulation

// --- Vector Rendering Structures ---
struct Vec2 { float x, y; };
struct VectorVertex { float x, y, edgeDist; };

struct PackedShapeDef {
	int coreStart, coreCount;
	struct SkirtDef { int start, count; };
	std::vector<SkirtDef> skirts;
	float r, g, b, a;
};

// --- Vector Shaders ---
const char* vectorVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in float aEdgeDist;

out float EdgeDist;
uniform vec2 uResolution; // This is now the screen canvas size
uniform vec2 uPan;
uniform float uZoom;

void main() {
    // 1. Mathematically apply pan and zoom to the raw vector coordinate
    vec2 pixelPos = (aPos * uZoom) + uPan;

    // 2. Map the transformed coordinate into the canvas clip space
    vec2 clipSpace = vec2((pixelPos.x / uResolution.x) * 2.0 - 1.0, 1.0 - (pixelPos.y / uResolution.y) * 2.0);
    gl_Position = vec4(clipSpace, 0.0, 1.0);
    EdgeDist = aEdgeDist;
}
)";

const char* vectorFragmentShaderSource = R"(
#version 330 core
in float EdgeDist;
out vec4 FragColor;

uniform vec4 polyColor;
uniform float uZoom;

void main() {
    // Ensure the AA skirt stays exactly 1 screen pixel wide even when zooming in infinitely
    float adjustedDist = EdgeDist * max(uZoom, 1.0);
    float alpha = clamp(1.0 - adjustedDist, 0.0, 1.0) * polyColor.a;
    
    FragColor = vec4(polyColor.rgb, alpha);
}
)";

// --- Helper Functions ---
inline uint32_t DistSquared(uint32_t v0,uint32_t v1) {
	if(v0==0xffffffff || v1==0xffffffff) return 0xffffffff;
	int16_t x0=(v0>>16)&0xffff; int16_t y0=v0&0xffff;
	int16_t x1=(v1>>16)&0xffff; int16_t y1=v1&0xffff;
	int16_t x=x1-x0; int16_t y=y1-y0;
	return (x*x)+(y*y);
}

inline ImVec4 ImLoad(const V4& v){ return ImVec4(v.x,v.y,v.z,v.w); }
inline ImVec2 ImLoad(const V2& v){ return ImVec2(v.x,v.y); }
inline V2 VLoad(const ImVec2& v){ return V2(v.x,v.y); }

class GuiLog {
	public:
		enum PRIMITIVE_TYPE { NA=0, TEXT=1 };
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
	} else {
		primitive = &m_primitives[m_tail];
	}
	if(m_maxSize > 0) m_tail = (m_tail + 1) % m_maxSize;
	
	primitive->m_type=TEXT;
	uint32_t color=0xc0c0c0ff;
	if(strstr(text,"ERROR")) color=0xdc3545ff;
	else if(strstr(text,"WARNING")) color=0xffc107ff;
	else if(strstr(text,"NOTIFY")) color=0x007bffff;
	primitive->m_color=color;
	primitive->m_text=text;
}

void GuiLog::InitLog() { m_primitives.reserve(m_maxSize); }

void GuiLog::DrawLog() {
	const std::lock_guard<std::mutex> lock(m_lock);
	const int numPrimitives = (int)m_primitives.size();
	for(int i=0;i!=numPrimitives;i++) {
		const int index = (m_tail + i) % numPrimitives;
		const Primitive* primitive = &m_primitives[index];
		if(primitive->m_type == TEXT) {
			int color=primitive->m_color;
			ImGui::TextColored(ImLoad(uint322V4(color)),"%s",primitive->m_text.c_str());
			if(primitive->m_text.rfind('\n')==std::string::npos) ImGui::SameLine(0,0);
		}
	}
	if(ImGui::GetScrollY()==ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
	static V2 popupPosition;
	if(ImGui::IsWindowHovered() && ImGui::IsMouseReleased(1)) {
		popupPosition=VLoad(ImGui::GetMousePos());
		ImGui::OpenPopup("LogPopup");
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(4,4));
	ImGui::SetNextWindowPos(ImLoad(popupPosition));
	if(ImGui::BeginPopup("LogPopup",ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar)) {
		if(!ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) ImGui::CloseCurrentPopup();
		if(ImGui::Button("Clear log")) {
			m_primitives.clear();
			m_tail = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(1);
}

class Viewer {
	public:
		void End();
		void Begin();
		void Run();
		void DrawProfilerDisplay(const std::vector<DisplayTimer>& profilerDisplay);
		
		std::atomic<bool> m_close=false;
		GuiLog m_log;
		float m_fps=0;
		V2 m_imagePos;
		int m_numberResults=0;

		// --- View / Camera State ---
		float m_zoom = 1.0f;
		V2 m_pan = {0.0f, 0.0f};

		void VTracerUpdateLoop();
		
	protected:
		struct Settings {
			bool m_splines=true;
			float m_cornerAngle=60.0f;
			float m_spliceAngle=45.0f;
			float m_lengthThreshold=4.0f;
			int m_deepenDiff=16;
			int m_mergeThreshold=8;
			std::string m_filename;
		};
		struct JobSettings : public Settings { int m_type; };
		FixedQueueMT<JobSettings,2> m_queue;

		std::mutex m_profilerLock;
		Profiler m_profiler;
		std::vector<DisplayTimer> m_profilerDisplay;
		int m_numberClusters=0;
		
		std::atomic<bool> m_restart={false};
		std::atomic<bool> m_pause={false};

		// --- Shared Vector Render State ---
		std::mutex m_vectorRenderLock;
		bool m_vectorDataReady = false;
		std::vector<VectorVertex> m_sharedCoreVertices;
		std::vector<VectorVertex> m_sharedSkirtVertices;
		std::vector<PackedShapeDef> m_sharedShapeDefs;

		std::atomic<bool> m_closing={false};
		std::thread m_updateThread;

		float m_progress=0.0f;
		Settings m_settingsGui;

		// --- Vector Geometry Generation ---
		Vec2 CalculateMiter(Vec2 prev, Vec2 curr, Vec2 next, float thickness, bool isCW) {
			Vec2 d1 = {curr.x - prev.x, curr.y - prev.y};
			float len1 = std::sqrt(d1.x * d1.x + d1.y * d1.y);
			if (len1 > 0.0001f) { d1.x /= len1; d1.y /= len1; }

			Vec2 d2 = {next.x - curr.x, next.y - curr.y};
			float len2 = std::sqrt(d2.x * d2.x + d2.y * d2.y);
			if (len2 > 0.0001f) { d2.x /= len2; d2.y /= len2; }

			Vec2 n1 = isCW ? Vec2{d1.y, -d1.x} : Vec2{-d1.y, d1.x};
			Vec2 n2 = isCW ? Vec2{d2.y, -d2.x} : Vec2{-d2.y, d2.x};

			Vec2 n_avg = {n1.x + n2.x, n1.y + n2.y};
			float len_avg = std::sqrt(n_avg.x * n_avg.x + n_avg.y * n_avg.y);
			
			if (len_avg < 0.001f) return {curr.x + n1.x * thickness, curr.y + n1.y * thickness};
			n_avg.x /= len_avg; n_avg.y /= len_avg;

			float d = n_avg.x * n1.x + n_avg.y * n1.y;
			if (d < 0.1f) d = 0.1f; 
			
			float miter_length = thickness / d;
			return {curr.x + n_avg.x * miter_length, curr.y + n_avg.y * miter_length};
		}
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
	if(window->SkipItems) return;
	ImGuiContext& g=*GImGui;
	const ImGuiStyle& style=g.Style;
	const ImVec2 size_arg(-1.0f,0);
	ImVec2 pos=window->DC.CursorPos;
	ImVec2 sz=ImGui::CalcItemSize(size_arg,ImGui::CalcItemWidth(),g.FontSize);
	ImRect bb(pos,ImVec2(pos.x+sz.x,pos.y+sz.y));
	ImGui::ItemSize(bb,style.FramePadding.y);
	if(!ImGui::ItemAdd(bb,0)) return;
	ImGui::RenderFrame(bb.Min,bb.Max,ImGui::GetColorU32(ImGuiCol_FrameBg),false,0.0f);
	bb.Expand(ImVec2(-style.FrameBorderSize,-style.FrameBorderSize));
	for(int i=0;i!=count;i++){
		ImRect bb1=bb;
		bb1.Min.x=ImMin(bb.Min.x+positions[i*2+0],bb.Max.x);
		bb1.Max.x=ImMin(bb.Min.x+positions[i*2+1],bb.Max.x);
		ImGui::RenderFrame(bb1.Min,bb1.Max,colors[i],false,0.0f);
	}
}

void Viewer::Begin() { m_log.InitLog(); }
void Viewer::End() {}

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
		ImVec2 topLeft, bottomRight;
		LineBB(topLeft,bottomRight);
		float width=bottomRight.x-topLeft.x;
		int count=0;
		float positions[64*2];
		uint32_t colors[64];
		for(int j=0;j!=(int)profilerDisplay[i].m_bars.size();j++){
			positions[count*2+0]=profilerDisplay[i].m_bars[j].m_start*width;
			positions[count*2+1]=profilerDisplay[i].m_bars[j].m_end*width;
			if(positions[count*2+0]==positions[count*2+1]) positions[count*2+1]+=1;
			colors[count++]=profilerDisplay[i].m_bars[j].m_color;
			if(count==countof(colors)) break;
		}
		LineMultiRect(positions,colors,count);
	}
}

void Viewer::VTracerUpdateLoop() {
	m_profiler.m_rangeMilliseconds=1000;

	while(!m_closing) {
		JobSettings settings;
		if(!m_queue.Pop(10000,[&](JobSettings* ps)->void {
			if(!ps) return;
			settings=*ps;
		})){
			continue;
		};
		if(m_closing) break;
		auto time=std::chrono::system_clock::now();
		m_progress=0.0f;
		START_TIMER(convertTimer,&m_profiler,"Load and convert",0x802faf);

		bool diagonal=false;
		uint32_t hierarchical=0xFFFFFFFF;
		int32_t keying_action=1;
		int32_t mode=settings.m_splines?1:0;

		BatchConfig batch_conf;
		batch_conf.mode=mode;
		batch_conf.corner_threshold=DEGREES_TO_RADIANS(settings.m_cornerAngle);
		batch_conf.length_threshold=settings.m_lengthThreshold;
		batch_conf.max_iterations=10;
		batch_conf.splice_threshold=DEGREES_TO_RADIANS(settings.m_spliceAngle);

		std::string imageName=GetFileNameRemap(settings.m_filename.c_str());

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

			// ==========================================
			// CPU GL Geometry Generation
			// ==========================================
			START_TIMER(renderTimer,&m_profiler,"Geometry Setup",0x6f3f4f);
			
			std::vector<VectorVertex> localCoreVertices;
			std::vector<VectorVertex> localSkirtVertices;
			std::vector<PackedShapeDef> localShapeDefs;

			for (const auto& shape : global_result_shapes) {
				PackedShapeDef pack;
				pack.r = shape.color.r / 255.0f;
				pack.g = shape.color.g / 255.0f;
				pack.b = shape.color.b / 255.0f;
				pack.a = 1.0f; 
				
				using Point = std::array<float, 2>;
				std::vector<std::vector<Point>> earcutPolygon;
				std::vector<Vec2> flatVertices; 
				
				for (const auto& prim : shape.primitives) {
					if (prim.points.empty()) continue;
					std::vector<Vec2> contourPts;

					if (prim.is_spline) {
						size_t k = 1;
						contourPts.push_back({(float)prim.points[0].x, (float)prim.points[0].y});
						while (k + 2 < prim.points.size()) {
							PointF p0 = (k > 1) ? prim.points[k-1] : prim.points[0];
							PointF p1 = prim.points[k];
							PointF p2 = prim.points[k+1];
							PointF p3 = prim.points[k+2];

							double dist = std::hypot(p3.x - p0.x, p3.y - p0.y);
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
								contourPts.push_back({(float)x, (float)y});
							}
							k += 3;
						}
					} else {
						for (const auto& p : prim.points) contourPts.push_back({(float)p.x, (float)p.y});
					}

					// --- NEW FIX: Remove duplicate closing points ---
					// If the path is explicitly closed, remove the last point so our 
					// modulo math doesn't calculate a zero-length edge at the seam.
					if (contourPts.size() > 1) {
						Vec2 first = contourPts.front();
						Vec2 last = contourPts.back();
						float distSq = (first.x - last.x) * (first.x - last.x) + (first.y - last.y) * (first.y - last.y);
						if (distSq < 0.001f) {
							contourPts.pop_back();
						}
					}

					// We still need at least 3 points to make a valid polygon
					if(contourPts.size() < 3) continue;

					// Earcut prep
					std::vector<Point> earcutContour;
					for(auto& p : contourPts) {
						earcutContour.push_back({p.x, p.y});
						flatVertices.push_back(p);
					}
					earcutPolygon.push_back(earcutContour);

					float area = 0;
					for(int i=0; i < (int)contourPts.size(); ++i) {
						int j = (i+1) % contourPts.size();
						area += (contourPts[i].x * contourPts[j].y - contourPts[j].x * contourPts[i].y);
					}
					bool isCW = (area > 0.0f); 

					PackedShapeDef::SkirtDef sdef;
					sdef.start = (int)localSkirtVertices.size();
					float thickness = 1.0f;
					int count = contourPts.size();
					for (int i = 0; i <= count; i++) {
						int idx = i % count; 
						Vec2 prev = contourPts[(idx - 1 + count) % count];
						Vec2 curr = contourPts[idx];
						Vec2 next = contourPts[(idx + 1) % count];
						
						Vec2 outer = CalculateMiter(prev, curr, next, thickness, isCW);
						localSkirtVertices.push_back({curr.x, curr.y, 0.0f});
						localSkirtVertices.push_back({outer.x, outer.y, 1.0f});
					}
					sdef.count = (int)localSkirtVertices.size() - sdef.start;
					pack.skirts.push_back(sdef);
				}

				if (earcutPolygon.empty()) continue;

				std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(earcutPolygon);

				pack.coreStart = (int)localCoreVertices.size();
				for (int i = 0; i < (int)indices.size(); i++) {
					Vec2 pt = flatVertices[indices[i]];
					localCoreVertices.push_back({pt.x, pt.y, 0.0f}); 
				}
				pack.coreCount = (int)localCoreVertices.size() - pack.coreStart;
				
				localShapeDefs.push_back(pack);
			}
			END_TIMER(renderTimer,&m_profiler);

			// Transfer to Main Thread for GPU Upload
			START_TIMER(copyTimer,&m_profiler,"Lock Data",0xafef2f);
			m_vectorRenderLock.lock();
			m_sharedCoreVertices = std::move(localCoreVertices);
			m_sharedSkirtVertices = std::move(localSkirtVertices);
			m_sharedShapeDefs = std::move(localShapeDefs);
			m_vectorDataReady = true;
			
			// Reset view settings when a new image is loaded
			m_zoom = 1.0f;
			m_pan = V2(0.0f, 0.0f);
			
			m_vectorRenderLock.unlock();
			END_TIMER(copyTimer,&m_profiler);
			
			cpp_destroy_builder(b);
		}
		m_progress=1.0f;
		m_numberResults=cpp_get_result_count();
		END_TIMER(convertTimer,&m_profiler);

		m_profilerLock.lock();
		m_profilerDisplay.clear();
		m_profiler.GetDisplayTimers(m_profilerDisplay);
		m_profilerLock.unlock();
	}
}

void Viewer::Run(){
	glfwSetErrorCallback(glfw_error_callback);
	if(!glfwInit()){
		uprintf("glfwInit failed\n");
		return;
	}
	
#if __APPLE__
    const char* glsl_version="#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, true);
#else
    const char* glsl_version="#version 330 core";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

	GLFWwindow* window=glfwCreateWindow(2200,1200,"VTracer CPP + GPU Infinite Scaling",NULL,NULL);
	if(!window){
		uprintf("Unable to create window\n");
		return;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); 

	glewExperimental = GL_TRUE;
	bool err=glewInit()!=GLEW_OK;
	if(err){
		FATAL("Failed to initialize OpenGL loader!");
		return;
	}
	glGetError(); 

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io=ImGui::GetIO(); (void)io;
	io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags|=ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();

	ImGuiStyle& style=ImGui::GetStyle();
	if(io.ConfigFlags&ImGuiConfigFlags_ViewportsEnable){
		style.WindowRounding=0.0f;
		style.Colors[ImGuiCol_WindowBg].w=1.0f;
	}

	ImGui_ImplGlfw_InitForOpenGL(window,true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	io.Fonts->AddFontFromFileTTF(GetFileNameRemap("$(DATA)/fonts/inconsolata/InconsolataGo-Regular.ttf").c_str(),20.0f);

	// ==========================================
	// GPU Shader Setup
	// ==========================================
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vectorVertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &vectorFragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

	GLint resLoc = glGetUniformLocation(shaderProgram, "uResolution");
	GLint colorLoc = glGetUniformLocation(shaderProgram, "polyColor");
	GLint panLoc = glGetUniformLocation(shaderProgram, "uPan");
	GLint zoomLoc = glGetUniformLocation(shaderProgram, "uZoom");

	// FBO and Texture Setup
	GLuint vectorFbo, vectorFboTex;
	glGenFramebuffers(1, &vectorFbo);
	glGenTextures(1, &vectorFboTex);
	glBindTexture(GL_TEXTURE_2D, vectorFboTex);
	// We will dynamically resize this later based on the ImGui canvas size
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1024, 1024, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	
	glBindFramebuffer(GL_FRAMEBUFFER, vectorFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vectorFboTex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// VBO / VAO Setup
	GLuint coreVao, coreVbo, skirtVao, skirtVbo;
	glGenVertexArrays(1, &coreVao); glGenBuffers(1, &coreVbo);
	glGenVertexArrays(1, &skirtVao); glGenBuffers(1, &skirtVbo);

	auto configureVAO = [](GLuint vao, GLuint vbo) {
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VectorVertex), (void*)0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(VectorVertex), (void*)(2 * sizeof(float)));
		glEnableVertexAttribArray(1);
	};
	configureVAO(coreVao, coreVbo);
	configureVAO(skirtVao, skirtVbo);

	std::mutex profilerDisplayLock;
	std::vector<DisplayTimer> profilerDisplay;

	int tick=0;
	m_updateThread=std::thread([&](){ VTracerUpdateLoop(); });

	std::vector<PackedShapeDef> renderShapes;

	int fboWidth = 1024;
	int fboHeight = 1024;

	while(!glfwWindowShouldClose(window)){
		auto time=std::chrono::system_clock::now();
		uint64_t t0=GetTimeEpochMicroseconds();

		Profiler profiler;
		profiler.m_rangeMilliseconds=100;
		START_TIMER(mainTimer,&profiler,"main",0xffff20);

		// ==========================================
		// UI Setup & Canvas Calculation
		// ==========================================
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

		// Initialize the Canvas window inside ImGui
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Frame", 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		
		ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
		ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
		if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
		if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
		ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

		ImGui::InvisibleButton("canvas", canvas_sz);
		bool is_hovered = ImGui::IsItemHovered();

		// Handle Panning
		if (is_hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
			m_pan.x += io.MouseDelta.x;
			m_pan.y += io.MouseDelta.y;
		}

		// Handle Zooming to mouse position
		if (is_hovered && io.MouseWheel != 0.0f) {
			float relX = io.MousePos.x - canvas_p0.x - m_pan.x;
			float relY = io.MousePos.y - canvas_p0.y - m_pan.y;

			float newZoom = m_zoom * powf(1.1f, io.MouseWheel);
			if (newZoom < 0.01f) newZoom = 0.01f;
			if (newZoom > 1000.0f) newZoom = 1000.0f; 

			float zoomRatio = newZoom / m_zoom;
			m_zoom = newZoom;

			m_pan.x -= relX * (zoomRatio - 1.0f);
			m_pan.y -= relY * (zoomRatio - 1.0f);
		}


		// ==========================================
		// Dynamic GPU Rendering
		// ==========================================
		START_TIMER(updateTextureTimer,&profiler,"GPU Infinite Render",0x208f7f);

		m_vectorRenderLock.lock();
		if (m_vectorDataReady) {
			renderShapes = m_sharedShapeDefs;

			glBindBuffer(GL_ARRAY_BUFFER, coreVbo);
			glBufferData(GL_ARRAY_BUFFER, (int)(m_sharedCoreVertices.size() * sizeof(VectorVertex)), m_sharedCoreVertices.data(), GL_STATIC_DRAW);

			glBindBuffer(GL_ARRAY_BUFFER, skirtVbo);
			glBufferData(GL_ARRAY_BUFFER, (int)(m_sharedSkirtVertices.size() * sizeof(VectorVertex)), m_sharedSkirtVertices.data(), GL_STATIC_DRAW);

			m_vectorDataReady = false;
		}
		m_vectorRenderLock.unlock();

		// Update FBO Size if the ImGui window resizes
		if (fboWidth != (int)canvas_sz.x || fboHeight != (int)canvas_sz.y) {
			fboWidth = (int)canvas_sz.x;
			fboHeight = (int)canvas_sz.y;
			glBindTexture(GL_TEXTURE_2D, vectorFboTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fboWidth, fboHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		}

		// Draw Vector Graphics Directly into FBO matching exactly the Canvas resolution
		glBindFramebuffer(GL_FRAMEBUFFER, vectorFbo);
		glViewport(0, 0, fboWidth, fboHeight);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Dark background
		glClear(GL_COLOR_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		glUseProgram(shaderProgram);
		
		// Send the Canvas Resolution and the Pan/Zoom coordinates to the shader
		glUniform2f(resLoc, (float)fboWidth, (float)fboHeight);
		glUniform2f(panLoc, m_pan.x, m_pan.y);
		glUniform1f(zoomLoc, m_zoom);

		for(const auto& shape : renderShapes) {
			glUniform4f(colorLoc, shape.r, shape.g, shape.b, shape.a);
			
			glBindVertexArray(coreVao);
			glDrawArrays(GL_TRIANGLES, shape.coreStart, shape.coreCount);

			glBindVertexArray(skirtVao);
			for(const auto& skirt : shape.skirts) {
				glDrawArrays(GL_TRIANGLE_STRIP, skirt.start, skirt.count);
			}
		}
		
		glDisable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		END_TIMER(updateTextureTimer,&profiler);


		// Draw the newly generated FBO exactly over the Canvas
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddImage((ImTextureID)(uint64_t)vectorFboTex, canvas_p0, canvas_p1, ImVec2(0, 1), ImVec2(1, 0), 0xffffffff);
		
		ImGui::End(); // End Frame Window
		ImGui::PopStyleVar();

		// ==========================================
		// Side Panels
		// ==========================================
		ImGui::Begin("Settings");
		ImGui::Text("FPS windows %.2f",m_fps);
		ImGui::Text("Zoom: %.2fx", m_zoom);

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
		if(index==-1 && files.size() > 0) {
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
		uint64_t t1=GetTimeEpochMicroseconds();
		m_fps=(float)(1000000L/(t1-t0));

		tick++;
	}
	m_closing=true;
	m_queue.Close();
	m_updateThread.join();

	if(vectorFboTex) glDeleteTextures(1,&vectorFboTex);
	if(vectorFbo) glDeleteFramebuffers(1,&vectorFbo);

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
	if(g_viewer) g_viewer->m_log.AddText(str);
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
	std::thread tr=std::thread([&](){});
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
	return main();
}
#endif