#include "routemine/config.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
using namespace routemine;
int main(int argc,char** argv){try{
    if(argc<4)throw std::invalid_argument("ramp_bench probe terrain output-prefix | plan terrain request output-prefix");
    bool probe=std::string(argv[1])=="probe";
    if(!probe&&(argc!=5||(std::string(argv[1])!="plan"&&std::string(argv[1])!="plan-fast"&&std::string(argv[1])!="plan-auto"&&std::string(argv[1])!="corridor"&&std::string(argv[1])!="optimize-corridor"&&std::string(argv[1])!="hybrid")))throw std::invalid_argument("Invalid benchmark arguments");
    std::string output_prefix=argv[probe?3:4];
    for(const auto& extension:{".csv",".dxf",".request",".metrics.json",".bin",".json"}){
        auto output=std::filesystem::absolute(std::filesystem::u8path(output_prefix+extension)).lexically_normal();
        for(auto input:{argv[2],probe?argv[2]:argv[3]}){auto source=std::filesystem::absolute(std::filesystem::u8path(input)).lexically_normal();std::error_code error;
            if(output==source||(std::filesystem::exists(output)&&std::filesystem::equivalent(output,source,error)))throw std::invalid_argument("Benchmark output would overwrite input");}}
    auto begin=std::chrono::steady_clock::now();DxfImportReport report;auto t=load_terrain(std::filesystem::u8path(argv[2]),&report);
    auto bounds=t.bounds();auto bottom=t.vertices().front(),top=bottom;
    for(auto q:t.vertices()){if(q.z<bottom.z)bottom=q;if(q.z>top.z)top=q;}
    Vec3 low_center{};std::size_t low_count=0;
    for(auto q:t.vertices())if(q.z<bottom.z+5){low_center.x+=q.x;low_center.y+=q.y;++low_count;}
    low_center.x/=low_count;low_center.y/=low_count;double nearest=1e100;Vec3 floor=bottom;
    for(auto f:t.faces()){
        auto a=t.vertices()[f[0]],b=t.vertices()[f[1]],c=t.vertices()[f[2]];
        Vec3 q{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
        if(q.z<bottom.z+5){double d=std::hypot(q.x-low_center.x,q.y-low_center.y);if(d<nearest&&t.elevation(q.x,q.y)){nearest=d;floor=q;}}
    }
    std::cout<<std::setprecision(17)<<"triangles="<<t.faces().size()<<" skipped="<<report.degenerate_faces_skipped<<"\nbounds="<<bounds[0].x<<','<<bounds[0].y<<','<<bounds[0].z<<".."<<bounds[1].x<<','<<bounds[1].y<<','<<bounds[1].z<<"\nfloor="<<floor.x<<','<<floor.y<<','<<floor.z<<"\ntop="<<top.x<<','<<top.y<<','<<top.z<<std::endl;
    if(std::string(argv[1])=="probe"){
        std::string prefix=argv[3];double spacing=argc>4?std::stod(argv[4]):10;
        if(!std::isfinite(spacing)||spacing<=0)throw std::invalid_argument("Invalid probe spacing");
        auto nx=static_cast<std::size_t>((bounds[1].x-bounds[0].x)/spacing)+1,ny=static_cast<std::size_t>((bounds[1].y-bounds[0].y)/spacing)+1;
        std::ofstream grid(prefix+".bin",std::ios::binary);if(!grid)throw std::runtime_error("Cannot write grid");
        std::size_t valid=0;for(std::size_t y=0;y<ny;++y)for(std::size_t x=0;x<nx;++x){auto z=t.elevation(bounds[0].x+x*spacing,bounds[0].y+y*spacing);double value=z?*z:std::numeric_limits<double>::quiet_NaN();valid+=bool(z);grid.write(reinterpret_cast<const char*>(&value),sizeof value);}
        std::ofstream meta(prefix+".json");meta<<std::setprecision(17)<<"{\"nx\":"<<nx<<",\"ny\":"<<ny<<",\"spacing\":"<<spacing<<",\"origin\":["<<bounds[0].x<<','<<bounds[0].y<<"],\"floor\":["<<floor.x<<','<<floor.y<<','<<floor.z<<"],\"top\":["<<top.x<<','<<top.y<<','<<top.z<<"]}";
        std::cout<<"grid="<<nx<<'x'<<ny<<" valid="<<valid<<std::endl;
    }else if((std::string(argv[1])=="plan"||std::string(argv[1])=="plan-fast"||std::string(argv[1])=="plan-auto"||std::string(argv[1])=="corridor"||std::string(argv[1])=="optimize-corridor"||std::string(argv[1])=="hybrid")&&argc==5){
        std::ifstream request(argv[3]);auto c=resolve_configuration(t,argv[2],read_settings(request));
        if(std::string(argv[1])=="plan-fast"||std::string(argv[1])=="plan-auto")c.road_search.first_feasible=true;
        RoadAlternatives r;
        if(std::string(argv[1])=="hybrid"){c.parameters.continuous_curves=true;r.surface=plan_continuous_ramp(t,c.start,c.finish,c.parameters,c.costs,c.search,c.road_search);}
        else if(std::string(argv[1])=="corridor"||std::string(argv[1])=="optimize-corridor"){
            auto base=plan_surface_reference(t,c.start,c.finish,c.parameters,c.costs,c.search,c.road_search);c.parameters=base.parameters;c.search=base.search;c.road_search=base.road;r.surface=std::move(base.route);
            if(std::string(argv[1])=="optimize-corridor"&&r.surface.status==SearchStatus::found){c.parameters.mode=DesignMode::earthwork;r.surface=optimize_surface_reference(t,r.surface,c.parameters,c.costs,c.search,c.road_search);}
        }else if(std::string(argv[1])=="plan-auto"){
            auto adjusted=plan_road_with_retries(t,c.start,c.finish,c.parameters,c.costs,c.search,c.road_search,{}, {},
                [](const std::string& stage){std::cout<<stage<<std::endl;});
            c.parameters=adjusted.parameters;c.search=adjusted.search;c.road_search=adjusted.road;r.surface=std::move(adjusted.route);
            std::ofstream report(output_prefix+".diagnostics.txt");report<<r.surface.adjustment_report;
        }else r=plan_road(t,c.start,c.finish,c.parameters,c.costs,c.search,c.road_search);
        std::cout<<"status="<<static_cast<int>(r.surface.status)<<" expanded="<<r.surface.expanded<<" edges="<<r.surface.edges_checked<<std::endl;
        for(auto& m:r.messages)std::cout<<m<<'\n';
        std::string prefix=argv[4];std::ofstream snapshot(prefix+".request");write_configuration(snapshot,c);
        std::ofstream metrics(prefix+".metrics.json");metrics<<std::setprecision(17)<<"{\"status\":"<<static_cast<int>(r.surface.status)<<",\"expanded\":"<<r.surface.expanded<<",\"edges\":"<<r.surface.edges_checked;
        if(r.surface.status==SearchStatus::found){std::ofstream out(prefix+".csv");write_route_csv(out,r.surface.route,{},c.parameters.continuous_curves);auto& v=r.surface.estimate->validation;
            std::ofstream dxf(prefix+".dxf");write_route_dxf(dxf,r.surface.route,{},c.parameters.continuous_curves);
            std::ofstream curves(prefix+".curves.csv");curves<<std::setprecision(17)<<"first,last,cx,cy,cz,sweep_degrees\n";for(const auto& curve:r.surface.curves)curves<<curve.first<<','<<curve.last<<','<<curve.center.x<<','<<curve.center.y<<','<<curve.center.z<<','<<curve.sweep_degrees<<'\n';
            double radius=std::numeric_limits<double>::infinity();for(auto& curve:r.surface.curves){auto q=r.surface.route[curve.first];radius=std::min(radius,std::hypot(q.x-curve.center.x,q.y-curve.center.y));}
            metrics<<",\"horizontal_m\":"<<v.metrics.horizontal_m<<",\"max_grade_percent\":"<<v.metrics.max_grade_percent<<",\"max_cut_m\":"<<v.max_cut_m<<",\"max_fill_m\":"<<v.max_fill_m<<",\"curve_primitives\":"<<r.surface.curves.size()<<",\"min_radius_m\":"<<(std::isfinite(radius)?std::to_string(radius):"null")<<",\"points\":"<<r.surface.route.size()<<",\"checked_constraints_pass\":true";
            std::cout<<"length="<<v.metrics.horizontal_m<<" max_grade="<<v.metrics.max_grade_percent<<" cut="<<v.max_cut_m<<" fill="<<v.max_fill_m<<" curves="<<r.surface.curves.size()<<'\n';}
        metrics<<",\"elapsed_s\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<"}";
    }
    std::cout<<"elapsed_s="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<std::endl;
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
