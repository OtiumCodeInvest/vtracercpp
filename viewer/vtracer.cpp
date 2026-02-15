#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <set>
#include <queue>

#define STBI_UNUSED_SYMBOLS
//#define STB_IMAGE_IMPLEMENTATION
#include "shared/stb_image.h"

#include "vtracer.h"


// --- Implementations ---
bool BuilderImpl::pixel_at(int32_t x,int32_t y,Color* out) const {
	if(x<0||y<0||(uint32_t)x>=width||(uint32_t)y>=height) return false;
	size_t idx=((uint32_t)y*width+(uint32_t)x)*4;
	out->r=pixels[idx]; out->g=pixels[idx+1]; out->b=pixels[idx+2]; out->a=pixels[idx+3];
	return true;
}
bool BuilderImpl::almost_same(bool h1,Color c1,bool h2,Color c2,int32_t t) const {
	if(h1&&h2) return (std::abs((int)c1.r-c2.r)+std::abs((int)c1.g-c2.g)+std::abs((int)c1.b-c2.b))<=t;
	return false;
}
void BuilderImpl::combine_clusters(ClusterIndex f_idx,ClusterIndex t_idx) {
	if(f_idx>=clusters.size()||t_idx>=clusters.size()) return;
	Cluster& f=clusters[f_idx]; Cluster& t=clusters[t_idx];
	for(uint32_t i : f.indices) cluster_indices[i]=t_idx;
	t.indices.insert(t.indices.end(),f.indices.begin(),f.indices.end()); f.indices.clear();
	t.sum.merge(f.sum); t.rect.merge(f.rect); f.sum.clear(); f.rect.clear();
}
std::vector<ClusterIndex> BuilderImpl::neighbours_internal(const Cluster& c) const {
	if(c.indices.empty()) return {};
	ClusterIndex me=cluster_indices[c.indices[0]];
	std::set<ClusterIndex> s;
	for(uint32_t i : c.indices){
		uint32_t x=i%width,y=i/width;
		if(y>0){ ClusterIndex n=cluster_indices[(y-1)*width+x]; if(n!=ZERO&&n!=me) s.insert(n); }
		if(y<height-1){ ClusterIndex n=cluster_indices[(y+1)*width+x]; if(n!=ZERO&&n!=me) s.insert(n); }
		if(x>0){ ClusterIndex n=cluster_indices[y*width+(x-1)]; if(n!=ZERO&&n!=me) s.insert(n); }
		if(x<width-1){ ClusterIndex n=cluster_indices[y*width+(x+1)]; if(n!=ZERO&&n!=me) s.insert(n); }
	}
	return std::vector<ClusterIndex>(s.begin(),s.end());
}
uint32_t BuilderImpl::perimeter_internal(const Cluster& c) const {
	if(c.indices.empty()) return 0;
	ClusterIndex me=cluster_indices[c.indices[0]];
	uint32_t p=0;
	for(uint32_t i : c.indices){
		uint32_t x=i%width,y=i/width;
		bool b=false;
		if((y>0&&cluster_indices[(y-1)*width+x]!=me)||y==0) b=true;
		else if((y<height-1&&cluster_indices[(y+1)*width+x]!=me)||y==height-1) b=true;
		else if((x>0&&cluster_indices[y*width+(x-1)]!=me)||x==0) b=true;
		else if((x<width-1&&cluster_indices[y*width+(x+1)]!=me)||x==width-1) b=true;
		if(b) p++;
	}
	return p;
}
void BuilderImpl::stage_2_sync_parity(int32_t deepen_diff) {
	if(hierarchical==0) return;
	for(auto& c:clusters){ c.residue_sum=c.sum; c.merged_into=0; }
	clusters_output.clear(); cluster_areas.clear();
	for(size_t i=1; i<clusters.size(); ++i){
		size_t a=clusters[i].area(); if(a==0) continue;
		bool f=false; for(auto& ea:cluster_areas) if(ea.area==a){ ea.count++; f=true; break; }
		if(!f) cluster_areas.push_back({a, 1});
	}
	std::sort(cluster_areas.begin(),cluster_areas.end(),[](const Area& a,const Area& b){ return a.area<b.area; });

	size_t ai=0;
	while(ai<cluster_areas.size()){
		size_t ca=cluster_areas[ai].area;
		if(cluster_areas[ai].count==0){ ai++; continue; }
		for(size_t cid=1; cid<clusters.size(); ++cid){
			if(clusters[cid].area()!=ca) continue;
			if(ca>hierarchical){ clusters_output.push_back((ClusterIndex)cid); continue; }
			auto nbs=neighbours_internal(clusters[cid]);
			if(!nbs.empty()){
				ClusterIndex bt=0; int64_t min_s=INT64_MAX; Color mc=clusters[cid].color();
				for(auto nid:nbs){
					Color nc=clusters[nid].color();
					int32_t diff=std::abs((int)mc.r-nc.r)+std::abs((int)mc.g-nc.g)+std::abs((int)mc.b-nc.b);
					int64_t s=((int64_t)diff*65535)+(int64_t)nid;
					if(s<min_s){ min_s=s; bt=nid; }
				}
				int32_t bd=(int32_t)(min_s/65535);
				bool deep=(hierarchical==UINT32_MAX)&&(perimeter_internal(clusters[cid])<clusters[cid].area())&&(bd>deepen_diff);
				bool hol=(nbs.size()<=1);
				if(deep) clusters_output.push_back((ClusterIndex)cid);

				size_t old_a=clusters[bt].area();
				for(auto& ea:cluster_areas) if(ea.area==old_a){ ea.count--; break; }

				if(!deep){
					clusters[bt].residue_sum.merge(clusters[cid].residue_sum);
					combine_clusters((ClusterIndex)cid,bt);
				} else{
					ColorSum s_sum=clusters[cid].sum; BoundingRect s_rect=clusters[cid].rect; auto s_ind=clusters[cid].indices;
					combine_clusters((ClusterIndex)cid,bt);
					clusters[cid].sum=s_sum; clusters[cid].rect=s_rect; clusters[cid].indices=s_ind;
					if(hol){
						clusters[bt].holes.insert(clusters[bt].holes.end(),clusters[cid].indices.begin(),clusters[cid].indices.end());
						clusters[bt].num_holes++;
					}
					clusters[cid].merged_into=bt; clusters[bt].depth++;
				}

				size_t new_a=clusters[bt].area();
				bool upd=false;
				for(auto& ea:cluster_areas) if(ea.area==new_a){ ea.count++; upd=true; break; }
				if(!upd){
					cluster_areas.push_back({new_a, 1});
					std::sort(cluster_areas.begin(),cluster_areas.end(),[](const Area& a,const Area& b){ return a.area<b.area; });
				}
			} else{ clusters_output.push_back((ClusterIndex)cid); }
		}
		ai++;
	}
}
void BuilderImpl::clustering(int merge_threshold) {
	int32_t len=(int)cluster_indices.size();
	int32_t th=merge_threshold;
	for(int32_t i=0; i<len; ++i){
		int32_t x=i%width,y=i/width;
		Color c,u,l,ul;
		bool hc=pixel_at(x,y,&c),hu=pixel_at(x,y-1,&u),hl=pixel_at(x-1,y,&l),hul=pixel_at(x-1,y-1,&ul);
		ClusterIndex cu=(y>0) ? cluster_indices[(y-1)*width+x] : ZERO;
		ClusterIndex cl=(x>0) ? cluster_indices[y*width+(x-1)] : ZERO;
		ClusterIndex cul=(x>0&&y>0) ? cluster_indices[(y-1)*width+(x-1)] : ZERO;
		if(cl!=cu&&almost_same(hl,l,hu,u,th)&&(diagonal||(almost_same(hc,c,hl,l,th)&&almost_same(hc,c,hu,u,th)))){
			if(clusters[cl].area()<=clusters[cu].area()){
				combine_clusters(cl,cu); if(cl==clusters.size()-1) clusters.pop_back(); cl=cu;
			} else{
				combine_clusters(cu,cl); if(cu==clusters.size()-1) clusters.pop_back(); cu=cl;
			}
		}
		if(has_key&&c==key&&keying_action==KeyingAction::Keep) clusters[ZERO].add((uint32_t)i,c,x,y);
		else if(almost_same(hc,c,hu,u,th)&&almost_same(hc,c,hul,ul,th)){ cluster_indices[i]=cu; clusters[cu].add((uint32_t)i,c,x,y); } else if(almost_same(hc,c,hl,l,th)&&almost_same(hc,c,hul,ul,th)){ cluster_indices[i]=cl; clusters[cl].add((uint32_t)i,c,x,y); } else if(diagonal&&almost_same(hc,c,hul,ul,th)){ cluster_indices[i]=cul; clusters[cul].add((uint32_t)i,c,x,y); } else{
			Cluster nc; nc.add((uint32_t)i,c,x,y);
			clusters.push_back(nc); cluster_indices[i]=(ClusterIndex)(clusters.size()-1);
		}
	}
	next_index=(ClusterIndex)clusters.size();
}
void BuilderImpl::print_cluster_info_sums() const { /*...*/ }



