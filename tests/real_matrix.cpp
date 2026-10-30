#include "routemine/config.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
using namespace routemine;
namespace {
double distance(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
struct Point {std::string name;Vec3 q;};
struct Case {std::size_t a,b;std::string name;};
std::string status_name(SearchStatus s,bool timeout){
    if(timeout)return "timeout";
    switch(s){case SearchStatus::found:return "found";case SearchStatus::no_path:return "no_path";
        case SearchStatus::budget_exceeded:return "budget_exceeded";case SearchStatus::cancelled:return "cancelled";}
    return "error";
}
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::invalid_argument("routemine_real_matrix terrain.dxf output-directory");
    auto input=std::filesystem::absolute(std::filesystem::u8path(argv[1])).lexically_normal();
    auto directory=std::filesystem::absolute(std::filesystem::u8path(argv[2])).lexically_normal();
    if(directory==input||directory==input.parent_path())throw std::invalid_argument("Use a separate output directory");
    std::filesystem::create_directories(directory);
    DxfImportReport imported;auto terrain=load_terrain(input,&imported);auto bounds=terrain.bounds();
    std::vector<Point> points;auto valid=[&](Vec3 p){auto z=terrain.elevation(p.x,p.y);return z&&std::abs(*z-p.z)<1e-6;};
    auto add=[&](std::string name,Vec3 p){if(!valid(p))throw std::runtime_error("Invalid selected point: "+name);points.push_back({std::move(name),p});};
    auto extrema=[&](bool high){double z=high?bounds[1].z:bounds[0].z;
        for(auto p:terrain.vertices())if(std::abs(p.z-z)<1e-6&&valid(p))return p;
        throw std::runtime_error("No unambiguous exact extreme point");};
    add("MAX",extrema(true));add("MIN",extrema(false));
    Vec3 center{};std::size_t count=0;
    for(auto q:terrain.vertices())if(std::abs(q.z-bounds[0].z)<1e-6){center.x+=q.x;center.y+=q.y;++count;}
    center.x/=count;center.y/=count;
    std::vector<Vec3> centers;centers.reserve(terrain.faces().size());
    Vec3 floor=points[1].q;double nearest=std::numeric_limits<double>::infinity();
    for(auto f:terrain.faces()){
        auto a=terrain.vertices()[f[0]],b=terrain.vertices()[f[1]],c=terrain.vertices()[f[2]];
        Vec3 q{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};centers.push_back(q);
        if(std::abs(q.z-bounds[0].z)<1e-6&&distance(q,center)<nearest&&valid(q)){floor=q;nearest=distance(q,center);}}
    add("FLOOR",floor);
    for(auto [label,fx,fy]:std::vector<std::tuple<const char*,double,double>>{
        {"SW",.2,.2},{"S",.5,.2},{"SE",.8,.2},{"W",.2,.5},{"C",.5,.5},
        {"E",.8,.5},{"NW",.2,.8},{"N",.5,.8},{"NE",.8,.8}}){
        Vec3 target{std::lerp(bounds[0].x,bounds[1].x,fx),std::lerp(bounds[0].y,bounds[1].y,fy),0};
        auto best=std::min_element(centers.begin(),centers.end(),[&](Vec3 a,Vec3 b){return distance(a,target)<distance(b,target);});
        if(best==centers.end()||!valid(*best))throw std::runtime_error("Invalid spatial point");add(label,*best);
    }
    // A nearby pair also checks short connections without relying on height extremes.
    auto nearby=std::min_element(centers.begin(),centers.end(),[&](Vec3 a,Vec3 b){
        return std::abs(distance(a,points[7].q)-40)<std::abs(distance(b,points[7].q)-40);});
    if(nearby!=centers.end())add("C_NEAR",*nearby);
    std::ofstream point_file(directory/"points.csv");point_file<<std::setprecision(17)<<"point,X,Y,Z\n";
    for(const auto& p:points)point_file<<p.name<<','<<p.q.x<<','<<p.q.y<<','<<p.q.z<<'\n';
    point_file.close();
    std::vector<Case> cases;
    auto pair=[&](std::size_t a,std::size_t b){if(distance(points[a].q,points[b].q)<1e-5)return;
        if(std::any_of(cases.begin(),cases.end(),[&](const Case& c){return c.a==a&&c.b==b;}))return;
        cases.push_back({a,b,points[a].name+"_to_"+points[b].name});};
    pair(0,1);pair(1,0);pair(0,2);pair(2,0);
    for(std::size_t i=3;i<12;++i)pair(i,2);
    for(auto [a,b]:std::vector<std::pair<std::size_t,std::size_t>>{{3,11},{5,9},{6,8},{4,10}}){pair(a,b);pair(b,a);}
    pair(7,12);pair(12,7);
    std::ofstream summary(directory/"results.csv");summary<<std::setprecision(17)
        <<"case,status,seconds,start_X,start_Y,start_Z,finish_X,finish_Y,finish_Z,horizontal_m,max_grade_percent,min_radius_m,max_cut_m,max_fill_m,points,expanded,edges,validation_pass,error\n";
    std::size_t found=0,failed=0;
    std::cout<<"input="<<input.string()<<" triangles="<<terrain.faces().size()<<" cases="<<cases.size()<<" max_z="<<bounds[1].z<<" min_z="<<bounds[0].z<<std::endl;
    for(const auto& item:cases){
        auto began=std::chrono::steady_clock::now();auto a=points[item.a].q,b=points[item.b].q;
        auto values=routetec_interface_defaults();auto put=[&](const char* key,double v){std::ostringstream s;s<<std::setprecision(17)<<v;values[key]=s.str();};
        put("start_x",a.x);put("start_y",a.y);put("start_z",a.z);put("finish_x",b.x);put("finish_y",b.y);put("finish_z",b.z);
        auto config=resolve_configuration(terrain,input.string(),values);auto prefix=directory/item.name;
        std::ofstream request(prefix.string()+".request");write_configuration(request,config);request.close();
        SearchResult route;std::string error;bool verified=false;std::atomic<bool> timed_out=false;
        std::stop_source stop;std::mutex mutex;std::condition_variable_any cv;
        std::jthread timer([&](std::stop_token token){std::unique_lock lock(mutex);cv.wait_for(lock,token,std::chrono::seconds(30),[]{return false;});if(!token.stop_requested()){timed_out=true;stop.request_stop();}});
        try{route=plan_road(terrain,a,b,config.parameters,config.costs,config.search,config.road_search,stop.get_token()).surface;
            if(route.status==SearchStatus::found){auto v=validate(route.route,terrain,config.parameters,route.curves);
                verified=v.checked_constraints_pass()&&distance(route.route.front(),a)<1e-7&&distance(route.route.back(),b)<1e-7&&std::abs(route.route.front().z-a.z)<1e-7&&std::abs(route.route.back().z-b.z)<1e-7;
                if(!verified)throw std::runtime_error("Independent validation failed");}}
        catch(const std::exception& e){error=e.what();route.status=SearchStatus::no_path;}
        timer.request_stop();cv.notify_all();timer.join();
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        auto status=error.empty()?status_name(route.status,timed_out):"error";
        summary<<item.name<<','<<status<<','<<seconds<<','<<a.x<<','<<a.y<<','<<a.z<<','<<b.x<<','<<b.y<<','<<b.z<<',';
        if(status=="found"){
            ++found;const auto& v=route.estimate->validation;double radius=std::numeric_limits<double>::infinity();
            for(auto c:route.curves)radius=std::min(radius,distance(c.center,route.route[c.first]));
            summary<<v.metrics.horizontal_m<<','<<v.metrics.max_grade_percent<<',';
            if(std::isfinite(radius))summary<<radius;
            summary<<','<<v.max_cut_m<<','<<v.max_fill_m<<','<<route.route.size();
            std::ofstream csv(prefix.string()+".csv");write_route_csv(csv,route.route,{},true);
            std::ofstream dxf(prefix.string()+".dxf");write_route_dxf(dxf,route.route,{},true);
        }else{++failed;summary<<",,,,,";}
        std::replace(error.begin(),error.end(),',',';');std::replace(error.begin(),error.end(),'\n',' ');
        summary<<','<<route.expanded<<','<<route.edges_checked<<','<<verified<<','<<error<<'\n';summary.flush();
        std::cout<<item.name<<" status="<<status<<" seconds="<<seconds<<" expanded="<<route.expanded<<" edges="<<route.edges_checked;
        if(route.estimate&&status=="found")std::cout<<" length="<<route.estimate->validation.metrics.horizontal_m;
        if(!error.empty())std::cout<<" error="<<error;
        std::cout<<std::endl;
    }
    std::cout<<"TOTAL found="<<found<<" failed="<<failed<<" cases="<<cases.size()<<std::endl;
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
