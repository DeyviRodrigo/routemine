#include "routemine/core.hpp"
#include <charconv>
#include <sstream>
#include <stdexcept>
namespace routemine {
Terrain load_obj(std::istream& input) {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::size_t,3>> faces;
    std::string line;std::size_t number=0;
    while(std::getline(input,line)) {
        ++number;line=line.substr(0,line.find('#'));
        std::istringstream row(line);std::string type;if(!(row>>type))continue;
        auto fail=[&](){throw std::invalid_argument("Unsupported/invalid OBJ at line "+std::to_string(number));};
        if(type=="v") {
            Vec3 p;std::string extra;if(!(row>>p.x>>p.y>>p.z)||row>>extra||!finite(p))fail();
            vertices.push_back(p);
        }else if(type=="f") {
            std::array<std::size_t,3> face{};std::string token;
            for(auto& id:face){
                if(!(row>>token))fail();
                // Accept plain positive or negative vertex indices only; never silently drop UV/normals.
                long long value{};auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
                if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||value==0)fail();
                if(value>0){if(static_cast<unsigned long long>(value)>vertices.size())fail();id=static_cast<std::size_t>(value-1);}
                else {if(value < -static_cast<long long>(vertices.size()))fail();id=static_cast<std::size_t>(static_cast<long long>(vertices.size())+value);}
            }
            if(row>>token)fail();faces.push_back(face);
        }else if(type=="o"||type=="g"||type=="s") {
            // Organization labels do not alter vertex coordinates or triangle connectivity.
        }else fail();
    }
    if(input.bad())throw std::runtime_error("OBJ read failed");
    return Terrain(std::move(vertices),std::move(faces));
}
}