// =============================================================
// 3. MASK & GEOMETRY HELPERS
// =============================================================

std::vector<uint8_t> global_mask_buffer;
uint32_t global_mask_width=0;
uint32_t global_mask_height=0;

struct CSubCluster{
	int32_t rect_l,rect_t,rect_r,rect_b;
	std::vector<uint32_t> indices;
	uint32_t parent_width;
};
std::vector<CSubCluster> global_sub_clusters;
std::vector<CSubCluster> global_holes;

struct CBoundary{
	std::vector<uint8_t> pixels;
	uint32_t width;
	uint32_t height;
	int32_t off_x;
	int32_t off_y;
};
std::vector<CBoundary> global_boundaries;

// Buffer for passing points to Rust
struct Point{ int32_t x,y; };
std::vector<Point> global_path_buffer;

// Helper functions for geometry
inline int32_t SignedArea(Point p1,Point p2,Point p3) {
	return (p2.x-p1.x)*(p3.y-p1.y)-(p3.x-p1.x)*(p2.y-p1.y);
}

inline int32_t ManhattanDist(Point p1,Point p2) {
	return std::abs(p1.x-p2.x)+std::abs(p1.y-p2.y);
}

// Helper for penalty calculation (Area^2 / Base)
inline double GetDist(Point p1,Point p2) {
	return std::hypot(p1.x-p2.x,p1.y-p2.y);
}

inline double EvaluatePenalty(Point a,Point b,Point c) {
	double l1=GetDist(a,b);
	double l2=GetDist(b,c);
	double l3=GetDist(c,a);
	if(l3<1e-5) return 0.0;
	double s=(l1+l2+l3)/2.0;
	double area=std::sqrt(s*(s-l1)*(s-l2)*(s-l3));
	return (area*area)/l3;
}

// =============================================================
// 4. FFI EXPORTS
// =============================================================

struct CClusterMeta{
	uint32_t sum_r,sum_g,sum_b,sum_a,sum_count;
	uint32_t res_r,res_g,res_b,res_a,res_count;
	int32_t rect_l,rect_t,rect_r,rect_b;
	uint32_t num_holes;
	uint32_t depth;
	uint32_t merged_into;
};


std::vector<PointF> global_path_buffer_f;

// Math Helpers for Smoothing
inline double Norm(double x,double y) { return std::sqrt(x*x+y*y); }
inline double DistSq(PointF p1,PointF p2) { return (p1.x-p2.x)*(p1.x-p2.x)+(p1.y-p2.y)*(p1.y-p2.y); }
inline double Dist(PointF p1,PointF p2) { return std::sqrt(DistSq(p1,p2)); }

inline double Angle(PointF p) {
	// atan2 return -pi to pi.
	// Rust logic: if y<0 -> -acos(x), else acos(x).
	// But normalize() in Rust does x/norm, y/norm.
	// We stick to standard atan2 for robustness or replicate exactly if needed.
	// Replicating Rust 'angle' logic:
	if(p.y<0) return -std::acos(p.x);
	return std::acos(p.x);
}

inline double SignedAngleDiff(double a1,double a2) {
	const double PI=3.14159265358979323846;
	double v2=a2;
	if(a1>v2) v2+=2.0*PI;
	double diff=v2-a1;
	if(diff>PI) return diff-2.0*PI;
	return diff;
}

inline PointF Normalize(PointF p) {
	double n=Norm(p.x,p.y);
	if(n<1e-9) return {0,0};
	return {p.x/n,p.y/n};
}

std::vector<uint32_t> global_cut_indices;


// =============================================================
// VECTOR MATH HELPERS (Dense Format)
// =============================================================

inline PointF operator+(PointF a,PointF b) { return {a.x+b.x,a.y+b.y}; }
inline PointF operator-(PointF a,PointF b) { return {a.x-b.x,a.y-b.y}; }
inline PointF operator*(PointF a,double s) { return {a.x*s,a.y*s}; }
inline PointF operator/(PointF a,double s) { return {a.x/s,a.y/s}; }
inline double Dot(PointF a,PointF b) { return a.x*b.x+a.y*b.y; }
inline double LenSq(PointF a) { return a.x*a.x+a.y*a.y; }
inline double Len(PointF a) { return std::sqrt(LenSq(a)); }
inline PointF Unit(PointF a) { double l=Len(a); return l<1e-9 ? PointF{0,0} : a/l; }

// =============================================================
// SCHNEIDER'S ALGORITHM (Curve Fitting)
// =============================================================

std::vector<PointF> global_spline_buffer;

// Evaluate cubic bezier at parameter t
PointF EvalBezier(const PointF* cp,double t) {
	double t1=1.0-t;
	double t12=t1*t1; double t13=t12*t1;
	double t2=t*t; double t3=t2*t;
	return cp[0]*t13+cp[1]*(3.0*t*t12)+cp[2]*(3.0*t2*t1)+cp[3]*t3;
}

// Chord length parameterization
std::vector<double> Parameterize(const std::vector<PointF>& pts,int first,int last) {
	std::vector<double> u; u.reserve(last-first+1);
	u.push_back(0.0);
	for(int i=first+1;i<=last;++i){
		u.push_back(u.back()+Len(pts[i]-pts[i-1]));
	}
	double total=u.back();
	if(total<1e-9) return u;
	for(size_t i=0;i<u.size();++i) u[i]/=total;
	return u;
}

