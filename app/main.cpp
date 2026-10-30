#include "routemine/core.hpp"
#include <iomanip>
#include <iostream>
#include <string>
int plan_command(const char* terrain_path,const char* request_path,const char* output_path,bool basic=false,bool show_only=false);
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--inspect-terrain") {
        try {
            routemine::DxfImportReport report;
            auto terrain=routemine::load_terrain(std::filesystem::u8path(argv[2]),&report);auto bounds=terrain.bounds();
            std::cout<<std::setprecision(17)<<"vertices="<<terrain.vertices().size()<<"\ntriangles="<<terrain.faces().size()
                     <<"\ndegenerate_xy_faces_skipped="<<report.degenerate_faces_skipped<<'\n';
            for(auto p:bounds)std::cout<<"bounds="<<p.x<<','<<p.y<<','<<p.z<<'\n';return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    }
    if(argc==5 && std::string(argv[1])=="--plan")return plan_command(argv[2],argv[3],argv[4]);
    if(argc==5 && std::string(argv[1])=="--plan-basic")return plan_command(argv[2],argv[3],argv[4],true);
    if(argc==4 && std::string(argv[1])=="--show-config")return plan_command(argv[2],argv[3],nullptr,true,true);
    if(argc==2 && std::string(argv[1])=="--cost-demo") {
        try {
            using namespace routemine;
            Terrain t({{0,0,0},{50,0,3},{100,0,0},{0,30,0},{50,30,0},{100,30,0}},
                      {{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
            const std::vector<Vec3> direct{{0,0,0},{100,0,0}},detour{{0,0,0},{0,30,0},{100,30,0},{100,0,0}};
            Parameters p;p.max_grade_percent=2;p.max_cut_m=5;p.max_fill_m=0;p.road_width_m=1;
            UnitCosts costs;costs.road_per_m2=10;
            for(double excavation:{1.0,10.0}) {
                costs.soil_cut_per_m3=excavation;
                auto a=estimate_cost(direct,t,p,costs),b=estimate_cost(detour,t,p,costs);
                std::cout<<"synthetic_cut_price="<<excavation<<" direct_CU="<<a.total<<" detour_CU="<<b.total
                         <<" preliminary_choice="<<(a.total<b.total?"excavate":"detour")<<'\n';
            }
            std::cout<<"Illustrative prices only; vehicle geometry checks remain pending.\n";return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    }
    if(argc!=2 || std::string(argv[1])!="--demo") {
        std::cout<<"RouteTec: --demo | --cost-demo | --plan terrain.obj|dxf request.txt route.csv\n"
                 <<"--plan-basic terrain.obj|dxf endpoints.txt route.csv | --show-config terrain.obj|dxf endpoints.txt\n"
                 <<"--inspect-terrain terrain.obj|dxf\nVehicle geometry remains unverified.\n";
        return argc==1?0:2;
    }
    try {
        using namespace routemine;
        const Terrain terrain({{500000,8000000,1000},{501000,8000000,1100},{500000,8001000,1000}},{{0,1,2}});
        const std::vector<Vec3> route{{500010,8000010,1001},{500110,8000010,1011}};
        Parameters p; p.mode=DesignMode::surface;
        const auto result=validate(route,terrain,p);
        std::cout<<std::fixed<<std::setprecision(6)<<"backend=cpu\nhorizontal_m="<<result.metrics.horizontal_m
                 <<"\nmax_grade_percent="<<result.metrics.max_grade_percent<<"\nsamples="<<result.samples
                 <<"\nchecked_constraints_pass="<<std::boolalpha<<result.checked_constraints_pass()
                 <<"\nall_constraints_pass="<<result.all_constraints_pass()<<'\n';
        for(const auto& item:result.violations) std::cout<<"violation="<<item<<'\n';
        for(const auto& item:result.not_evaluated) std::cout<<"not_evaluated="<<item<<'\n';
        return result.checked_constraints_pass()?0:1;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}
}
