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
#include <sstream>
#include <thread>
using namespace routemine;
double dist(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
int main(int argc,char** argv){try{
    if(argc<3)throw std::runtime_error("adaptive_matrix terrain output-directory [seconds-per-case]");
    auto output=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(output);int seconds=argc>3?std::stoi(argv[3]):30;
    bool defaults=argc>4&&std::string(argv[4])=="defaults";
    DxfImportReport report;auto terrain=load_terrain(std::filesystem::u8path(argv[1]),&report);auto bounds=terrain.bounds();
    auto high=terrain.vertices().front(),low=high;std::vector<Vec3> centers;std::optional<std::pair<Vec3,Vec3>> short_pair;
    for(auto v:terrain.vertices()){auto z=terrain.elevation(v.x,v.y);if(!z||std::abs(*z-v.z)>1e-6)continue;if(v.z>high.z)high=v;if(v.z<low.z)low=v;}
    for(auto f:terrain.faces()){auto a=terrain.vertices()[f[0]],b=terrain.vertices()[f[1]],c=terrain.vertices()[f[2]];Vec3 q{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
        if(terrain.elevation(q.x,q.y))centers.push_back(q);
        if(!short_pair&&std::max({a.z,b.z,c.z})-std::min({a.z,b.z,c.z})<1e-8&&dist(a,b)>40){
            short_pair=std::pair{Vec3{.5*a.x+.25*b.x+.25*c.x,.5*a.y+.25*b.y+.25*c.y,a.z},Vec3{.25*a.x+.5*b.x+.25*c.x,.25*a.y+.5*b.y+.25*c.y,a.z}};
        }
    }
    auto near=[&](Vec3 p){return *std::min_element(centers.begin(),centers.end(),[&](Vec3 a,Vec3 b){return dist(a,p)<dist(b,p);});};
    auto middle=near({(bounds[0].x+bounds[1].x)/2,(bounds[0].y+bounds[1].y)/2,0});
    struct Pair{std::string name;Vec3 a,b;};std::vector<Pair> pairs{{"HIGH_LOW",high,low},{"HIGH_MIDDLE",high,middle},{"MIDDLE_LOW",middle,low},{"LOW_HIGH",low,high}};
    if(short_pair){pairs.push_back({"SHORT",short_pair->first,short_pair->second});pairs.push_back({"SHORT_REVERSE",short_pair->second,short_pair->first});}
    Vec3 user_a{793219.29991371243,8442359.4311684202,4447.5},user_b{793305.50737256743,8440467.7877923045,3435};
    if(auto z=terrain.elevation(user_a.x,user_a.y);z&&std::abs(*z-user_a.z)<1e-6)pairs.push_back({"USER_PAIR",user_a,user_b});
    Vec3 observed_a{793252.91692920763,8442379.5810208265,4447.5},observed_b{793245.16120166751,8440444.3783917315,3435};
    if(defaults)if(auto z=terrain.elevation(observed_a.x,observed_a.y);z&&std::abs(*z-observed_a.z)<1e-6){pairs.push_back({"OBSERVED_PAIR",observed_a,observed_b});pairs.push_back({"OBSERVED_REVERSE",observed_b,observed_a});}
    std::ofstream csv(output/"results.csv");csv<<std::setprecision(17)<<"case,mode,status,seconds,attempts,length_m,grade_pct,radius_m,straight_m,cut_m,fill_m,valid,error\n";
    std::cout<<"terrain="<<argv[1]<<" triangles="<<terrain.faces().size()<<" cases="<<pairs.size()*2<<std::endl;
    for(const auto& pair:pairs)for(bool earthwork:{false,true}){
        auto began=std::chrono::steady_clock::now();std::stop_source stop;std::atomic<bool> timeout=false;std::mutex mutex;std::condition_variable_any cv;
        std::jthread timer([&](std::stop_token token){std::unique_lock lock(mutex);cv.wait_for(lock,token,std::chrono::seconds(seconds),[]{return false;});if(!token.stop_requested()){timeout=true;stop.request_stop();}});
        Parameters p;p.continuous_curves=true;p.max_grade_percent=10;p.min_radius_m=defaults?30:50;p.min_straight_m=defaults?35:84;p.max_cut_m=2;p.max_fill_m=2;p.mode=earthwork?DesignMode::earthwork:DesignMode::surface;p.sample_spacing_m=2;
        SearchOptions limits;limits.max_states=30000;limits.max_expansions=6000;limits.max_edges=70000;RoadOptions road;road.max_ramp_candidates=24;
        if(defaults){limits=SearchOptions{};road=RoadOptions{};road.first_feasible=true;}
        UnitCosts costs;costs.road_per_m2=1;AdjustedRoadResult planned;std::string error;bool valid=false;
        try{if(defaults){planned.parameters=p;planned.search=limits;planned.road=road;planned.route=plan_road(terrain,pair.a,pair.b,p,costs,limits,road,stop.get_token()).surface;}
            else planned=plan_road_adjusted(terrain,pair.a,pair.b,p,costs,limits,road,stop.get_token());
            if(planned.route.status==SearchStatus::found){valid=estimate_sectioned_route(planned.route,terrain,planned.parameters,costs).validation.checked_constraints_pass();if(!valid)throw std::runtime_error("Independent validation failed");}
        }catch(const std::exception& e){error=e.what();}
        timer.request_stop();cv.notify_all();timer.join();double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        std::string status=!error.empty()?"error":timeout?"timeout":planned.route.status==SearchStatus::found?"found":planned.route.status==SearchStatus::budget_exceeded?"budget_exceeded":planned.route.status==SearchStatus::cancelled?"cancelled":"not_found";
        std::string name=pair.name+(earthwork?"_earthwork":"_surface");std::replace(error.begin(),error.end(),',',';');
        csv<<pair.name<<','<<(earthwork?"earthwork":"surface")<<','<<status<<','<<elapsed<<','<<planned.route.adjustment_attempts.size()<<',';
        if(valid){const auto& v=planned.route.estimate->validation;csv<<v.metrics.horizontal_m<<','<<v.metrics.max_grade_percent<<','<<planned.parameters.min_radius_m<<','<<planned.parameters.min_straight_m<<','<<v.max_cut_m<<','<<v.max_fill_m;
            std::ofstream route(output/(name+".csv"));if(earthwork)write_classified_route_csv(route,terrain,planned.route.route);else write_route_csv(route,planned.route.route);
        }else csv<<",,,,,";
        csv<<','<<valid<<','<<error<<'\n';csv.flush();std::ofstream audit(output/(name+".txt"));audit<<planned.route.adjustment_report<<"\n"<<error;
        std::cout<<name<<" status="<<status<<" seconds="<<elapsed<<" attempts="<<planned.route.adjustment_attempts.size()<<std::endl;
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
