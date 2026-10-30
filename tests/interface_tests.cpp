#include "routemine/config.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool v){if(!v)throw std::runtime_error("assertion failed");}
void near(double a,double b){check(std::abs(a-b)<1e-7);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad);}
std::string document(const std::string& entities){return "0\nSECTION\n2\nENTITIES\n"+entities+"0\nENDSEC\n0\nEOF\n";}
std::string face(Vec3 a,Vec3 b,Vec3 c,std::optional<Vec3> d={}) {
    std::ostringstream out;out.precision(17);out<<"0\n3DFACE\n8\nTIN\n";
    Vec3 points[]{a,b,c,d.value_or(c)};
    for(int i=0;i<4;++i)out<<10+i<<'\n'<<points[i].x<<'\n'<<20+i<<'\n'<<points[i].y<<'\n'<<30+i<<'\n'<<points[i].z<<'\n';
    return out.str();
}
int main(int argc,char** argv){try {
    check(argc==2||argc==3);std::string name=argv[1];
    if(name=="profile_roundoff") {
        std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
        for(int y=0;y<=20;++y)for(int x=0;x<=20;++x)vertices.push_back({793000.123+x*20.37,8440000.456+y*20.19,3500+y*2.0});
        for(std::size_t y=0;y<20;++y)for(std::size_t x=0;x<20;++x){auto i=y*21+x;faces.push_back({i,i+1,i+21});faces.push_back({i+1,i+22,i+21});}
        DxfImportReport report;auto t=argc==3?load_terrain(std::filesystem::u8path(argv[2]),&report):Terrain(vertices,faces);
        Vec3 a=argc==3?Vec3{793219.29991371243,8442359.4311684202,4447.5}:Vec3{793019.29991371243,8440359.4311684202,0};
        Vec3 b=argc==3?Vec3{793305.50737256743,8440467.7877923045,3435}:Vec3{793305.50737256743,8440067.7877923045,0};
        a.z=*t.elevation(a.x,a.y);b.z=*t.elevation(b.x,b.y);
        for(auto endpoints:{std::array<Vec3,2>{a,b},std::array<Vec3,2>{b,a}}){
            auto profile=t.segment_profile(endpoints[0],endpoints[1]);check(bool(profile));
            std::cout<<"profile_points="<<profile->size()<<std::endl;
            auto metrics=measure(*profile);check(std::isfinite(metrics.max_grade_percent));
            check(profile->front().x==endpoints[0].x&&profile->back().x==endpoints[1].x);
            Parameters p;p.max_grade_percent=100;auto v=validate(*profile,t,p);if(argc==2)check(v.checked_constraints_pass());
        }
    } else if(name=="dxf_triangles") {
        std::istringstream input(document(face({500000,8000000,1000},{500010,8000000,1001},{500000,8000010,1000})));
        DxfImportReport report;auto t=load_dxf(input,&report);near(*t.elevation(500001,8000001),1000.1);
        check(report.faces_read==1&&report.triangles_loaded==1&&report.degenerate_faces_skipped==0);
        near(t.vertices()[0].x,500000);check(!t.elevation(500009,8000009));
    } else if(name=="dxf_quads") {
        std::istringstream input(document(face({0,0,0},{10,0,1},{10,10,1},Vec3{0,10,0})));
        auto t=load_dxf(input);check(t.faces().size()==2);near(*t.elevation(5,5),0.5);
        rejects([]{std::istringstream in(document(face({0,0,0},{10,0,1},{10,10,1},Vec3{0,10,5})));load_dxf(in);});
        rejects([]{std::istringstream in(document(face({0,0,0},{10,0,0},{2,2,0},Vec3{0,10,0})));load_dxf(in);});
    } else if(name=="dxf_invalid") {
        for(auto text:{"AutoCAD Binary DXF\r\n", "0\nSECTION\n2\nENTITIES\n0", "0\nEOF\n"})rejects([&]{std::istringstream in(text);load_dxf(in);});
        for(auto entity:{"MESH","INSERT","POLYLINE","SOLID","TRACE"})rejects([&]{std::istringstream in(document("0\n"+std::string(entity)+"\n"));load_dxf(in);});
        rejects([]{std::istringstream in(document("0\n3DFACE\n10\n0\n20\n0\n"));load_dxf(in);});
        rejects([]{std::istringstream in(document("0\n3DFACE\n10\nnan\n"));load_dxf(in);});
        rejects([]{std::istringstream in(document(face({0,0,0},{1,0,0},{2,0,0})));load_dxf(in);});
    } else if(name=="dxf_report") {
        auto valid=face({0,0,0},{10,0,0},{0,10,0});auto degenerate=face({20,0,0},{21,0,0},{22,0,0});
        DxfImportReport report;std::istringstream in(document(valid+degenerate));auto t=load_dxf(in,&report);
        check(report.faces_read==2&&report.triangles_loaded==1&&report.degenerate_faces_skipped==1);check(!t.elevation(21,0));
        std::istringstream paper(document(valid+"0\n3DFACE\n67\n1\n"));auto p=load_dxf(paper);check(p.faces().size()==1);
        std::istringstream blocks("0\nSECTION\n2\nBLOCKS\n"+degenerate+"0\nENDSEC\n"+document(valid));auto b=load_dxf(blocks);check(b.faces().size()==1);
    } else if(name=="spatial_index") {
        std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
        for(int y=0;y<10;++y)for(int x=0;x<10;++x) {
            if(x==5)continue;auto id=vertices.size();double xx=500000+x,yy=8000000+y;
            vertices.insert(vertices.end(),{{xx,yy,x*0.1},{xx+1,yy,(x+1)*0.1},{xx,yy+1,x*0.1},{xx+1,yy+1,(x+1)*0.1}});
            faces.push_back({id,id+1,id+2});faces.push_back({id+1,id+3,id+2});
        }
        Terrain t(vertices,faces);for(int y=0;y<10;++y)for(int x=0;x<10;++x) {
            auto z=t.elevation(500000+x+0.25,8000000+y+0.25);if(x==5)check(!z);else {check(z.has_value());near(*z,(x+0.25)*0.1);}
        }
        check(t.segment_profile({500000,8000001,0},{500004,8000001,0}).has_value());
        check(!t.segment_profile({500000,8000001,0},{500009,8000001,0}));
        auto id=vertices.size();vertices.insert(vertices.end(),{{500000,8000000,5},{500001,8000000,5},{500000,8000001,5}});faces.push_back({id,id+1,id+2});
        Terrain overlap(vertices,faces);check(!overlap.elevation(500000.25,8000000.25));
    } else if(name=="routetec_profile") {
        Terrain t({{0,0,0},{100,0,0},{0,100,0}},{{0,1,2}});auto values=routetec_interface_defaults();
        for(const auto& [key,value]:Settings{{"start_x","0"},{"start_y","0"},{"start_z","0"},{"finish_x","20"},{"finish_y","0"},{"finish_z","0"}})values[key]=value;
        auto c=resolve_configuration(t,"test.dxf",values);near(c.parameters.max_grade_percent,10);near(c.parameters.max_cut_m,2);near(c.parameters.max_fill_m,2);near(c.parameters.road_width_m,8);check(c.parameters.continuous_curves&&c.parameters.mode==DesignMode::surface);
        near(c.parameters.min_straight_m,35);near(c.parameters.min_radius_m,30);near(c.parameters.max_turn_degrees,120);
        near(c.costs.road_per_m2,1);near(c.costs.soil_cut_per_m3,0);check(c.cost_profile=="length_only");
        auto implicit_cost=values;implicit_cost.erase("cost_profile");check(resolve_configuration(t,"test.dxf",implicit_cost).cost_profile=="length_only");
        values["max_grade_percent"]="0";near(resolve_configuration(t,"test.dxf",values).parameters.max_grade_percent,0);values["max_grade_percent"]="7.5";
        auto override=resolve_configuration(t,"test.dxf",values);near(override.parameters.max_grade_percent,7.5);
        values["soil_cut_per_m3"]="20";rejects([&]{resolve_configuration(t,"test.dxf",values);});values.erase("soil_cut_per_m3");
        std::ostringstream saved;write_configuration(saved,override);std::istringstream input(saved.str());auto reloaded=resolve_configuration(t,"",read_settings(input));
        std::ostringstream again;write_configuration(again,reloaded);check(again.str()==saved.str());
    } else if(name=="route_exports") {
        std::vector<Vec3> route{{500000,8000000,1000},{500010,8000000,1001},{500020,8000000,1000}};
        std::ostringstream csv;write_route_csv(csv,route);check(csv.str().find("ID,Progresiva_m,X,Y,Z,Pendiente_tramo_pct\n")==0);
        check(csv.str().find("2,10,500010,8000000,1001,10\n")!=std::string::npos);check(csv.str().find("3,20,500020,8000000,1000,-10\n")!=std::string::npos);
        std::ostringstream dxf;write_route_dxf(dxf,route);check(dxf.str().ends_with("0\nEOF\n"));check(dxf.str().find("70\n8\n")!=std::string::npos);
        check(dxf.str().find("10\n500010\n20\n8000000\n30\n1001\n")!=std::string::npos);
        rejects([]{std::ostringstream out;write_route_csv(out,std::vector<Vec3>{{0,0,0},{0,0,1}});});
        rejects([]{std::ostringstream out;write_route_dxf(out,std::vector<Vec3>{});});
    } else if(name=="progress") {
        std::vector<int> values;
        auto progress=[&](int value){check(value>=0&&value<=100);check(values.empty()||value>=values.back());values.push_back(value);};
        auto complete=[&]{check(values.size()>3&&values.front()==0&&values.back()==100);check(values[1]>0&&values[1]<85);values.clear();};
        std::string obj="v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n";
        for(int i=0;i<4096;++i)obj+="# import progress padding\n";
        std::istringstream obj_input(obj);check(load_obj(obj_input,progress).faces().size()==1);complete();
        std::string padding;for(int i=0;i<4096;++i)padding+="999\nimport progress padding\n";
        std::istringstream dxf_input(document(padding+face({0,0,0},{10,0,0},{0,10,0})));
        check(load_dxf(dxf_input,nullptr,progress).faces().size()==1);complete();
        std::istringstream invalid("invalid\n");rejects([&]{load_obj(invalid,progress);});check(values.back()<100);
    } else if(name=="cancel_search") {
        Terrain t({{0,0,0},{10,0,0},{0,10,0}},{{0,1,2}});std::stop_source source;source.request_stop();
        auto r=plan_cpu(t,{0,0,0},{5,0,0},Parameters{},UnitCosts{},SearchOptions{},source.get_token());check(r.status==SearchStatus::cancelled&&r.route.empty());
    } else throw std::runtime_error("unknown test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
