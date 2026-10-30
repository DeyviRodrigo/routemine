#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <source_location>
using namespace routemine;
void require(bool condition,const std::source_location location=std::source_location::current()){if(!condition)throw std::runtime_error("assertion failed at line "+std::to_string(location.line()));}
void near(double a,double b,double eps=1e-8){require(std::abs(a-b)<=eps);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected);}
Terrain flat(){return Terrain({{0,0,0},{100,0,0},{0,100,0}},{{0,1,2}});}
bool has(const Validation& v,const std::string& s){return std::find(v.violations.begin(),v.violations.end(),s)!=v.violations.end();}
int main(int argc,char** argv){try{
    require(argc==2);const std::string test=argv[1];
    if(test=="utm") {
        Vec3 p{500000.001,8000000.002,1000.003}, o{500000,8000000,1000};
        auto local=to_local(p,o), world=to_world(local,o);near(local.x,0.001);near(world.x,p.x);near(world.y,p.y);
        Terrain t({o,{500010,8000000,1010},{500000,8000010,1000}},{{0,1,2}});near(*t.elevation(500001,8000001),1001);
    }else if(test=="utm_seam") {
        // Exact DXF boundary that used to lose ~43% of its interpolated samples.
        Vec3 a{791612.7352246094,8441460.638916016,3982.5},b{791612.5738476563,8441439.497070312,3980.484375000001};
        Vec3 inside{791620.389765625,8441446.59008789,3982.5};
        Terrain seam({inside,a,b},{{0,1,2}});
        for(int i=0;i<=10000;++i){double t=i/10000.;auto x=std::lerp(a.x,b.x,t),y=std::lerp(a.y,b.y,t);
            auto z=seam.elevation(x,y);require(z.has_value());near(*z,std::lerp(a.z,b.z,t),1e-6);
            require(seam.surface_gradient(x,y,false).has_value());
        }
        require(seam.segment_profile(a,b).has_value());
        Parameters p;p.mode=DesignMode::surface;p.sample_spacing_m=1;
        require(validate(std::vector<Vec3>{a,b},seam,p).checked_constraints_pass());
        // A physical 1 micrometre hole is far above UTM floating-point roundoff.
        const double ox=791600,oy=8441400,gap=1e-6;
        Terrain hole({{ox,oy,0},{ox+1,oy,0},{ox,oy+1,0},{ox+1+gap,oy,0},{ox+2,oy,0},{ox+2,oy+1,0}},{{0,1,2},{3,4,5}});
        require(!hole.elevation(ox+1+gap/2,oy));
        require(!hole.segment_profile({ox+0.5,oy,0},{ox+1.5,oy,0}));
        // Conflicting heights on genuine overlap must still be ambiguous.
        Terrain overlap({inside,a,b,{inside.x,inside.y,inside.z+1},{a.x,a.y,a.z+1},{b.x,b.y,b.z+1}},{{0,1,2},{3,4,5}});
        require(!overlap.elevation(std::midpoint(a.x,b.x),std::midpoint(a.y,b.y)));
        // No extrapolation a micrometre past an otherwise valid edge.
        auto mid=Vec3{std::midpoint(a.x,b.x),std::midpoint(a.y,b.y),0};double dx=b.x-a.x,dy=b.y-a.y,h=std::hypot(dx,dy);
        double cross=dx*(inside.y-a.y)-dy*(inside.x-a.x),sign=cross>0?1.:-1.;
        require(!seam.elevation(mid.x+sign*dy/h*1e-6,mid.y-sign*dx/h*1e-6));
    }else if(test=="grade") {
        const std::vector<Vec3> r{{0,0,0},{100,0,10}};auto m=measure(r);near(m.horizontal_m,100);near(m.max_grade_percent,10);
        Parameters p;p.max_grade_percent=9;require(has(validate(r,flat(),p),"grade_limit"));
    }else if(test=="diagnostics") {
        auto message=[&](std::vector<Vec3> route,Parameters p,const Terrain& terrain,std::span<const AlignmentCurve> curves={}){
            auto v=validate(route,terrain,p,curves);
            require(!v.checked_constraints_pass());require(v.diagnostics.size()==v.violations.size());
            try{estimate_cost(route,terrain,p,UnitCosts{},curves);throw std::runtime_error("expected rejection");}
            catch(const std::invalid_argument& e){require(std::string(e.what())==validation_failure_message(v));}
            return validation_failure_message(v);
        };
        Parameters p;p.max_grade_percent=10;
        auto text=message({{1,1,0},{11,1,0},{21,1,2}},p,flat());
        require(text.find("Tramo 2–3")!=std::string::npos);
        require(text.find("10.000000–20.000000 m")!=std::string::npos);
        require(text.find("Pendiente: 20 %; máximo permitido: 10 %")!=std::string::npos);
        require(text.find("Inicio XYZ: (11.000000, 1.000000, 0.000000)")!=std::string::npos);
        p.max_cut_m=1;p.max_fill_m=1;
        text=message({{1,1,-2},{11,1,-2}},p,flat());require(text.find("Corte: 2 m")!=std::string::npos);
        text=message({{1,1,2},{11,1,2}},p,flat());require(text.find("Relleno: 2 m")!=std::string::npos);
        p.mode=DesignMode::surface;
        text=message({{1,1,0.1},{11,1,0.1}},p,flat());require(text.find("Separación respecto al terreno: 0.1 m; tolerancia: 0.01 m")!=std::string::npos);
        text=message({{1,1,0},{90,90,0}},p,flat());require(text.find("Cobertura del terreno")!=std::string::npos);
        // Exact TIN boundaries must locate failures even when regular sampling skips them.
        Terrain ridge({{0,0,0},{5,0,3},{10,0,0},{0,5,0},{5,5,3},{10,5,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        p.sample_spacing_m=100;p.mode=DesignMode::earthwork;
        text=message({{0,1,0},{10,1,0}},p,ridge);require(text.find("Corte:")!=std::string::npos);
        near(validate(std::vector<Vec3>{{0,1,0},{10,1,0}},ridge,p).max_cut_m,3);
        p=Parameters{};p.min_radius_m=5;
        text=message({{1,1,0},{5,1,0},{5,5,0}},p,flat());require(text.find("Cambio de dirección")!=std::string::npos);
        require(text.find("Tramo 1–3")!=std::string::npos);
        const std::vector<Vec3> line{{1,1,0},{5,1,0}};
        const std::vector<AlignmentCurve> bad{{0,99,{0,0,0},30}};
        text=message(line,p,flat(),bad);require(text.find("Ubicación no disponible")!=std::string::npos);
        require(validate(line,flat(),Parameters{}).diagnostics.empty());
    }else if(test=="invalid") {
        rejects([]{measure(std::vector<Vec3>{{0,0,0},{0,0,1}});});
        rejects([]{measure(std::vector<Vec3>{{0,0,0},{1,0,std::numeric_limits<double>::quiet_NaN()}});});
        Parameters p;p.sample_spacing_m=0;rejects([&]{validate(std::vector<Vec3>{{1,1,0},{2,1,0}},flat(),p);});
        p.sample_spacing_m=1;p.max_samples=2;rejects([&]{validate(std::vector<Vec3>{{1,1,0},{4,1,0}},flat(),p);});
    }else if(test=="mesh") {
        rejects([]{Terrain({{0,0,0}},{{0,1,2}});});
        rejects([]{Terrain({{0,0,0},{1,0,1},{2,0,2}},{{0,1,2}});});
        near(*flat().elevation(10,10),0);require(!flat().elevation(90,90));
    }else if(test=="hole") {
        Terrain t({{0,0,0},{2,0,0},{0,2,0},{4,0,0},{6,0,0},{6,2,0}},{{0,1,2},{3,4,5}});
        require(!t.elevation(3,0));Parameters p;p.sample_spacing_m=0.25;
        require(has(validate(std::vector<Vec3>{{1,0,0},{5,0,0}},t,p),"terrain_missing_or_ambiguous"));
    }else if(test=="overlap") {
        Terrain t({{0,0,0},{2,0,0},{0,2,0},{0,0,1},{2,0,1},{0,2,1}},{{0,1,2},{3,4,5}});
        require(!t.elevation(0.5,0.5));
    }else if(test=="cut_fill") {
        Parameters p;p.max_fill_m=1;p.max_cut_m=3;
        require(has(validate(std::vector<Vec3>{{1,1,2},{5,1,2}},flat(),p),"fill_limit"));
        auto r=validate(std::vector<Vec3>{{1,1,-2},{5,1,-2}},flat(),p);require(r.checked_constraints_pass());near(r.max_cut_m,2);
        p.max_cut_m=1;require(has(validate(std::vector<Vec3>{{1,1,-2},{5,1,-2}},flat(),p),"cut_limit"));
    }else if(test=="surface") {
        Parameters p;p.mode=DesignMode::surface;
        require(has(validate(std::vector<Vec3>{{1,1,1},{5,1,1}},flat(),p),"surface_separation"));
    }else if(test=="incomplete") {
        auto r=validate(std::vector<Vec3>{{1,1,0},{5,1,0}},flat(),Parameters{});
        require(r.checked_constraints_pass());require(!r.all_constraints_pass());require(r.not_evaluated.size()==8);
    }else if(test=="economics") {
        Terrain t({{0,0,0},{50,0,3},{100,0,0},{0,30,0},{50,30,0},{100,30,0}},
                  {{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        const std::vector<Vec3> direct{{0,0,0},{100,0,0}}, detour{{0,0,0},{0,30,0},{100,30,0},{100,0,0}};
        Parameters p;p.max_grade_percent=2;p.max_cut_m=5;p.max_fill_m=0;p.road_width_m=1;
        UnitCosts c;c.road_per_m2=10;c.soil_cut_per_m3=1;
        auto d=estimate_cost(direct,t,p,c),b=estimate_cost(detour,t,p,c);
        near(d.cut_m3,150);near(d.total,1150);near(b.total,1600);require(d.total<b.total);
        require(!d.validation.all_constraints_pass());
        c.soil_cut_per_m3=10;require(estimate_cost(direct,t,p,c).total>b.total);
        c.rock_fraction=1.1;rejects([&]{estimate_cost(direct,t,p,c);});
        p.max_cut_m=2;c.rock_fraction=0;rejects([&]{estimate_cost(direct,t,p,c);});
        p.max_cut_m=5;p.mode=DesignMode::surface;rejects([&]{estimate_cost(direct,t,p,c);});
    }else if(test=="cost_balance") {
        Parameters p;p.max_grade_percent=100;p.road_width_m=2;p.sample_spacing_m=10;
        UnitCosts c;c.reusable_cut_fraction=0.5;c.fill_placement_per_m3=2;c.imported_material_per_m3=3;
        c.disposal_per_m3=4;c.haul_per_m3_km=5;c.internal_haul_km=1;c.import_haul_km=2;c.disposal_haul_km=3;
        auto r=estimate_cost(std::vector<Vec3>{{1,1,-1},{11,1,1}},flat(),p,c);
        near(r.cut_m3,5);near(r.fill_m3,5);near(r.reused_m3,2.5);near(r.imported_m3,2.5);near(r.surplus_m3,2.5);
        near(r.haul,75);near(r.total,102.5);
    }else if(test=="backend") {
        const std::vector<Vec3> route{{0,0,0},{10,0,1},{20,0,-1}};
        auto g=grades_cpu(route);near(g[0],10);near(g[1],20);
    }else throw std::runtime_error("unknown test");
    std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
