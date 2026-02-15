#pragma once

#include <vector>
#include <string>
#include <stdint.h>

class BuilderImpl;

struct BatchConfig {
    int32_t mode; // 0=Polygon, 1=Spline (Matches PathSimplifyMode)
    double corner_threshold;
    double length_threshold;
    int32_t max_iterations;
    double splice_threshold;
};

BuilderImpl* cpp_execute_pipeline_from_file(const char* filename, bool diagonal, uint32_t hierarchical, int32_t keying_action, BatchConfig conf);
int32_t cpp_convert_file_to_svg(const char* input_path, const char* output_path, bool diagonal, uint32_t hierarchical, int32_t keying_action, BatchConfig conf, int32_t precision);
BuilderImpl* cpp_create_builder(uint8_t* p,uint32_t w,uint32_t h);
void cpp_destroy_builder(BuilderImpl* b);
void cpp_key_image(BuilderImpl* b);
void cpp_set_builder_config(BuilderImpl* b,bool diagonal,uint32_t hierarchical,int32_t keying_action_int);
void cpp_run_clustering(BuilderImpl* b,int merge_threshold,int32_t deepen_diff);
void cpp_batch_convert(BuilderImpl* b,BatchConfig conf);
uint32_t cpp_get_result_count();

#ifdef _WIN32
#pragma warning( disable : 4267 )
#endif

// =============================================================
// 1. DATA STRUCTURES
// =============================================================

using ClusterIndex=uint32_t;
const ClusterIndex ZERO=0;

struct Color{
	uint8_t r,g,b,a;
	bool operator==(const Color& other) const {
		return r==other.r&&g==other.g&&b==other.b&&a==other.a;
	}
};

enum class KeyingAction{ Keep,Discard };

struct ColorSum{
	uint32_t r=0,g=0,b=0,a=0,counter=0;
	void add(Color c) { r+=c.r; g+=c.g; b+=c.b; a+=c.a; counter++; }
	void merge(const ColorSum& o) { r+=o.r; g+=o.g; b+=o.b; a+=o.a; counter+=o.counter; }
	void clear() { r=0; g=0; b=0; a=0; counter=0; }
	Color average() const {
		if(counter==0) return {0, 0, 0, 0};
		return {(uint8_t)(r/counter), (uint8_t)(g/counter), (uint8_t)(b/counter), (uint8_t)(a/counter)};
	}
};

struct BoundingRect{
	int32_t left=0,top=0,right=0,bottom=0;
	bool is_empty() const { return (right-left)==0&&(bottom-top)==0; }
	void add_x_y(int32_t x,int32_t y) {
		if(is_empty()){ left=x; right=x+1; top=y; bottom=y+1; return; }
		if(x<left) left=x; else if(x+1>right) right=x+1;
		if(y<top) top=y; else if(y+1>bottom) bottom=y+1;
	}
	void merge(const BoundingRect& o) {
		if(o.is_empty()) return;
		if(is_empty()){ *this=o; return; }
		left=std::min(left,o.left); right=std::max(right,o.right);
		top=std::min(top,o.top); bottom=std::max(bottom,o.bottom);
	}
	void clear() { left=0; top=0; right=0; bottom=0; }
};

struct Cluster{
	std::vector<uint32_t> indices;
	std::vector<uint32_t> holes;
	uint32_t num_holes=0;
	uint32_t depth=0;
	ColorSum sum;
	ColorSum residue_sum;
	BoundingRect rect;
	ClusterIndex merged_into=0;

	size_t area() const { return indices.size(); }
	Color color() const { return sum.average(); }
	void add(uint32_t i,Color c,int32_t x,int32_t y) {
		indices.push_back(i); sum.add(c); rect.add_x_y(x,y);
	}
};

struct Area{ size_t area; size_t count; };

// =============================================================
// 2. BUILDER CLASS
// =============================================================

class BuilderImpl{
	public:
	bool diagonal;
	uint32_t hierarchical;
	uint32_t batch_size;
	Color key;
	KeyingAction keying_action;
	bool has_key;
	uint32_t width;
	uint32_t height;
	std::vector<uint8_t> pixels;
	std::vector<Cluster> clusters;
	std::vector<ClusterIndex> cluster_indices;
	std::vector<Area> cluster_areas;
	std::vector<ClusterIndex> clusters_output;
	uint32_t iteration;
	ClusterIndex next_index;

	bool pixel_at(int32_t x,int32_t y,Color* out) const;
	bool almost_same(bool h1,Color c1,bool h2,Color c2,int32_t t) const;
	void combine_clusters(ClusterIndex f,ClusterIndex t);
	std::vector<ClusterIndex> neighbours_internal(const Cluster& c) const;
	uint32_t perimeter_internal(const Cluster& c) const;
	void clustering(int merge_threshold=8);
	void stage_2_sync_parity(int32_t dd);
	void print_cluster_info_sums() const;
};

// =============================================================
// 5. BATCH CONVERSION (The "Cluster Loop")
// =============================================================
struct PointF{ double x,y; };

struct CPrimitive{
	std::vector<PointF> points;
	bool is_spline;
};

struct CShapeResult{
	Color color;
	std::vector<CPrimitive> primitives;
};

extern std::vector<CShapeResult> global_result_shapes;