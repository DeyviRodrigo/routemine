#include "routemine/road_candidates.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
using namespace routemine;
void check(bool x,const char* message="road candidate assertion failed"){if(!x)throw std::runtime_error(message);}
Terrain plane(double slope=0,double offset=0){return Terrain({{-2000,-2000,offset-2000*slope},{2000,-2000,offset+2000*slope},{-2000,2000,offset-2000*slope},{2000,2000,offset+2000*slope}},{{0,1,2},{1,3,2}});}
Parameters design(){Parameters p;p.mode=DesignMode::earthwork;p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=35;p.road_width_m=8;p.max_grade_percent=10;p.max_cut_m=100;p.max_fill_m=100;return p;}
SearchResult line(double length,double grade=0){SearchResult r;r.status=SearchStatus::found;r.route={{0,0,0},{length,0,length*grade}};return r;}
SearchResult arc(bool closed=false){SearchResult r;r.status=SearchStatus::found;const int count=closed?360:90;for(int i=0;i<=count;++i){double a=(-90+i)*std::numbers::pi/180;r.route.push_back({60*std::cos(a),60+60*std::sin(a),0});}if(closed)r.curves={{0,180,{0,60,0},180},{180,360,{0,60,0},180}};else r.curves={{0,90,{0,60,0},90}};return r;}
int main(int argc,char** argv){try{check(argc==2);const std::string test=argv[1];auto p=design();RoadCandidateLimits limits;
    if(test=="limits"){
        check(validate_road_candidate(plane(),line(200),p,limits));
        check(validate_road_candidate(plane(0,1),line(80),p,limits));
        check(!validate_road_candidate(plane(0,1),line(200),p,limits),"long continuous cut accepted");
        check(!validate_road_candidate(plane(),line(80,0.15),p,limits),"road itself exceeded requested grade");
        check(validate_road_candidate(plane(0.15),line(80,0.1),p,limits),"short terrain exception below2x rejected");
        check(!validate_road_candidate(plane(0.25),line(80,0.1),p,limits),"terrain above2x accepted");
        check(!validate_road_candidate(plane(0.15),line(200,0.1),p,limits),"long slope/intervention accepted");
        limits.max_intervention_length_m=250;check(validate_road_candidate(plane(0,1),line(200),p,limits));
    }else if(test=="geometry"){
        auto bend=arc();check(validate_road_candidate(plane(),bend,p,limits),"smooth tangent arc rejected");
        auto sharp=line(100);sharp.route.push_back({100,100,0});check(!validate_road_candidate(plane(),sharp,p,limits),"unmodelled sharp corner accepted");
        auto tight=p;tight.min_radius_m=100;check(!validate_road_candidate(plane(),bend,tight,limits),"radius relaxed");
        check(!validate_road_candidate(plane(),arc(true),p,limits),"road retraced/touched itself");
    }else if(test=="rounds"){
        auto terrain=plane();auto ref=line(600);ref.route={{-300,0,0},{300,0,0}};SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;road.max_ramp_candidates=4;UnitCosts costs;costs.road_per_m2=1;
        std::stop_source stop;std::size_t branches=0,rounds=0;bool curved=false,long_path=false;
        road.trace=[&](std::span<const Vec3> path,bool complete){check(complete,"unchecked geometric trace emitted");check(path.size()>2,"raw ray emitted");};
        auto review=explore_road_candidates(terrain,ref,p,costs,resource,road,limits,stop.get_token(),[&](const DesignReview& r,std::size_t round){check(validate_design_review(terrain,r,p));rounds=round;if(round>=3)stop.request_stop();},[&](const SearchResult& candidate){++branches;check(validate_road_candidate(terrain,candidate,p,limits),"branch violated original constraints");curved|=!candidate.curves.empty();long_path|=measure(candidate.route).horizontal_m>600;});
        check(rounds>=3&&branches>0&&curved,"no valid curved candidates generated");check(long_path,"long alternatives were suppressed");check(validate_design_review(terrain,review,p));
    }else if(test=="contours"){
        auto terrain=plane(0.4);auto ref=line(600);ref.route={{-300,-300,-120},{-300,300,-120}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;UnitCosts costs;costs.road_per_m2=1;std::stop_source stop;double longest=0;
        explore_road_candidates(terrain,ref,p,costs,resource,road,limits,stop.get_token(),[&](const DesignReview&,std::size_t round){if(round>=1)stop.request_stop();},[&](const SearchResult& c){check(validate_road_candidate(terrain,c,p,limits));if(c.curves.empty())longest=std::max(longest,measure(c.route).horizontal_m);});
        check(longest>300,"a valid straight contour was forced into a short curved departure");
    }else if(test=="origins"){
        auto terrain=plane();auto ref=line(600);ref.route={{-300,0,0},{300,0,0}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;UnitCosts costs;std::stop_source stop;bool interior=false;
        explore_road_candidates(terrain,ref,p,costs,resource,road,limits,stop.get_token(),[&](const DesignReview&,std::size_t round){if(round>=4)stop.request_stop();},[&](const SearchResult& c){check(validate_road_candidate(terrain,c,p,limits));interior|=std::abs(c.route.front().y)>1;});
        check(interior,"growth never moved away from the original endpoints/corridor");
    }else if(test=="guide"){
        Terrain terrain({{-500,-300,0},{-100,-300,0},{-500,300,0},{-100,300,0},{100,-300,0},{500,-300,0},{100,300,0},{500,300,0}},{{0,1,2},{1,3,2},{4,5,6},{5,7,6}});
        SearchResult guide;guide.status=SearchStatus::no_path;guide.strategy="candidate_guide";guide.route={{-300,0,0},{300,0,0}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;road.max_ramp_candidates=4;UnitCosts costs;std::stop_source stop;std::size_t branches=0;
        auto review=explore_road_candidates(terrain,guide,p,costs,resource,road,limits,stop.get_token(),[&](const DesignReview& value,std::size_t round){check(validate_design_review(terrain,value,p));if(round>=4)stop.request_stop();},[&](const SearchResult& candidate){++branches;check(validate_road_candidate(terrain,candidate,p,limits));});
        check(branches>0&&!review.connected,"missing reference prevented independent candidate search");
        check(guide.status==SearchStatus::no_path&&!guide.estimate&&guide.route.size()==2,"guide was promoted to a valid road");
    }else if(test=="grade_traces"){
        auto sloping=plane(0.15);std::vector<Vec3> trace{{0,0,0},{200,0,30}};
        check(validate_grade_trace(sloping,trace,2*p.max_grade_percent),"gray terrain-only trace rejected");
        check(!validate_grade_trace(sloping,trace,p.max_grade_percent),"gray grade limit ignored");
        trace.back().z=0;check(!validate_grade_trace(sloping,trace,20),"floating gray chord accepted");
        Terrain ridge({{0,-10,0},{5,-10,2},{10,-10,0},{0,10,0},{5,10,2},{10,10,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        check(!validate_grade_trace(ridge,std::vector<Vec3>{{1,0,.4},{9,0,.4}},20),"steep face hidden between trace endpoints");
        Terrain gentle_ridge({{0,-10,0},{5,-10,1},{10,-10,0},{0,10,0},{5,10,1},{10,10,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        check(!validate_grade_trace(gentle_ridge,std::vector<Vec3>{{2,0,.4},{8,0,.4}},20),"gray chord floated across a grade-compliant ridge");
        check(validate_grade_trace(gentle_ridge,std::vector<Vec3>{{2,0,.4},{5,0,1},{8,0,.4}},20),"exact low-grade ridge crossing rejected");
        Terrain hole({{-20,-20,0},{-1,-20,0},{-20,20,0},{-1,20,0},{1,-20,0},{20,-20,0},{1,20,0},{20,20,0}},{{0,1,2},{1,3,2},{4,5,6},{5,7,6}});
        check(!validate_grade_trace(hole,std::vector<Vec3>{{-10,0,0},{10,0,0}},20),"gray trace crossed missing terrain");
        auto terrain=plane();SearchResult guide;guide.strategy="candidate_guide";guide.route={{-300,0,0},{300,0,0}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;road.max_ramp_candidates=4;
        std::stop_source stop;std::size_t grays=0,roads=0;
        explore_road_candidates(terrain,guide,p,UnitCosts{},resource,road,limits,stop.get_token(),[&](const DesignReview&,std::size_t round){if(round>=1)stop.request_stop();},[&](const SearchResult&){++roads;},[&](std::span<const Vec3> path){
            check(roads==0,"gray trace waited for a fully valid road");check(validate_grade_trace(terrain,path,2*p.max_grade_percent),"unchecked gray trace emitted");
            check(measure(path).horizontal_m>=5,"gray progress too short");for(std::size_t i=1;i<path.size();++i)check(std::hypot(path[i].x-path[i-1].x,path[i].y-path[i-1].y)<=2.000001,"gray trace lacks uniform samples");
            ++grays;stop.request_stop();
        });
        check(grays==1&&roads==0,"gray tracing did not start early or ignored cancellation");
        std::stop_source cancelled;cancelled.request_stop();std::size_t late=0;
        explore_road_candidates(terrain,guide,p,UnitCosts{},resource,road,limits,cancelled.get_token(),{},{},[&](std::span<const Vec3>){++late;});check(late==0,"cancelled search emitted a gray trace");
    }else if(test=="grade_expansion"){
        auto terrain=plane(.4);SearchResult guide;guide.strategy="candidate_guide";guide.route={{-300,-300,-120},{-300,300,-120}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;road.max_ramp_candidates=4;
        std::stop_source stop;std::optional<Vec3> end;double chain=0,longest=0;bool relaxed_band=false;std::size_t rounds=0;
        explore_road_candidates(terrain,guide,p,UnitCosts{},resource,road,limits,stop.get_token(),[&](const DesignReview&,std::size_t round){rounds=round;if(round>=2)stop.request_stop();},[&](const SearchResult& candidate){check(validate_road_candidate(terrain,candidate,p,limits),"expanded exploration relaxed road criteria");},[&](std::span<const Vec3> path){
            check(validate_grade_trace(terrain,path,2*p.max_grade_percent),"expanded gray exploration exceeded its slope/coverage limit");
            if(!end||std::hypot(end->x-path.front().x,end->y-path.front().y)>1e-6)chain=0;
            chain+=measure(path).horizontal_m;longest=std::max(longest,chain);end=path.back();
            for(std::size_t i=1;i<path.size();++i){const double h=std::hypot(path[i].x-path[i-1].x,path[i].y-path[i-1].y);if(h>1e-8)relaxed_band|=100*std::abs(path[i].z-path[i-1].z)/h>p.max_grade_percent+1e-4;}
        });
        check(rounds>=2&&longest>800,"exploration did not extend terrain walks beyond its old short-prefix limit");
        check(relaxed_band,"successive walks repeated contour headings instead of exploring the doubled-grade band");
    }else if(test=="candidate_incumbent"){
        auto terrain=plane();SearchResult guide;guide.strategy="candidate_guide";guide.route={{-300,0,0},{300,0,0}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;road.max_ramp_candidates=4;UnitCosts costs;costs.road_per_m2=1;
        std::stop_source stop;std::size_t first_round=0,last_round=0,complete_count=0;double first_cost=0,best_cost=std::numeric_limits<double>::infinity(),previous=std::numeric_limits<double>::infinity();
        auto review=explore_road_candidates(terrain,guide,p,costs,resource,road,limits,stop.get_token(),[&](const DesignReview& value,std::size_t round){
            last_round=round;if(value.connected){if(first_round==0)first_round=round;check(validate_road_candidate(terrain,*value.connected,p,limits));
                check(value.connected->route.front().x==-300&&value.connected->route.front().y==0&&value.connected->route.front().z==0&&value.connected->route.back().x==300&&value.connected->route.back().y==0&&value.connected->route.back().z==0,"incumbent changed A/B coordinates");
                const double score=value.connected->estimate->total;check(score<=previous+1e-6,"incumbent objective became worse");previous=score;
                if(round>=first_round+3)stop.request_stop();}
            if(round>=6)stop.request_stop();
        },[&](const SearchResult& candidate){if(std::hypot(candidate.route.front().x+300,candidate.route.front().y)<1e-6&&std::hypot(candidate.route.back().x-300,candidate.route.back().y)<1e-6){
                const double score=candidate.estimate->total;if(complete_count++==0)first_cost=score;best_cost=std::min(best_cost,score);}
        });
        check(review.connected&&complete_count>=2&&first_round>0&&last_round>=first_round+3,"connection stopped further search");
        check(first_cost>best_cost+1&&std::abs(review.connected->estimate->total-best_cost)<1e-6,"cheaper connection did not replace the first incumbent");
        check(std::abs(measure(review.connected->route).horizontal_m-600)<1e-6,"direct shorter connection lost to a long alternative");
    }else if(test=="hairpins"){
        auto terrain=plane();SearchResult guide;guide.strategy="candidate_guide";guide.route={{0,0,0},{0,180,0}};
        SearchOptions resource;resource.max_states=80;resource.max_expansions=20;resource.max_edges=200;RoadOptions road;road.max_ramp_candidates=4;
        std::stop_source stop;std::size_t max_turns=0;bool complete=false;
        auto review=explore_road_candidates(terrain,guide,p,UnitCosts{},resource,road,limits,stop.get_token(),[&](const DesignReview&,std::size_t round){if(round>=3)stop.request_stop();},[&](const SearchResult& candidate){
            check(validate_road_candidate(terrain,candidate,p,limits),"hairpin weakened full road validation");
            if(candidate.strategy!="serpentine")return;std::size_t turns=0;for(const auto& curve:candidate.curves)if(std::abs(std::abs(curve.sweep_degrees)-180)<1e-6)++turns;
            max_turns=std::max(max_turns,turns);complete|=candidate.route.front().x==0&&candidate.route.front().y==0&&candidate.route.back().x==0&&candidate.route.back().y==180;
        },[&](std::span<const Vec3> path){check(validate_grade_trace(terrain,path,2*p.max_grade_percent),"hairpin emitted an invalid grade trace");});
        check(complete&&max_turns>=3&&review.connected,"tangent multi-hairpin connections were not generated");
    }else if(test=="cancel"){
        std::stop_source stop;stop.request_stop();std::size_t updates=0;auto r=explore_road_candidates(plane(),line(200),p,UnitCosts{},SearchOptions{},RoadOptions{},limits,stop.get_token(),[&](const DesignReview&,std::size_t){++updates;});check(r.sections.empty()&&updates>0);
    }else throw std::invalid_argument("unknown test");std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
