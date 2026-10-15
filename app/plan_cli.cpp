#include "routemine/config.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
int plan_command(const char* terrain_path,const char* request_path,const char* output_path,bool basic,bool show_only) {
    try {
        using namespace routemine;
        std::ifstream mesh_input(terrain_path);if(!mesh_input)throw std::runtime_error("Cannot open terrain OBJ");
        auto terrain=load_obj(mesh_input);
        std::ifstream input(request_path);if(!input)throw std::runtime_error("Cannot open request");
        auto config=resolve_configuration(terrain,terrain_path,read_settings(input),!basic);
        std::cout<<"# EFFECTIVE CONFIGURATION\n";write_configuration(std::cout,config);
        if(show_only)return 0;
        std::filesystem::path output(output_path),project=std::string(output_path)+".project.conf";
        auto protect_inputs=[&](const std::filesystem::path& path){
            for(const auto* source:{terrain_path,request_path}) {
                if(std::filesystem::absolute(path).lexically_normal()==std::filesystem::absolute(source).lexically_normal() ||
                    (std::filesystem::exists(path)&&std::filesystem::equivalent(path,source)))
                    throw std::invalid_argument("Output/project snapshot must differ from inputs; choose another output path");
            }
        };
        protect_inputs(output);protect_inputs(project);
        std::ofstream snapshot(project);if(!snapshot)throw std::runtime_error("Cannot save effective project configuration");
        write_configuration(snapshot,config);snapshot.close();if(!snapshot)throw std::runtime_error("Project configuration write failed");
        std::cout<<"# effective_config_saved="<<project.string()<<'\n';
        auto result=plan_cpu(terrain,config.start,config.finish,config.parameters,config.costs,config.search);
        std::cout<<"expanded="<<result.expanded<<" edges_checked="<<result.edges_checked<<'\n';
        if(result.status!=SearchStatus::found){std::cout<<(result.status==SearchStatus::no_path?"status=no_path_in_lattice\n":"status=budget_exceeded\n");return result.status==SearchStatus::no_path?3:4;}
        std::ofstream csv(output);if(!csv)throw std::runtime_error("Cannot open route output");
        csv<<std::setprecision(17)<<"X,Y,Z\n";for(auto q:result.route)csv<<q.x<<','<<q.y<<','<<q.z<<'\n';
        csv.close();if(!csv)throw std::runtime_error("Route output write failed");
        const auto& e=*result.estimate;
        std::cout<<std::setprecision(12)<<"status=preliminary_discrete_corridor\npoints="<<result.route.size()
            <<"\nhorizontal_m="<<e.validation.metrics.horizontal_m<<"\nmax_grade_percent="<<e.validation.metrics.max_grade_percent
            <<"\ncenterline_cut_m3="<<e.cut_m3<<"\ncenterline_fill_m3="<<e.fill_m3<<"\npreliminary_cost_CU="<<e.total
            <<"\nall_constraints_pass="<<std::boolalpha<<e.validation.all_constraints_pass()<<'\n';
        for(const auto& item:e.validation.not_evaluated)std::cout<<"not_evaluated="<<item<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
