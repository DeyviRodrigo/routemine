#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool ok,const char* why="assertion failed"){if(!ok)throw std::runtime_error(why);}
bool has(const Validation& v,const char* key){return std::find(v.violations.begin(),v.violations.end(),key)!=v.violations.end();}
Terrain bowl(bool benches=false){
    std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
    for(int y=-20;y<=20;++y)for(int x=-20;x<=20;++x){double z=std::max(0.0,0.4*(std::hypot(x*10,y*10)-20));if(benches)z=7.5*std::ceil(z/7.5);vertices.push_back({x*10.0,y*10.0,z});}
    for(std::size_t y=0;y<40;++y)for(std::size_t x=0;x<40;++x){auto i=y*41+x;faces.push_back({i,i+1,i+41});faces.push_back({i+1,i+42,i+41});}
    return Terrain(vertices,faces);
}
Parameters params(){Parameters p;p.mode=DesignMode::earthwork;p.continuous_curves=true;p.min_radius_m=10;p.min_straight_m=0;p.max_turn_degrees=30;p.max_grade_percent=10;p.max_cut_m=8;p.max_fill_m=8;p.road_width_m=4;p.sample_spacing_m=2;return p;}
void fixture(const Terrain& terrain,Vec3 a,Vec3 b,const std::string& name){
    auto folder=std::filesystem::path("spiral-fixture");std::filesystem::create_directories(folder);
    std::ofstream mesh(folder/(name+".obj"));mesh<<std::setprecision(17);
    for(auto v:terrain.vertices())mesh<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<'\n';
    for(auto f:terrain.faces())mesh<<"f "<<f[0]+1<<' '<<f[1]+1<<' '<<f[2]+1<<'\n';
    std::ofstream request(folder/(name+".request"));request<<std::setprecision(17)
        <<"road_profile=routetec_monolith\ncost_profile=length_only\nstart_x="<<a.x<<"\nstart_y="<<a.y<<"\nstart_z="<<a.z
        <<"\nfinish_x="<<b.x<<"\nfinish_y="<<b.y<<"\nfinish_z="<<b.z
        <<"\nmax_grade_percent=10\nmin_radius_m=30\nmin_straight_m=35\nmax_turn_degrees=120\ncontinuous_curves=1\nearthwork_mode=0\nmax_cut_m=2\nmax_fill_m=2\nroad_width_m=8\n";
}
int main(int argc,char** argv){try{
    check(argc==2);std::string test=argv[1];auto p=params();
    if(test=="zero_grade"){
        Terrain flat({{0,0,0},{200,0,0},{0,200,0}},{{0,1,2}});p.max_grade_percent=0;
        auto r=plan_road(flat,{10,10,0},{100,10,0},p,UnitCosts{},SearchOptions{}).surface;
        check(r.status==SearchStatus::found,"Horizontal road with zero grade missing");
        check(r.estimate->validation.checked_constraints_pass());check(r.estimate->validation.metrics.max_grade_percent==0);
        Terrain slope({{0,0,0},{200,0,2},{0,200,0}},{{0,1,2}});
        auto blocked=plan_road(slope,{0,0,0},{100,0,1},p,UnitCosts{},SearchOptions{}).surface;
        check(blocked.status==SearchStatus::no_path,"Zero grade accepted different endpoint heights");
        Terrain ridge({{0,0,0},{100,0,2},{200,0,0},{0,200,0},{100,200,2},{200,200,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        p.mode=DesignMode::surface;SearchOptions small;small.max_states=100;small.max_edges=1000;
        auto obstructed=plan_road(ridge,{0,50,0},{200,50,0},p,UnitCosts{},small).surface;
        check(obstructed.status!=SearchStatus::found,"Zero grade crossed elevated ridge");
    }else if(test=="spiral_matrix"){
        auto original=bowl();std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces(original.faces().begin(),original.faces().end());
        for(auto q:original.vertices())vertices.push_back({q.x+10000,q.y-20000,500-q.z});
        Terrain t(vertices,faces);p.mode=DesignMode::surface;p.min_radius_m=30;p.min_straight_m=35;p.road_width_m=8;p.max_cut_m=p.max_fill_m=2;
        RoadOptions options;options.adaptive_resolution=false;options.first_feasible=true;SearchOptions limits;
        auto at=[&](double radius,double angle){Vec3 q{10000+radius*std::cos(angle),-20000+radius*std::sin(angle),0};q.z=*t.elevation(q.x,q.y);return q;};
        fixture(t,at(160,0.31),at(50,1.17),"Cerro_convexo_40_por_ciento");
        for(auto specification:std::vector<std::array<double,4>>{{160,0.31,50,1.17},{50,1.17,160,0.31},{180,2.1,60,-0.73},{60,-0.73,180,2.1},{140,0,140,std::numbers::pi},{140,std::numbers::pi,140,0}}){
            auto a=at(specification[0],specification[1]),b=at(specification[2],specification[3]);auto r=plan_road(t,a,b,p,UnitCosts{},limits,options).surface;
            check(r.status==SearchStatus::found,"Offset steep hill matrix route missing");check(validate(r.route,t,p,r.curves).checked_constraints_pass(),"Offset hill verification failed");
            check(r.route.front().x==a.x&&r.route.back().x==b.x,"Offset hill endpoints changed");
            std::cout<<"hill matrix length="<<r.estimate->validation.metrics.horizontal_m<<" grade="<<r.estimate->validation.metrics.max_grade_percent<<" curves="<<r.curves.size()<<'\n';
            if(a.z!=b.z){double winding=0;for(auto curve:r.curves)winding+=std::abs(curve.sweep_degrees);check(winding>360,"Steep hill did not acquire a complete turn");}
        }
    }else if(test=="steep_spiral"||test=="steep_spiral_reverse"){
        // A 40% hillside needs a winding path to respect a 10% road grade.
        // Endpoints are joined by a feasible logarithmic-spiral corridor, whose
        // curvature radius stays above 30 m; a straight is far too steep.
        auto t=bowl();double theta=std::log(180.0/40)*std::sqrt(1/(0.22*0.22)-1);
        Vec3 a{180,0,*t.elevation(180,0)},b{40*std::cos(theta),40*std::sin(theta),0};b.z=*t.elevation(b.x,b.y);
        if(test=="steep_spiral_reverse")std::swap(a,b);
        p.mode=DesignMode::surface;p.min_radius_m=30;p.min_straight_m=35;p.road_width_m=8;p.max_cut_m=p.max_fill_m=2;
        // Keep a reproducible application fixture alongside the numerical test.
        // It is also useful for the user's illustrated workflow/manual.
        if(test=="steep_spiral"){
            auto folder=std::filesystem::path("spiral-fixture");std::filesystem::create_directories(folder);
            std::ofstream mesh(folder/"Cerro_40_por_ciento.obj");mesh<<std::setprecision(17);
            for(auto v:t.vertices())mesh<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<'\n';
            for(auto f:t.faces())mesh<<"f "<<f[0]+1<<' '<<f[1]+1<<' '<<f[2]+1<<'\n';
            std::ofstream request(folder/"Espiral_10_por_ciento.request");request<<std::setprecision(17)
                <<"road_profile=routetec_monolith\ncost_profile=length_only\nstart_x="<<a.x<<"\nstart_y="<<a.y<<"\nstart_z="<<a.z
                <<"\nfinish_x="<<b.x<<"\nfinish_y="<<b.y<<"\nfinish_z="<<b.z
                <<"\nmax_grade_percent=10\nmin_radius_m=30\nmin_straight_m=35\nmax_turn_degrees=120\ncontinuous_curves=1\nearthwork_mode=0\nmax_cut_m=2\nmax_fill_m=2\nroad_width_m=8\n";
        }
        SearchOptions limits;RoadOptions options;options.adaptive_resolution=false;options.first_feasible=true;
        auto r=plan_road(t,a,b,p,UnitCosts{},limits,options).surface;
        std::cout<<"status="<<static_cast<int>(r.status)<<" expanded="<<r.expanded<<" edges="<<r.edges_checked<<'\n';
        check(r.status==SearchStatus::found,"Steep hillside spiral missing");
        auto v=validate(r.route,t,p,r.curves);check(v.checked_constraints_pass(),"Steep hillside spiral verification failed");
        check(v.metrics.max_grade_percent<=10+1e-7&&v.max_cut_m<1e-7&&v.max_fill_m<1e-7);
        check(r.curves.size()>10&&v.metrics.horizontal_m>500,"Steep hillside acquired a shortcut");
        check(r.route.front().x==a.x&&r.route.back().x==b.x,"Spiral endpoints changed");
    }else if(test=="trace"||test=="bowl"||test=="benches"||test=="reverse"||test=="cancel"||test=="budget"){
        auto t=bowl(test=="benches");Vec3 a{180,0,*t.elevation(180,0)},b{0,0,*t.elevation(0,0)};
        if(test=="reverse")std::swap(a,b);SearchOptions limits;RoadOptions road;road.adaptive_resolution=false;
        std::stop_source stop;if(test=="cancel")stop.request_stop();if(test=="budget")limits.max_edges=10;
        std::size_t partial=0,complete=0;
        if(test=="trace")road.trace=[&](std::span<const Vec3> path,bool connected){
            check(path.size()>=2);for(auto q:path)check(finite(q));
            if(connected){++complete;auto envelope=p;envelope.continuous_curves=false;envelope.min_radius_m=0;envelope.min_straight_m=0;envelope.max_turn_degrees=180;check(validate(path,t,envelope).checked_constraints_pass(),"Unvalidated complete trace");
                check(path.front().x==a.x&&path.back().x==b.x,"Trace endpoints changed");}else ++partial;
        };
        std::vector<int> progress;
        auto r=plan_road(t,a,b,p,UnitCosts{},limits,road,stop.get_token(),[&](int value){
            check(value>=0&&value<=100,"Progress outside range");
            check(progress.empty()||value>=progress.back(),"Progress moved backwards");progress.push_back(value);
        }).surface;
        check(!progress.empty()&&progress.front()==0);
        check(test=="cancel"?progress.back()<100:progress.back()==100);
        if(test!="cancel")check(progress.size()>4,"Missing search stage progress");
        if(test=="cancel")check(r.status==SearchStatus::cancelled&&r.route.empty());
        else if(test=="budget")check(r.status!=SearchStatus::found&&r.route.empty());
        else{
            std::cout<<"status="<<static_cast<int>(r.status)<<" expanded="<<r.expanded<<" edges="<<r.edges_checked<<'\n';
            check(r.status==SearchStatus::found,"Continuous bowl ramp not found");check(r.strategy=="continuous_ramp");
            check(validate(r.route,t,p,r.curves).checked_constraints_pass(),"Independent ramp verification failed");
            check(r.route.front().x==a.x&&r.route.front().z==a.z&&r.route.back().x==b.x&&r.route.back().z==b.z,"Endpoint changed");
            check(r.estimate->validation.metrics.horizontal_m>=std::abs(a.z-b.z)*100/p.max_grade_percent-1e-6);
            check(r.estimate->validation.metrics.max_grade_percent<=p.max_grade_percent+1e-8);
            check(r.curves.size()>5,"No continuous winding generated");
            if(test=="trace"){
                check(partial>0&&complete>0,"Missing live trace categories");road.trace={};
                auto plain=plan_road(t,a,b,p,UnitCosts{},limits,road).surface;
                check(plain.expanded==r.expanded&&plain.edges_checked==r.edges_checked&&plain.route.size()==r.route.size(),"Observer altered search");
                for(std::size_t i=0;i<r.route.size();++i)check(r.route[i].x==plain.route[i].x&&r.route[i].y==plain.route[i].y&&r.route[i].z==plain.route[i].z,"Observer altered route");
            }
        }
    }else if(test=="objective"){
        auto t=bowl();Vec3 a{180,0,*t.elevation(180,0)},b{0,0,0};
        UnitCosts costs;costs.road_per_m2=1;costs.soil_cut_per_m3=2;costs.fill_placement_per_m3=3;
        SearchOptions limits;RoadOptions road;road.adaptive_resolution=false;
        auto first=plan_road(t,a,b,p,costs,limits,road).surface;
        check(first.status==SearchStatus::found,"Cost-guided ramp not found");
        auto scaled=costs;scaled.road_per_m2*=10;scaled.soil_cut_per_m3*=10;scaled.fill_placement_per_m3*=10;
        auto second=plan_road(t,a,b,p,scaled,limits,road).surface;
        check(second.status==SearchStatus::found,"Scaled-cost ramp not found");
        check(std::abs(second.estimate->total-10*first.estimate->total)<1e-5,"Currency scaling changed objective");
        check(first.route.size()==second.route.size(),"Currency scaling changed route");
        road.adaptive_resolution=true;auto refined=plan_road(t,a,b,p,costs,limits,road).surface;
        check(refined.status==SearchStatus::found&&refined.estimate->total<=first.estimate->total+1e-8,"Refinement lost the cheaper valid incumbent");
        check(validate(refined.route,t,p,refined.curves).checked_constraints_pass());
    }else if(test=="surface_profile"||test=="fill_limits"){
        double height=test=="fill_limits"?-1:1;
        Terrain ridge({{-100,-100,0},{0,-100,height},{100,-100,0},{-100,100,0},{0,100,height},{100,100,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        Vec3 a{-100,0,0},b{100,0,0};p.max_grade_percent=test=="fill_limits"?0.5:10;
        p.mode=test=="fill_limits"?DesignMode::earthwork:DesignMode::surface;p.max_cut_m=0;p.max_fill_m=1;
        auto result=plan_road(ridge,a,b,p,UnitCosts{},SearchOptions{}).surface;
        check(result.status==SearchStatus::found,"Terrain mode route missing");
        check(validate(result.route,ridge,p,result.curves).checked_constraints_pass());
        if(test=="surface_profile"){
            check(result.route.size()>2,"Surface profile lost the ridge crossing");
            check(result.estimate->validation.max_cut_m<1e-8&&result.estimate->validation.max_fill_m<1e-8,"Surface mode performed earthwork");
            check(result.estimate->cut_m3<1e-6&&result.estimate->fill_m3<1e-6);
        }else{
            check(result.estimate->validation.max_fill_m<=1+1e-8&&result.estimate->validation.max_fill_m>0.9);
            p.max_fill_m=0;check(!validate(result.route,ridge,p,result.curves).checked_constraints_pass(),"Fill limit ignored");
            p.max_fill_m=0.5;check(!validate(result.route,ridge,p,result.curves).checked_constraints_pass(),"Independent fill cap ignored");
        }
    }else if(test=="terrain_options"){
        std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
        for(int y=-10;y<=10;++y)for(int x=-10;x<=10;++x)vertices.push_back({x*20.0,y*20.0,std::max(0.0,1-std::abs(x*20.0)/20-std::abs(y*20.0)/100)});
        for(std::size_t y=0;y<20;++y)for(std::size_t x=0;x<20;++x){auto i=y*21+x;faces.push_back({i,i+1,i+21});faces.push_back({i+1,i+22,i+21});}
        Terrain hill(vertices,faces);Vec3 a{-100,0,0},b{100,0,0};p.min_radius_m=50;p.max_grade_percent=2;p.max_cut_m=2;p.max_fill_m=2;
        RoadOptions road;road.adaptive_resolution=false;p.mode=DesignMode::surface;
        auto surface=plan_road(hill,a,b,p,UnitCosts{},SearchOptions{},road).surface;
        std::cout<<"surface_status="<<static_cast<int>(surface.status)<<" expanded="<<surface.expanded<<'\n';
        check(surface.status==SearchStatus::found,"Surface hill route not found");
        check(validate(surface.route,hill,p,surface.curves).checked_constraints_pass());
        check(surface.estimate->validation.max_cut_m<1e-8&&surface.estimate->validation.max_fill_m<1e-8);
        p.mode=DesignMode::earthwork;auto alternative=plan_road(hill,a,b,p,UnitCosts{},SearchOptions{},road).surface;
        check(alternative.status==SearchStatus::found&&validate(alternative.route,hill,p,alternative.curves).checked_constraints_pass());
        check(alternative.estimate->validation.metrics.horizontal_m<surface.estimate->validation.metrics.horizontal_m-1);
        check(alternative.estimate->validation.max_cut_m<=2&&alternative.estimate->validation.max_fill_m<=2);
        auto strict=p;strict.max_cut_m=0;check(!validate(alternative.route,hill,strict,alternative.curves).checked_constraints_pass(),"Cut ban ignored");
        strict.max_cut_m=0.5;check(!validate(alternative.route,hill,strict,alternative.curves).checked_constraints_pass(),"Cut cap ignored");
        std::cout<<"surface_m="<<surface.estimate->validation.metrics.horizontal_m<<" alternative_m="<<alternative.estimate->validation.metrics.horizontal_m<<'\n';
    }else if(test=="direct"){
        Terrain flat({{-100,-100,0},{100,-100,0},{-100,100,0},{100,100,0}},{{0,1,2},{1,3,2}});
        auto r=plan_road(flat,{0,0,0},{40,0,0},p,UnitCosts{},SearchOptions{}).surface;
        check(r.status==SearchStatus::found&&r.route.size()==2&&r.curves.empty(),"A feasible short straight acquired an unnecessary loop");
        check(std::abs(r.estimate->validation.metrics.horizontal_m-40)<1e-7);
    }else if(test=="joins"||test=="overlap"||test=="exports"){
        Terrain flat({{-100,-100,0},{100,-100,0},{-100,100,0},{100,100,0}},{{0,1,2},{1,3,2}});
        p.min_radius_m=20;std::vector<Vec3> route{{20,0,0}};std::vector<AlignmentCurve> curves;
        int quarters=test=="overlap"?8:3;
        for(int q=0;q<quarters;++q){auto first=route.size()-1;for(int i=1;i<=90;++i){double angle=(q*90+i)*std::numbers::pi/180;route.push_back({20*std::cos(angle),20*std::sin(angle),0});}curves.push_back({first,route.size()-1,{0,0,0},90});}
        auto v=validate(route,flat,p,curves);
        if(test=="overlap")check(has(v,"ramp_spacing_or_intersection"),"Stacked or crossing turns accepted");
        else{check(v.checked_constraints_pass(),"Continuous curves forced to add tangents or obey isolated turn cap");p.continuous_curves=false;check(!validate(route,flat,p,curves).checked_constraints_pass());}
        if(test=="exports"){std::ostringstream csv,dxf;write_route_csv(csv,route,{},true);write_route_dxf(dxf,route,{},true);check(csv.str().find("RAMPA_PROYECTO")!=std::string::npos);check(dxf.str().find("POLYLINE\n8\nROUTETEC_RAMPA\n")!=std::string::npos);}
    }else throw std::runtime_error("Unknown ramp test");
    std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
