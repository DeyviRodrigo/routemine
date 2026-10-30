#include "routemine/design_review.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace routemine;
void check(bool ok,const char* text="design review assertion failed"){if(!ok)throw std::runtime_error(text);}
Terrain flat(){return Terrain({{-500,-500,0},{1000,-500,0},{-500,1000,0},{1000,1000,0}},{{0,1,2},{1,3,2}});}
Parameters design(){Parameters p;p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=35;p.road_width_m=8;p.max_grade_percent=10;p.sample_spacing_m=2;p.mode=DesignMode::surface;return p;}
SearchResult path(std::vector<Vec3> points){SearchResult r;r.status=SearchStatus::found;r.strategy="surface_reference";r.route=std::move(points);return r;}
SearchOptions limits(){SearchOptions s;s.max_states=80;s.max_expansions=20;s.max_edges=200;return s;}
int main(int argc,char** argv){try{
    check(argc==2);const std::string test=argv[1];auto terrain=flat();auto p=design();auto budget=limits();UnitCosts cost;cost.road_per_m2=1;RoadOptions options;options.adaptive_resolution=false;options.max_ramp_candidates=4;
    if(test=="flat"){
        auto corridor=path({{0,0,0},{50,0,0},{50,80,0},{250,80,0},{250,0,0},{300,0,0}});int updates=0;std::vector<int> progress;
        auto review=review_surface_corridor(terrain,corridor,p,cost,budget,options,{},[&](int value){check(progress.empty()||value>=progress.back(),"progress moved backward");progress.push_back(value);},[&](const DesignReview& value){++updates;check(validate_design_review(terrain,value,p),"published unverified geometry");});
        check(updates>1&&review.connected.has_value(),"flat connected alignment missing");check(review.gaps.empty());
        check(review.connected->estimate->validation.metrics.horizontal_m<=300+1e-5,"shortest observed direct connection not selected");
        check(validate_design_review(terrain,review,p));check(progress.front()==0&&progress.back()==100);
        check(corridor.route.size()==6&&corridor.curves.empty(),"gray source was modified");
    }else if(test=="gaps"){
        Terrain pieces({{0,0,0},{180,0,0},{0,60,0},{180,60,0},{240,0,0},{420,0,0},{240,60,0},{420,60,0}},{{0,1,2},{1,3,2},{4,5,6},{5,7,6}});
        auto corridor=path({{0,30,0},{120,30,0},{300,30,0},{420,30,0}});bool published_local=false;
        auto review=review_surface_corridor(pieces,corridor,p,cost,budget,options,{}, {},[&](const DesignReview& update){check(validate_design_review(pieces,update,p));if(!update.sections.empty()&&!update.connected)published_local=true;});
        check(published_local&&!review.connected&&review.sections.size()>=2,"disconnected valid pieces not retained");
        check(!review.gaps.empty()&&validate_design_review(pieces,review,p));
        for(const auto& section:review.sections){for(auto point:section.candidate.route)check(point.x<=180+1e-6||point.x>=240-1e-6,"fake bridge through missing terrain");}
        check(std::any_of(review.gaps.begin(),review.gaps.end(),[](const auto& gap){return gap.first_station_m<180&&gap.last_station_m>240;}),"missing terrain was not marked as a pending connection");
    }else if(test=="fallback"){
        Terrain narrow({{0,0,0},{1800,0,0},{0,10,0},{1790,10,0},{1800,10,0},{1790,1800,0},{1800,1800,0}},{{0,1,2},{1,4,2},{3,4,5},{4,6,5}});
        auto corridor=path({});for(int i=0;i<=1795;++i)corridor.route.push_back({static_cast<double>(i),5,0});
        for(int i=6;i<=1795;++i)corridor.route.push_back({1795,static_cast<double>(i),0});
        auto review=review_surface_corridor(narrow,corridor,p,cost,budget,options);
        check(!review.connected&&validate_design_review(narrow,review,p));
        bool shorter=false;for(const auto& section:review.sections){auto length=section.candidate.estimate->validation.metrics.horizontal_m;
            check(length>=60-1e-6,"microscopic zigzag promoted");if(length<90&&section.first_station_m>1600&&section.first_station_m<1850)shorter=true;}
        check(shorter,"failed large corner window did not salvage a meaningful straight subpiece");
        check(!review.gaps.empty(),"sharp corner was silently joined");
    }else if(test=="strict"){
        DesignReview review;auto candidate=path({{0,0,0},{100,0,20}});review.sections.push_back({candidate,0,100});
        auto earth=p;earth.mode=DesignMode::earthwork;earth.max_fill_m=30;
        check(!validate_design_review(terrain,review,earth),"grade was silently relaxed");
        candidate=path({});for(int i=0;i<=90;++i){double angle=i*std::numbers::pi/180;candidate.route.push_back({5*std::cos(angle),5*std::sin(angle),0});}
        candidate.curves={{0,90,{0,0,0},90}};review.sections={{candidate,0,8}};
        check(!validate_design_review(terrain,review,p),"undersized radius passed");
        for(auto& q:candidate.route){q.x*=10;q.y*=10;}review.sections={{candidate,0,80}};
        check(validate_design_review(terrain,review,p),"valid wide arc rejected");
        review.sections[0].candidate.local_sections={{0,90,20,1,0}};
        check(!validate_design_review(terrain,review,p),"local relaxation bypassed original constraints");
    }else if(test=="spacing"){
        DesignReview review;review.sections={{path({{0,0,0},{100,0,0}}),0,100},{path({{0,2,0},{100,2,0}}),200,300}};
        review.gaps={{100,200,{100,0,0},{0,2,0},"empalme pendiente"}};
        check(!validate_design_review(terrain,review,p),"independent blue roads violate width separation");
        review.sections[1].first_station_m=0;review.sections[1].last_station_m=100;review.gaps.clear();
        check(!validate_design_review(terrain,review,p),"different endpoints were treated as same connection alternatives");
        review.sections[1].candidate.route={{100,0,0},{100,100,0}};review.sections[1].first_station_m=100;review.sections[1].last_station_m=200;review.gaps.clear();
        check(!validate_design_review(terrain,review,p),"unverified sharp join was treated as connected");
    }else if(test=="cancel"){
        std::stop_source stop;stop.request_stop();auto corridor=path({{0,0,0},{300,0,0}});
        auto review=review_surface_corridor(terrain,corridor,p,cost,budget,options,stop.get_token());
        check(review.sections.empty()&&!review.connected&&review.gaps.size()==1);
        check(review.report.find("cancelada")!=std::string::npos&&validate_design_review(terrain,review,p));
    }else throw std::invalid_argument("unknown design review test");
    std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