// Least-Squares fit of Bezier coefficients (alpha1, alpha2)
// cp[0] and cp[3] are fixed. We solve for cp[1] and cp[2] along tangents.
void GenerateBezier(const std::vector<PointF>& pts,int first,int last,const std::vector<double>& u,PointF tan1,PointF tan2,PointF* outCP) {
	PointF p0=pts[first];
	PointF p3=pts[last];

	// C(u) = p0*B0(u) + p1*B1(u) + p2*B2(u) + p3*B3(u)
	// p1 = p0 + alpha1*tan1
	// p2 = p3 + alpha2*tan2
	// Solve for alpha1, alpha2 via Least Squares

	double C[2][2]={{0,0},{0,0}};
	double X[2]={0,0};

	for(size_t i=0;i<u.size();++i){
		double t=u[i];
		double t1=1.0-t;
		double b0=t1*t1*t1;
		double b1=3.0*t*t1*t1;
		double b2=3.0*t*t*t1;
		double b3=t*t*t;

		PointF a1=tan1*b1;
		PointF a2=tan2*b2;
		PointF diff=pts[first+i]-(p0*b0)-(p0*b1)-(p3*b2)-(p3*b3);

		C[0][0]+=Dot(a1,a1); C[0][1]+=Dot(a1,a2);
		C[1][0]+=Dot(a1,a2); C[1][1]+=Dot(a2,a2);
		X[0]+=Dot(diff,a1);  X[1]+=Dot(diff,a2);
	}

	double det=C[0][0]*C[1][1]-C[1][0]*C[0][1];
	double alpha1,alpha2;

	if(std::abs(det)<1e-9){
		alpha1=alpha2=Dist(p0,p3)/3.0;
	} else{
		alpha1=(X[0]*C[1][1]-X[1]*C[0][1])/det;
		alpha2=(C[0][0]*X[1]-C[1][0]*X[0])/det;
	}

	// Safety checks
	double segLen=Dist(p0,p3);
	double epsilon=1e-6*segLen;
	if(alpha1<epsilon||alpha2<epsilon) alpha1=alpha2=segLen/3.0;

	outCP[0]=p0;
	outCP[1]=p0+tan1*alpha1;
	outCP[2]=p3+tan2*alpha2;
	outCP[3]=p3;
}

// Find max deviation
double ComputeMaxError(const std::vector<PointF>& pts,int first,int last,PointF* cp,const std::vector<double>& u,int* splitPoint) {
	double maxDistSq=0.0;
	*splitPoint=(last-first+1)/2;

	for(int i=first+1;i<last;++i){
		PointF P=EvalBezier(cp,u[i-first]);
		double d=LenSq(P-pts[i]);
		if(d>maxDistSq){
			maxDistSq=d;
			*splitPoint=i;
		}
	}
	return maxDistSq;
}

// Recursive Fitter
void FitCubic(const std::vector<PointF>& pts,int first,int last,PointF tan1,PointF tan2,double error) {
	// 2 points
	if(last-first==1){
		double dist=Dist(pts[first],pts[last])/3.0;
		global_spline_buffer.push_back(pts[first]+tan1*dist);
		global_spline_buffer.push_back(pts[last]+tan2*dist);
		global_spline_buffer.push_back(pts[last]);
		return;
	}

	std::vector<double> u=Parameterize(pts,first,last);
	PointF cp[4];
	GenerateBezier(pts,first,last,u,tan1,tan2,cp);

	int split;
	double maxErr=ComputeMaxError(pts,first,last,cp,u,&split);

	if(maxErr<error){
		// Output control points 1, 2, 3 (Point 0 is shared with previous)
		global_spline_buffer.push_back(cp[1]);
		global_spline_buffer.push_back(cp[2]);
		global_spline_buffer.push_back(cp[3]);
	} else{
		// Subdivide
		PointF centerTan=Unit(pts[split-1]-pts[split+1]);
		FitCubic(pts,first,split,tan1,centerTan,error);
		FitCubic(pts,split,last,centerTan*-1.0,tan2,error);
	}
}

std::vector<CShapeResult> global_result_shapes;


// =============================================================
// HELPERS FOR KEYING
// =============================================================

bool ColorExists(const BuilderImpl* b,Color c) {
	size_t len=b->pixels.size();
	for(size_t i=0; i<len; i+=4){
		if(b->pixels[i]==c.r&&b->pixels[i+1]==c.g&&b->pixels[i+2]==c.b) return true;
	}
	return false;
}

