#include "routemine/core.hpp"
#include "stream_progress.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace routemine {
namespace {
std::string trim(std::string s) {
    auto first=s.find_first_not_of(" \t\r");
    return first==std::string::npos?std::string{}:s.substr(first,s.find_last_not_of(" \t\r")-first+1);
}
double number(const std::string& value) {
    std::istringstream row(value);double v;std::string extra;
    if(!(row>>v)||row>>extra||!std::isfinite(v))throw std::invalid_argument("Invalid DXF coordinate: "+value);
    return v;
}
}
Terrain load_dxf(std::istream& input,DxfImportReport* report,ProgressCallback progress) {
    StreamProgress work(input,std::move(progress));
    if(report)*report={};
    std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
    std::string section,type;bool section_name=false,entities=false,ended=false,paper=false;
    std::array<Vec3,4> points{};std::array<std::array<bool,3>,4> seen{};
    auto flush=[&] {
        if(section!="ENTITIES"||type!="3DFACE"||paper)return;
        if(report)++report->faces_read;
        for(std::size_t i=0;i<3;++i)
            if(!seen[i][0]||!seen[i][1])throw std::invalid_argument("Incomplete DXF 3DFACE");
        if(!seen[3][0]&&!seen[3][1]&&!seen[3][2])points[3]=points[2];
        else if(!seen[3][0]||!seen[3][1])throw std::invalid_argument("Incomplete DXF fourth vertex");
        auto same=[](Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
        auto cross=[](Vec3 a,Vec3 b,Vec3 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        const bool quad=!same(points[2],points[3]);
        if(!quad&&std::abs(cross(points[0],points[1],points[2]))<=1e-12&&report) {
            ++report->degenerate_faces_skipped;return;
        }
        if(quad) {
            const double orientation=cross(points[0],points[1],points[2]);
            for(std::size_t i=0;i<4;++i)
                if(cross(points[i],points[(i+1)%4],points[(i+2)%4])*orientation<=0)
                    throw std::invalid_argument("DXF quad must be convex and nondegenerate in XY");
            auto a=to_local(points[1],points[0]),b=to_local(points[2],points[0]),c=to_local(points[3],points[0]);
            Vec3 n{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
            double error=std::abs(n.x*c.x+n.y*c.y+n.z*c.z);
            if(error>1e-7*std::hypot(std::hypot(n.x,n.y),n.z)*std::max(1.0,std::hypot(std::hypot(c.x,c.y),c.z)))
                throw std::invalid_argument("Nonplanar DXF quad: triangulate it explicitly before import");
        }
        auto base=vertices.size();
        vertices.insert(vertices.end(),points.begin(),points.begin()+(quad?4:3));
        faces.push_back({base,base+1,base+2});
        if(quad)faces.push_back({base,base+2,base+3});
        if(report)report->triangles_loaded+=quad?2:1;
    };
    std::string code_line,value;std::size_t line=0;
    while(std::getline(input,code_line)) {
        work.reading();
        ++line;if(!std::getline(input,value))throw std::invalid_argument("Truncated DXF pair");++line;
        code_line=trim(code_line);value=trim(value);int code{};
        auto parsed=std::from_chars(code_line.data(),code_line.data()+code_line.size(),code);
        if(parsed.ec!=std::errc{}||parsed.ptr!=code_line.data()+code_line.size())
            throw std::invalid_argument("Expected ASCII DXF group at line "+std::to_string(line-1));
        if(code==0) {
            flush();type=value;points={};seen={};paper=false;
            if(value=="SECTION") { if(!section.empty())throw std::invalid_argument("Nested DXF section");section_name=true; }
            else if(value=="ENDSEC") {section.clear();section_name=false;}
            else if(value=="EOF") {ended=true;break;}
            else if(section=="ENTITIES" && (value=="MESH"||value=="POLYLINE"||value=="LWPOLYLINE"||value=="INSERT"||value=="SOLID"||value=="TRACE"||value=="3DSOLID"||value=="BODY"||value=="REGION"||value=="SURFACE"||value=="ACAD_PROXY_ENTITY"))
                throw std::invalid_argument("Unsupported DXF geometry: "+value+". Export terrain as model-space 3DFACE.");
        } else if(code==2&&section_name) {
            section=value;section_name=false;if(section=="ENTITIES")entities=true;
        } else if(section=="ENTITIES"&&type=="3DFACE") {
            if(code==67)paper=value!="0";
            for(int axis=0;axis<3;++axis)for(int i=0;i<4;++i)if(code==10+10*axis+i) {
                if(seen[i][axis])throw std::invalid_argument("Duplicate DXF vertex coordinate");
                seen[i][axis]=true;double v=number(value);
                if(axis==0)points[i].x=v;else if(axis==1)points[i].y=v;else points[i].z=v;
            }
        }
    }
    if(input.bad())throw std::runtime_error("DXF read failed");
    if(!ended||!section.empty()||!entities)throw std::invalid_argument("Incomplete ASCII DXF document");
    work.report(85);Terrain terrain(std::move(vertices),std::move(faces));work.report(100);return terrain;
}
}
