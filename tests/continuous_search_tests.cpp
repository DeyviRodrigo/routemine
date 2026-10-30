#include "routemine/continuous_search.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace routemine;
void check(bool ok,const char* message="continuous exploration assertion failed"){if(!ok)throw std::runtime_error(message);}
Terrain flat(){return Terrain({{-1000,-1000,0},{1000,-1000,0},{-1000,1000,0},{1000,1000,0}},{{0,1,2},{1,3,2}});}
Parameters parameters(){Parameters p;p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=35;p.mode=DesignMode::surface;return p;}
SearchResult corridor(){SearchResult c;c.status=SearchStatus::found;c.strategy="surface_reference";c.route={{-300,0,0},{300,0,0}};return c;}
int main(int argc,char** argv){try{
    check(argc==2);std::string name=argv[1];auto terrain=flat();auto p=parameters();auto source=corridor();UnitCosts cost;cost.road_per_m2=1;
    SearchOptions limits;limits.max_states=60;limits.max_expansions=15;limits.max_edges=100;RoadOptions road;road.max_ramp_candidates=3;
    std::stop_source stop;
    if(name=="rounds"){
        std::size_t round=0,positive=0;double shortest=1e100;bool north=false,south=false;std::size_t later_traces=0;
        road.trace=[&](std::span<const Vec3> path,bool complete){check(!complete,"generic trace promoted an unchecked candidate");for(auto q:path)check(finite(q));if(round){++later_traces;for(auto q:path){north|=q.y>20;south|=q.y< -20;}}};
        auto result=explore_design_continuously(terrain,source,p,cost,limits,road,stop.get_token(),[&](const DesignReview& review,std::size_t current){
            check(validate_design_review(terrain,review,p),"round published geometry outside original constraints");
            check(current>=round,"round counter moved backwards");round=current;
            if(review.connected){auto length=measure(review.connected->route).horizontal_m;check(length<=shortest+1e-6,"best complete path got worse");shortest=length;}
            if(current){++positive;if(current>=3)stop.request_stop();}
        });
        check(positive>=3&&round>=3,"search stopped at initial review");check(later_traces>0&&north&&south,"later rounds did not explore both sides");
        check(result.connected&&validate_design_review(terrain,result,p));check(source.route.size()==2&&source.route.front().x== -300,"source mutated");
    }else if(name=="cancel"){
        stop.request_stop();auto begin=std::chrono::steady_clock::now();auto result=explore_design_continuously(terrain,source,p,cost,limits,road,stop.get_token());
        check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(1));check(result.sections.empty()&&!result.connected&&validate_design_review(terrain,result,p));
    }else if(name=="wait_cancel"){
        std::size_t rounds=0;auto begin=std::chrono::steady_clock::now();std::jthread timer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(350));stop.request_stop();});
        auto result=explore_design_continuously(terrain,source,p,cost,limits,road,stop.get_token(),[&](const DesignReview& review,std::size_t current){rounds=std::max(rounds,current);check(validate_design_review(terrain,review,p));});
        check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2),"wait ignored stop token");check(rounds>0&&rounds<10,"continuous loop spins without bounded idle wait");check(result.connected.has_value());
    }else throw std::invalid_argument("unknown continuous search test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