Color FindUnusedColor(const BuilderImpl* b) {
	Color candidates[]={
		{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
		{255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}
	};
	for(const auto& c:candidates){
		if(!ColorExists(b,c)) return c;
	}
	// Fallback: Try random colors
	for(int i=0; i<10; ++i){
		Color c={(uint8_t)(rand()%256), (uint8_t)(rand()%256), (uint8_t)(rand()%256), 255};
		if(!ColorExists(b,c)) return c;
	}
	return {0, 255, 0, 255}; // Final fallback
}

bool ShouldKey(const BuilderImpl* b) {
	if(b->width==0||b->height==0) return false;
	size_t threshold=(size_t)((b->width*2)*0.2);
	size_t transparent=0;

	// Check specific scanlines (matching Rust logic)
	int y_positions[]={0, (int)b->height/4, (int)b->height/2, 3*(int)b->height/4, (int)b->height-1};

	for(int y : y_positions){
		if(y<0||y>=(int)b->height) continue;
		for(uint32_t x=0; x<b->width; ++x){
			size_t idx=(y*b->width+x)*4;
			if(b->pixels[idx+3]==0){ // Alpha 0
				transparent++;
			}
			if(transparent>=threshold) return true;
		}
	}
	return false;
}

// =============================================================
// SVG WRITER HELPERS
// =============================================================

void AppendFloat(std::string& s,double v,int precision) {
	char buf[32];
	int len=snprintf(buf,sizeof(buf),"%.*f",precision,v);
	while(len>0&&buf[len-1]=='0') buf[--len]='\0';
	if(len>0&&buf[len-1]=='.') buf[--len]='\0';
	s+=buf;
}

void AppendColor(std::string& s,Color c) {
	char buf[16];
	snprintf(buf,sizeof(buf),"#%02X%02X%02X",c.r,c.g,c.b);
	s+=buf;
}

bool SaveSvgInternal(BuilderImpl* b,const char* filename,int precision) {
	if(!b||global_result_shapes.empty()) return false;
	FILE* f=fopen(filename,"w");
	if(!f) return false;

	fprintf(f,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(f,"<svg version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\" width=\"%u\" height=\"%u\">\n",b->width,b->height);

	std::string d;
	d.reserve(2048);

	for(const auto& shape:global_result_shapes){
		d.clear();
		for(const auto& prim:shape.primitives){
			if(prim.points.empty()) continue;

			d+="M";
			AppendFloat(d,prim.points[0].x,precision); d+=" ";
			AppendFloat(d,prim.points[0].y,precision); d+=" ";

			if(prim.is_spline){
				size_t k=1;
				while(k+2<prim.points.size()){
					d+="C";
					AppendFloat(d,prim.points[k].x,precision);   d+=" "; AppendFloat(d,prim.points[k].y,precision);   d+=" ";
					AppendFloat(d,prim.points[k+1].x,precision); d+=" "; AppendFloat(d,prim.points[k+1].y,precision); d+=" ";
					AppendFloat(d,prim.points[k+2].x,precision); d+=" "; AppendFloat(d,prim.points[k+2].y,precision); d+=" ";
					k+=3;
				}
			} else{
				for(size_t k=1; k<prim.points.size(); ++k){
					d+="L";
					AppendFloat(d,prim.points[k].x,precision); d+=" ";
					AppendFloat(d,prim.points[k].y,precision); d+=" ";
				}
			}
			d+="Z ";
		}
		if(!d.empty()){
			std::string c; AppendColor(c,shape.color);
			fprintf(f,"\t<path d=\"%s\" fill=\"%s\" />\n",d.c_str(),c.c_str());
		}
	}

	fprintf(f,"</svg>\n");
	fclose(f);
	return true;
}

//extern "C" {
	// Basic Management
BuilderImpl* cpp_create_builder(uint8_t* p,uint32_t w,uint32_t h) {
	auto* b=new BuilderImpl(); b->width=w; b->height=h; b->pixels.assign(p,p+w*h*4);
	b->diagonal=false; b->hierarchical=UINT32_MAX; b->batch_size=25600;
	b->clusters.push_back(Cluster()); b->cluster_indices.resize(w*h,0);
	b->iteration=0; b->next_index=1;
	return b;
}
void cpp_destroy_builder(BuilderImpl* b) { if(b) delete b; }

// NEW: Key Image (Auto-detect and replace transparency)
void cpp_key_image(BuilderImpl* b) {
	if(!b) return;
	if(ShouldKey(b)){
		Color k=FindUnusedColor(b);

		// Replace transparent pixels
		size_t len=b->pixels.size();
		for(size_t i=0; i<len; i+=4){
			if(b->pixels[i+3]==0){
				b->pixels[i]=k.r;
				b->pixels[i+1]=k.g;
				b->pixels[i+2]=k.b;
				b->pixels[i+3]=255; // Restore alpha
			}
		}

		b->key=k;
		b->has_key=true;
	} else{
		b->has_key=false;
		b->key={0, 0, 0, 0};
	}
}

// Removed key color args (handled by cpp_key_image now)
void cpp_set_builder_config(BuilderImpl* b,bool diagonal,uint32_t hierarchical,int32_t keying_action_int) {
	if(!b) return;
	b->diagonal=diagonal;
	b->hierarchical=hierarchical;
	b->keying_action=(keying_action_int==0) ? KeyingAction::Keep : KeyingAction::Discard;
}

void cpp_run_clustering(BuilderImpl* b,int merge_threshold,int32_t deepen_diff) { if(b){ b->clustering(); b->stage_2_sync_parity(deepen_diff); } }

// Cluster Data
void cpp_get_cluster_indices(BuilderImpl* b,uint32_t* o) { if(b) std::copy(b->cluster_indices.begin(),b->cluster_indices.end(),o); }
uint32_t cpp_get_cluster_count(BuilderImpl* b) { return b ? b->clusters.size() : 0; }
void cpp_get_cluster_meta(BuilderImpl* b,uint32_t i,CClusterMeta* o) {
	if(b&&i<b->clusters.size()){ const auto& c=b->clusters[i]; o->sum_r=c.sum.r; o->sum_g=c.sum.g; o->sum_b=c.sum.b; o->sum_a=c.sum.a; o->sum_count=c.sum.counter; o->res_r=c.residue_sum.r; o->res_g=c.residue_sum.g; o->res_b=c.residue_sum.b; o->res_a=c.residue_sum.a; o->res_count=c.residue_sum.counter; o->rect_l=c.rect.left; o->rect_t=c.rect.top; o->rect_r=c.rect.right; o->rect_b=c.rect.bottom; o->num_holes=c.num_holes; o->depth=c.depth; o->merged_into=c.merged_into; }
}
uint32_t cpp_get_cluster_indices_len(BuilderImpl* b,uint32_t i) { return (b&&i<b->clusters.size()) ? b->clusters[i].indices.size() : 0; }
void cpp_copy_cluster_indices(BuilderImpl* b,uint32_t i,uint32_t* o) { if(b&&i<b->clusters.size()) std::copy(b->clusters[i].indices.begin(),b->clusters[i].indices.end(),o); }
uint32_t cpp_get_cluster_holes_len(BuilderImpl* b,uint32_t i) { return (b&&i<b->clusters.size()) ? b->clusters[i].holes.size() : 0; }
void cpp_copy_cluster_holes(BuilderImpl* b,uint32_t i,uint32_t* o) { if(b&&i<b->clusters.size()) std::copy(b->clusters[i].holes.begin(),b->clusters[i].holes.end(),o); }
uint32_t cpp_get_output_len(BuilderImpl* b) { return b ? b->clusters_output.size() : 0; }
void cpp_copy_output(BuilderImpl* b,uint32_t* o) { if(b) std::copy(b->clusters_output.begin(),b->clusters_output.end(),o); }

// Mask Generation
uint32_t cpp_generate_cluster_mask(BuilderImpl* b,uint32_t i) {
	if(!b||i>=b->clusters.size()) return 0;
	const auto& c=b->clusters[i];
	uint32_t w=c.rect.right-c.rect.left,h=c.rect.bottom-c.rect.top;
	global_mask_width=w; global_mask_height=h; global_mask_buffer.assign(w*h,0);
	for(uint32_t idx : c.indices){
		int32_t x=(int32_t)(idx%b->width)-c.rect.left,y=(int32_t)(idx/b->width)-c.rect.top;
		if(x>=0&&x<(int)w&&y>=0&&y<(int)h) global_mask_buffer[y*w+x]=1;
	}
	return global_mask_buffer.size();
}

// Decomposition (BFS)
uint32_t cpp_decompose_mask(bool) {
	global_sub_clusters.clear();
	uint32_t w=global_mask_width,h=global_mask_height;
	for(uint32_t y=0; y<h; ++y) for(uint32_t x=0; x<w; ++x){
		size_t idx=y*w+x;
		if(global_mask_buffer[idx]!=0){
			CSubCluster sub; sub.rect_l=(int)w; sub.rect_t=(int)h; sub.rect_r=0; sub.rect_b=0; sub.parent_width=w;
			std::queue<uint32_t> q; q.push((uint32_t)idx); global_mask_buffer[idx]=0;
			while(!q.empty()){
				uint32_t curr=q.front(); q.pop(); sub.indices.push_back(curr);
				uint32_t cy=curr/w,cx=curr%w;
				if((int)cx<sub.rect_l)
					sub.rect_l=cx;
				if((int)(cx+1)>sub.rect_r)
					sub.rect_r=cx+1;
				if((int)cy<sub.rect_t)
					sub.rect_t=cy;
				if((int)(cy+1)>sub.rect_b)
					sub.rect_b=cy+1;
				if(cy>0&&global_mask_buffer[curr-w]){ global_mask_buffer[curr-w]=0; q.push(curr-w); }
				if(cy<h-1&&global_mask_buffer[curr+w]){ global_mask_buffer[curr+w]=0; q.push(curr+w); }
				if(cx>0&&global_mask_buffer[curr-1]){ global_mask_buffer[curr-1]=0; q.push(curr-1); }
				if(cx<w-1&&global_mask_buffer[curr+1]){ global_mask_buffer[curr+1]=0; q.push(curr+1); }
			}
			global_sub_clusters.push_back(sub);
		}
	}
	return global_sub_clusters.size();
}
void cpp_get_sub_cluster_meta(uint32_t i,int32_t* l,int32_t* t,int32_t* r,int32_t* b) {
	if(i<global_sub_clusters.size()){ const auto& s=global_sub_clusters[i]; *l=s.rect_l; *t=s.rect_t; *r=s.rect_r; *b=s.rect_b; }
}
uint32_t cpp_generate_sub_cluster_mask(uint32_t i) {
	if(i>=global_sub_clusters.size()) return 0;
	const auto& s=global_sub_clusters[i];
	uint32_t w=s.rect_r-s.rect_l,h=s.rect_b-s.rect_t;
	global_mask_width=w; global_mask_height=h; global_mask_buffer.assign(w*h,0);
	for(uint32_t idx : s.indices){
		uint32_t ox=idx%s.parent_width,oy=idx/s.parent_width;
		int32_t lx=(int32_t)ox-s.rect_l,ly=(int32_t)oy-s.rect_t;
		if(lx>=0&&lx<(int)w&&ly>=0&&ly<(int)h) global_mask_buffer[ly*w+lx]=1;
	}
	return global_mask_buffer.size();
}
void cpp_get_mask_dim(uint32_t* w,uint32_t* h) { *w=global_mask_width; *h=global_mask_height; }
void cpp_copy_mask_data(uint8_t* o) { std::copy(global_mask_buffer.begin(),global_mask_buffer.end(),o); }

// Hole Finding
uint32_t cpp_find_holes() {
	global_holes.clear();
	uint32_t w=global_mask_width,h=global_mask_height;
	for(uint32_t y=0; y<h; ++y) for(uint32_t x=0; x<w; ++x){
		size_t idx=y*w+x;
		if(global_mask_buffer[idx]==0){
			CSubCluster hole; hole.rect_l=(int)w; hole.rect_t=(int)h; hole.rect_r=0; hole.rect_b=0; hole.parent_width=w;
			bool edge=false;
			std::queue<uint32_t> q; q.push((uint32_t)idx); global_mask_buffer[idx]=2;
			while(!q.empty()){
				uint32_t curr=q.front(); q.pop(); hole.indices.push_back(curr);
				uint32_t cy=curr/w,cx=curr%w;
				if(cx==0||cx==w-1||cy==0||cy==h-1) edge=true;
				if((int)cx<hole.rect_l)
					hole.rect_l=cx;
				if((int)cx+1>hole.rect_r)
					hole.rect_r=cx+1;
				if((int)cy<hole.rect_t)
					hole.rect_t=cy;
				if((int)cy+1>hole.rect_b)
					hole.rect_b=cy+1;
				if(cy>0&&global_mask_buffer[curr-w]==0){ global_mask_buffer[curr-w]=2; q.push(curr-w); }
				if(cy<h-1&&global_mask_buffer[curr+w]==0){ global_mask_buffer[curr+w]=2; q.push(curr+w); }
				if(cx>0&&global_mask_buffer[curr-1]==0){ global_mask_buffer[curr-1]=2; q.push(curr-1); }
				if(cx<w-1&&global_mask_buffer[curr+1]==0){ global_mask_buffer[curr+1]=2; q.push(curr+1); }
			}
			if(!edge) global_holes.push_back(hole);
		}
	}
	return global_holes.size();
}
void cpp_get_hole_meta(uint32_t i,int32_t* l,int32_t* t,int32_t* r,int32_t* b) {
	if(i<global_holes.size()){ const auto& h=global_holes[i]; *l=h.rect_l; *t=h.rect_t; *r=h.rect_r; *b=h.rect_b; }
}
uint32_t cpp_generate_hole_mask(uint32_t i) {
	if(i>=global_holes.size()) return 0;
	const auto& h=global_holes[i];
	uint32_t w=h.rect_r-h.rect_l,h_=h.rect_b-h.rect_t;
	global_mask_width=w; global_mask_height=h_; global_mask_buffer.assign(w*h_,0);
	for(uint32_t idx : h.indices){
		uint32_t ox=idx%h.parent_width,oy=idx/h.parent_width;
		int32_t lx=(int32_t)ox-h.rect_l,ly=(int32_t)oy-h.rect_t;
		if(lx>=0&&lx<(int)w&&ly>=0&&ly<(int)h_) global_mask_buffer[ly*w+lx]=1;
	}
	return global_mask_buffer.size();
}

// Boundary Management
uint32_t cpp_prepare_boundaries() {
	global_boundaries.clear();
	CBoundary main; main.pixels=global_mask_buffer; main.width=global_mask_width; main.height=global_mask_height; main.off_x=0; main.off_y=0;
	global_boundaries.push_back(main);
	uint32_t hc=cpp_find_holes();
	for(uint32_t i=0; i<hc; ++i){
		const auto& h=global_holes[i];
		for(uint32_t idx : h.indices) if(idx<global_boundaries[0].pixels.size()) global_boundaries[0].pixels[idx]=1; // Patch
		if(cpp_generate_hole_mask(i)>0){
			CBoundary hb; hb.pixels=global_mask_buffer; hb.width=global_mask_width; hb.height=global_mask_height;
			int32_t l,t,r,b; cpp_get_hole_meta(i,&l,&t,&r,&b);
			hb.off_x=l; hb.off_y=t;
			global_boundaries.push_back(hb);
		}
	}
	return global_boundaries.size();
}
uint32_t cpp_load_boundary(uint32_t i) {
	if(i>=global_boundaries.size()) return 0;
	const auto& b=global_boundaries[i];
	global_mask_width=b.width; global_mask_height=b.height; global_mask_buffer=b.pixels;
	return global_mask_buffer.size();
}
void cpp_get_boundary_offset(uint32_t i,int32_t* x,int32_t* y) {
	if(i<global_boundaries.size()){ *x=global_boundaries[i].off_x; *y=global_boundaries[i].off_y; }
}

// Find Start Pixel (C++)
uint32_t cpp_find_boundary_start(uint32_t boundary_idx,int32_t* out_x,int32_t* out_y) {
	if(boundary_idx>=global_boundaries.size()) return 0;
	const auto& b=global_boundaries[boundary_idx];

	for(uint32_t y=0; y<b.height; ++y){
		for(uint32_t x=0; x<b.width; ++x){
			size_t idx=y*b.width+x;

			// Pixel must be active (1)
			if(b.pixels[idx]!=0){
				// Check if it is on the boundary (has at least one empty neighbor)
				// We check 4-neighbors for simplicity, matching the Rust walker logic
				bool border=false;
				if(x==0||b.pixels[idx-1]==0) border=true;
				else if(x==b.width-1||b.pixels[idx+1]==0) border=true;
				else if(y==0||b.pixels[idx-b.width]==0) border=true;
				else if(y==b.height-1||b.pixels[idx+b.width]==0) border=true;

				if(border){
					*out_x=(int32_t)x;
					*out_y=(int32_t)y;
					return 1;
				}
			}
		}
	}
	return 0; // Not found
}

// NEW: Walker using internal buffer (No Copying!)
// Coding Standard: Dense formatting, horizontal layout
uint32_t cpp_walk_current_boundary(int32_t startX,int32_t startY,bool clockwise) {
	if(global_mask_buffer.empty()) return 0;

	global_path_buffer.clear();

	// 1. State Setup
	int32_t currX=startX;
	int32_t currY=startY;
	int32_t prevX=startX;
	int32_t prevY=startY;
	int32_t prevPrevX=startX;
	int32_t prevPrevY=startY;

	uint32_t length=0;
	bool first=true;

	// Use internal globals directly
	size_t width=(size_t)global_mask_width;
	size_t height=(size_t)global_mask_height;
	const uint8_t* pixels=global_mask_buffer.data();

	// 2. Constants
	const int32_t DX[]={0,1,1,1,0,-1,-1,-1};
	const int32_t DY[]={-1,-1,0,1,1,1,0,-1};
	const int32_t AX[]={-1,0,0,0,-1,0,-1,0};
	const int32_t AY[]={-1,0,0,0,0,0,0,0};
	const int32_t BX[]={0,0,0,0,0,0,-1,0};
	const int32_t BY[]={-1,0,-1,0,0,0,-1,0};

	const int searchCW[]={0,2,4,6};
	const int searchCCW[]={6,4,2,0};
	const int* searchDirs=clockwise ? searchCW : searchCCW;

	// 3. Main Loop
	while(true){
		if(first){
			first=false;
			global_path_buffer.push_back({currX,currY});
		} else if(currX==startX&&currY==startY&&length>0){
			break;
		}

		int currentRunDir=-1;

		while(true){
			int foundDir=-1;

			for(int i=0;i<4;++i){
				int dir=searchDirs[i];

				int32_t nextX=currX+DX[dir];
				int32_t nextY=currY+DY[dir];

				bool isPrev=(nextX==prevX&&nextY==prevY);
				bool isPrevPrev=(nextX==prevPrevX&&nextY==prevPrevY);

				if(isPrev||isPrevPrev) continue;

				int32_t ax=currX+AX[dir];
				int32_t ay=currY+AY[dir];
				int32_t bx=currX+BX[dir];
				int32_t by=currY+BY[dir];

				bool valA=false;
				if(ax>=0&&ay>=0&&(size_t)ax<width&&(size_t)ay<height){
					valA=pixels[(size_t)ay*width+(size_t)ax]!=0;
				}

				bool valB=false;
				if(bx>=0&&by>=0&&(size_t)bx<width&&(size_t)by<height){
					valB=pixels[(size_t)by*width+(size_t)bx]!=0;
				}

				if(valA!=valB){
					foundDir=dir;
					break;
				}
			}

			if(foundDir!=-1){
				if(currentRunDir!=-1&&currentRunDir!=foundDir) break;

				currentRunDir=foundDir;

				prevPrevX=prevX; prevPrevY=prevY;
				prevX=currX;     prevY=currY;
				currX+=DX[foundDir];
				currY+=DY[foundDir];
				length++;

				if(length>(uint32_t)(width*height*2)) return (uint32_t)global_path_buffer.size();
			} else{
				return (uint32_t)global_path_buffer.size();
			}
		}
		global_path_buffer.push_back({currX,currY});
	}

	return (uint32_t)global_path_buffer.size();
}

void cpp_copy_path_data(int32_t* out_buf) {
	memcpy(out_buf,global_path_buffer.data(),global_path_buffer.size()*sizeof(Point));
}

// Staircase Removal
uint32_t cpp_simplify_staircase(bool clockwise) {
	if(global_path_buffer.empty()) return 0;

	size_t len=global_path_buffer.size();
	std::vector<Point> result;
	result.reserve(len);

	for(size_t i=0;i<len;++i){
		size_t j=(i+1)%len;
		size_t h=(i>0) ? i-1 : len-1;

		bool keep=false;

		// Always keep start and end points (matching Rust logic)
		if(i==0||i==len-1){
			keep=true;
		} else{
			Point pI=global_path_buffer[i];
			Point pH=global_path_buffer[h];
			Point pJ=global_path_buffer[j];

			int32_t lenH=ManhattanDist(pI,pH);
			int32_t lenJ=ManhattanDist(pI,pJ);

			// Check for 1-pixel steps
			if(lenH==1||lenJ==1){
				int32_t area=SignedArea(pH,pI,pJ);
				// Keep if it forms a convex corner matching the winding order
				if(area!=0&&((area>0)==clockwise)){
					keep=true;
				}
			} else{
				// Keep non-staircase points
				keep=true;
			}
		}

		if(keep){
			result.push_back(global_path_buffer[i]);
		}
	}

	// Swap result back into global buffer
	global_path_buffer.swap(result);
	return (uint32_t)global_path_buffer.size();
}

// NEW: Limit Penalties (Simplify)
	// Coding Standard: Dense formatting, horizontal layout
uint32_t cpp_limit_penalties() {
	if(global_path_buffer.size()<=2) return (uint32_t)global_path_buffer.size();

	size_t len=global_path_buffer.size();
	std::vector<Point> result;
	result.reserve(len);

	// Always keep the first point
	result.push_back(global_path_buffer[0]);
	size_t last=0;
	double tolerance=1.0;

	for(size_t i=1;i<len;++i){
		// Cannot simplify adjacent points
		if(i==last+1) continue;

		// Calculate max penalty for all points between 'last' and 'i'
		double maxP=0.0;
		for(size_t k=last+1;k<i;++k){
			double p=EvaluatePenalty(global_path_buffer[last],global_path_buffer[k],global_path_buffer[i]);
			if(p>maxP) maxP=p;
		}

		// If penalty exceeds tolerance, we must commit the previous valid point
		if(maxP>=tolerance){
			result.push_back(global_path_buffer[i-1]);
			last=i-1;
		}
	}

	// Always keep the last point
	result.push_back(global_path_buffer[len-1]);

	global_path_buffer.swap(result);
	return (uint32_t)global_path_buffer.size();
}


// Path Smoothing (Integer -> Float)
uint32_t cpp_smooth_path(double cornerThreshold,double outsetRatio,double segmentLength,int maxIterations) {
	if(global_path_buffer.empty()) return 0;

	// 1. Convert Int to Float
	global_path_buffer_f.clear();
	global_path_buffer_f.reserve(global_path_buffer.size());
	for(const auto& p:global_path_buffer){
		global_path_buffer_f.push_back({(double)p.x,(double)p.y});
	}

	// Ensure closed loop for calculations (Rust logic expects closed)
	if(global_path_buffer_f.size()>1){
		PointF first=global_path_buffer_f[0];
		PointF last=global_path_buffer_f.back();
		if(std::abs(first.x-last.x)>1e-5||std::abs(first.y-last.y)>1e-5){
			global_path_buffer_f.push_back(first);
		}
	}

	// 2. Iterative Subdivision
	std::vector<bool> corners;
	std::vector<PointF> newPath;
	std::vector<bool> newCorners;

	for(int iter=0;iter<maxIterations;++iter){
		size_t len=global_path_buffer_f.size();
		// Path is closed, so actual segments = len-1
		if(len<3) break;
		size_t segCount=len-1;

		// A. Find Corners
		corners.assign(len,false);
		for(size_t i=0;i<segCount;++i){
			size_t prev=(i==0) ? segCount-1 : i-1;
			size_t next=(i+1)%segCount;

			PointF pI=global_path_buffer_f[i];
			PointF pPrev=global_path_buffer_f[prev];
			PointF pNext=global_path_buffer_f[next];

			PointF v1={pI.x-pPrev.x,pI.y-pPrev.y};
			PointF v2={pNext.x-pI.x,pNext.y-pI.y};

			double a1=Angle(Normalize(v1));
			double a2=Angle(Normalize(v2));

			if(std::abs(SignedAngleDiff(a1,a2))>=cornerThreshold){
				corners[i]=true;
			}
		}

		// B. Subdivide
		bool canTerminate=true;
		newPath.clear();
		newCorners.clear();
		// Reserve worst case (2x points)
		newPath.reserve(len*2);
		newCorners.reserve(len*2);

		for(size_t i=0;i<segCount;++i){
			newPath.push_back(global_path_buffer_f[i]);
			newCorners.push_back(corners[i]);

			size_t j=(i+1)%segCount;
			PointF pI=global_path_buffer_f[i];
			PointF pJ=global_path_buffer_f[j];

			double lenCurr=Dist(pI,pJ);
			if(lenCurr<=segmentLength) continue;

			size_t prev=(i==0) ? segCount-1 : i-1;
			size_t next=(j+1)%segCount;

			PointF pPrev=global_path_buffer_f[prev];
			PointF pNext=global_path_buffer_f[next];

			double lenPrev=Dist(pPrev,pI);
			double lenNext=Dist(pNext,pJ);

			// Ratio Check
			if(lenPrev/lenCurr>=2.0||lenNext/lenCurr>=2.0) continue;

			// Corner Logic
			if(corners[i]) prev=i;
			if(corners[j]) next=j;

			if(prev==i&&next==j) continue; // Both corners, don't smooth

			// 4-Point Scheme Calculation
			// Mid Out (between i and j)
			PointF midOut={(pI.x+pJ.x)*0.5,(pI.y+pJ.y)*0.5};
			// Mid In (between prev and next) -- wait, logic says p_1 and p_2 (the outer ones)
			// Rust: mid_in = find_mid_point(p_1, p_2). here p_1=pPrev, p_2=pNext
			PointF p1=global_path_buffer_f[prev]; // actually re-read in case logic above shifted it
			PointF p2=global_path_buffer_f[next];
			PointF midIn={(p1.x+p2.x)*0.5,(p1.y+p2.y)*0.5};

			PointF vecOut={midOut.x-midIn.x,midOut.y-midIn.y};
			double vecLen=Norm(vecOut.x,vecOut.y);

			PointF newPoint;
			if(vecLen<1e-9){
				newPoint=midOut;
			} else{
				double newMag=vecLen/outsetRatio;
				// Normalize vecOut and multiply
				newPoint={midOut.x+(vecOut.x/vecLen)*newMag, midOut.y+(vecOut.y/vecLen)*newMag};
			}

			newPath.push_back(newPoint);
			newCorners.push_back(false);

			if(Dist(pI,newPoint)>segmentLength||Dist(pJ,newPoint)>segmentLength){
				canTerminate=false;
			}
		}

		// Close Loop
		if(!newPath.empty()){
			newPath.push_back(newPath[0]);
		}

		global_path_buffer_f=newPath;
		if(canTerminate) break;
	}

	return (uint32_t)global_path_buffer_f.size();
}

void cpp_copy_path_data_f64(double* outBuf) {
	// Direct memcpy of struct PointF {double, double} into double array
	memcpy(outBuf,global_path_buffer_f.data(),global_path_buffer_f.size()*sizeof(PointF));
}

// Find Splice Points (Cut Indices)
// Detects where to break the path into Bezier segments (corners/inflections)
uint32_t cpp_find_cut_points(double threshold) {
	if(global_path_buffer_f.size()<3) return 0;

	global_cut_indices.clear();

	// global_path_buffer_f is closed (First==Last). 
	// Effective unique points are size()-1.
	size_t len=global_path_buffer_f.size();
	size_t effLen=len-1;

	bool isAngleIncreasing=false;
	double angleDisp=0.0;

	for(size_t i=0;i<effLen;++i){
		size_t prev=(i==0) ? effLen-1 : i-1;
		size_t next=(i+1)%effLen;

		PointF pI=global_path_buffer_f[i];
		PointF pPrev=global_path_buffer_f[prev];
		PointF pNext=global_path_buffer_f[next];

		PointF v1={pI.x-pPrev.x, pI.y-pPrev.y};
		PointF v2={pNext.x-pI.x, pNext.y-pI.y};

		double a1=Angle(Normalize(v1));
		double a2=Angle(Normalize(v2));

		double diff=SignedAngleDiff(a1,a2);
		bool isPos=diff>0.0;

		bool isCut=false;

		// Inflection Point Check
		if(i==0){
			isAngleIncreasing=isPos;
		} else if(isAngleIncreasing!=isPos){
			isCut=true;
			isAngleIncreasing=isPos;
		}

		// Threshold Accumulation Check
		angleDisp+=diff;
		if(std::abs(angleDisp)>=threshold){
			isCut=true;
		}

		if(isCut){
			global_cut_indices.push_back((uint32_t)i);
			angleDisp=0.0;
		}
	}

	// Ensure we have at least valid start/mid points if nothing found
	if(global_cut_indices.empty()){
		global_cut_indices.push_back(0);
	}
	if(global_cut_indices.size()==1){
		global_cut_indices.push_back((uint32_t)((global_cut_indices[0]+effLen/2)%effLen));
	}

	return (uint32_t)global_cut_indices.size();
}

void cpp_copy_cut_indices(uint32_t* outBuf) {
	memcpy(outBuf,global_cut_indices.data(),global_cut_indices.size()*sizeof(uint32_t));
}

// Fit Spline
uint32_t cpp_fit_spline(double errorThreshold) {
	if(global_path_buffer_f.size()<3||global_cut_indices.size()<2) return 0;

	global_spline_buffer.clear();
	double errSq=errorThreshold*errorThreshold;

	// Using cut indices from previous step
	size_t numCuts=global_cut_indices.size();

	// P0 of the first curve
	global_spline_buffer.push_back(global_path_buffer_f[global_cut_indices[0]]);

	for(size_t i=0;i<numCuts;++i){
		int first=global_cut_indices[i];
		int nextIdx=global_cut_indices[(i+1)%numCuts];

		// Extract sub-path (Circular logic handled by building a temp vector)
		std::vector<PointF> subPath;
		if(nextIdx>first){
			for(int k=first;k<=nextIdx;++k) subPath.push_back(global_path_buffer_f[k]);
		} else{
			// Wrap around
			for(size_t k=first;k<global_path_buffer_f.size();++k) subPath.push_back(global_path_buffer_f[k]);
			// path_buffer is closed, so size()-1 is same as 0. Skip it if we wrapped.
			for(int k=(global_path_buffer_f.back().x==global_path_buffer_f[0].x) ? 1 : 0; k<=nextIdx; ++k) subPath.push_back(global_path_buffer_f[k]);
		}

		if(subPath.size()<2) continue;

		// Estimate Tangents
		PointF t1=Unit(subPath[1]-subPath[0]);
		PointF t2=Unit(subPath[subPath.size()-2]-subPath.back());

		FitCubic(subPath,0,(int)subPath.size()-1,t1,t2,errSq);
	}
	return (uint32_t)global_spline_buffer.size();
}

void cpp_copy_spline_data(double* outBuf) {
	memcpy(outBuf,global_spline_buffer.data(),global_spline_buffer.size()*sizeof(PointF));
}

// Batch Processing
void cpp_batch_convert(BuilderImpl* b,BatchConfig conf) {
	if(!b) return;
	global_result_shapes.clear();

	// Iterate REVERSE (matching Rust: out_vec.iter().rev())
	// clusters_output contains the valid indices
	for(int i=(int)b->clusters_output.size()-1; i>=0; --i){
		ClusterIndex idx=b->clusters_output[i];

		// 1. Meta & Color
		const auto& cluster=b->clusters[idx]; // idx is ClusterIndex (uint32)
		// Color logic: Rust does new((res_r / res_count), ...)
		Color c=cluster.residue_sum.average();

		// 2. Generate Mask
		// Re-using existing mask generation logic
		cpp_generate_cluster_mask(b,(uint32_t)idx);

		if(global_mask_buffer.empty()) continue;

		// 3. Decompose
		cpp_decompose_mask(false); // sets global_sub_clusters

		// For each sub-cluster
		size_t sub_count=global_sub_clusters.size();
		for(size_t sub_i=0; sub_i<sub_count; ++sub_i){
			// Generate sub-mask
			uint32_t sub_size=cpp_generate_sub_cluster_mask((uint32_t)sub_i);
			if(sub_size==0) continue;

			// Prepare boundaries (Hull + Holes)
			cpp_prepare_boundaries(); // sets global_boundaries

			CShapeResult compound_shape;
			compound_shape.color=c;

			// Base offset calculation
			// base_offset_x = meta.rect_l + sub.rect_l
			int32_t base_x=cluster.rect.left+global_sub_clusters[sub_i].rect_l;
			int32_t base_y=cluster.rect.top+global_sub_clusters[sub_i].rect_t;

			size_t boundary_count=global_boundaries.size();
			for(size_t b_i=0; b_i<boundary_count; ++b_i){
				int32_t start_x=0,start_y=0;
				// Find start using internal logic
				if(cpp_find_boundary_start((uint32_t)b_i,&start_x,&start_y)==0) continue;

				// Load specific boundary into global_mask_buffer for walking
				cpp_load_boundary((uint32_t)b_i);

				// A. WALK
				bool clockwise=(b_i==0); // First is hull (cw), others are holes (ccw)
				if(cpp_walk_current_boundary(start_x,start_y,clockwise)==0) continue;

				// B. SIMPLIFY
				if(cpp_simplify_staircase(clockwise)==0) continue;
				if(cpp_limit_penalties()==0) continue;

				// Boundary local offset
				int32_t off_x=global_boundaries[b_i].off_x;
				int32_t off_y=global_boundaries[b_i].off_y;

				double final_off_x=(double)(base_x+off_x);
				double final_off_y=(double)(base_y+off_y);

				CPrimitive prim;

				if(conf.mode==1){ // Spline Mode
					// C. SMOOTH
					if(cpp_smooth_path(conf.corner_threshold,8.0,conf.length_threshold,conf.max_iterations)>=4){
						// D. CUT
						cpp_find_cut_points(conf.splice_threshold);
						// E. FIT
						if(cpp_fit_spline(1.0)>0){
							prim.is_spline=true;
							prim.points.reserve(global_spline_buffer.size());
							for(const auto& p:global_spline_buffer){
								prim.points.push_back({p.x+final_off_x, p.y+final_off_y});
							}
							compound_shape.primitives.push_back(prim);
						}
					}
				} else{ // Polygon Mode
					prim.is_spline=false;
					prim.points.reserve(global_path_buffer.size());
					for(const auto& p:global_path_buffer){
						prim.points.push_back({(double)p.x+final_off_x, (double)p.y+final_off_y});
					}
					if(!prim.points.empty()){
						compound_shape.primitives.push_back(prim);
					}
				}
			}

			if(!compound_shape.primitives.empty()){
				global_result_shapes.push_back(compound_shape);
			}
		}
	}
}

void cpp_get_builder_dim(BuilderImpl* b,uint32_t* w,uint32_t* h) {
	if(b){ *w=b->width; *h=b->height; }
}
// Load from file and execute pipeline
BuilderImpl* cpp_execute_pipeline_from_file(const char* filename,bool diagonal,uint32_t hierarchical,int32_t keying_action,BatchConfig conf) {
	int w,h,c;
	// Force 4 channels (RGBA)
	uint8_t* pixels=stbi_load(filename,&w,&h,&c,4);

	if(!pixels){
		return nullptr;
	}

	// 1. Create Builder (This copies the pixels into the BuilderImpl's vector)
	BuilderImpl* b=cpp_create_builder(pixels,(uint32_t)w,(uint32_t)h);

	// 2. Free the STB image memory immediately
	stbi_image_free(pixels);

	if(b){
		cpp_key_image(b);
		cpp_set_builder_config(b,diagonal,hierarchical,keying_action);
		cpp_run_clustering(b,16,8);
		cpp_batch_convert(b,conf);
	}

	return b;
}
BuilderImpl* cpp_execute_pipeline(uint8_t* img,uint32_t w,uint32_t h,bool diagonal,uint32_t hierarchical,int32_t keying_action,BatchConfig conf) {
	BuilderImpl* b=cpp_create_builder(img,w,h);
	cpp_key_image(b);
	cpp_set_builder_config(b,diagonal,hierarchical,keying_action);
	cpp_run_clustering(b,16,8);
	cpp_batch_convert(b,conf);
	return b;
}

// SINGLE ENTRY POINT (All-in-One)
int32_t cpp_convert_file_to_svg(const char* input_path,const char* output_path,bool diagonal,uint32_t hierarchical,int32_t keying_action,BatchConfig conf,int32_t precision) {
	int w,h,c;
	uint8_t* pixels=stbi_load(input_path,&w,&h,&c,4);
	if(!pixels) return 0;

	BuilderImpl* b=cpp_create_builder(pixels,(uint32_t)w,(uint32_t)h);
	stbi_image_free(pixels); // Free raw buffer immediately

	if(!b) return 0;

	// Execute Pipeline
	cpp_key_image(b);
	cpp_set_builder_config(b,diagonal,hierarchical,keying_action);
	cpp_run_clustering(b,16,8);
	cpp_batch_convert(b,conf);

	// Save Result
	bool ok=SaveSvgInternal(b,output_path,precision);

	delete b; // Cleanup Builder
	return ok ? 1 : 0;
}

// --- Result Accessors ---
uint32_t cpp_get_result_count() {
	return (uint32_t)global_result_shapes.size();
}

void cpp_get_result_meta(uint32_t i,Color* out_color,uint32_t* out_prim_count) {
	if(i<global_result_shapes.size()){
		*out_color=global_result_shapes[i].color;
		*out_prim_count=(uint32_t)global_result_shapes[i].primitives.size();
	}
}

uint32_t cpp_get_primitive_len(uint32_t shape_idx,uint32_t prim_idx) {
	if(shape_idx<global_result_shapes.size()){
		if(prim_idx<global_result_shapes[shape_idx].primitives.size()){
			return (uint32_t)global_result_shapes[shape_idx].primitives[prim_idx].points.size();
		}
	}
	return 0;
}

void cpp_get_primitive_data(uint32_t shape_idx,uint32_t prim_idx,double* out_pts,bool* out_is_spline) {
	const auto& p=global_result_shapes[shape_idx].primitives[prim_idx];
	*out_is_spline=p.is_spline;
	memcpy(out_pts,p.points.data(),p.points.size()*sizeof(PointF));
}


//}
