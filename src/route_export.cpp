#include "routemine/core.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <stdexcept>

namespace routemine {
namespace {
bool red(std::size_t edge,std::span<const SearchResult::Intervention> spans) {
    for(auto s:spans)if(edge>=s.first&&edge<s.last)return true;return false;
}
void check_spans(std::size_t count,std::span<const SearchResult::Intervention> spans) {
    std::size_t previous=0;for(auto s:spans){if(s.first<previous||s.first>=s.last||s.last>=count)throw std::invalid_argument("Invalid export intervention span");previous=s.last;}
}
}
void write_route_csv(std::ostream& out,std::span<const Vec3> route,std::span<const SearchResult::Intervention> interventions,bool projected_ramp) {
    (void)measure(route);out.imbue(std::locale::classic());
    check_spans(route.size(),interventions);out<<std::setprecision(17)<<"ID,Progresiva_m,X,Y,Z,Pendiente_tramo_pct";
    if(!interventions.empty()||projected_ramp)out<<",Tipo_tramo";out<<'\n';
    double station=0;
    for(std::size_t i=0;i<route.size();++i) {
        double grade=0;if(i) {
            auto d=to_local(route[i],route[i-1]);double h=std::hypot(d.x,d.y);
            station+=h;grade=100*d.z/h;
        }
        auto p=route[i];out<<i+1<<','<<station<<','<<p.x<<','<<p.y<<','<<p.z<<','<<grade;
        if(!interventions.empty()||projected_ramp)out<<','<<(projected_ramp?"RAMPA_PROYECTO":(i==0?"INICIO":(red(i-1,interventions)?"CORTE_NIVELACION":"SOBRE_TERRENO")));out<<'\n';
    }
    if(!out)throw std::runtime_error("CSV write failed");
}
void write_route_dxf(std::ostream& out,std::span<const Vec3> route,std::span<const SearchResult::Intervention> interventions,bool projected_ramp) {
    (void)measure(route);out.imbue(std::locale::classic());out<<std::setprecision(17);
    check_spans(route.size(),interventions);
    out<<"0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n"
         "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n4\n"
         "0\nLAYER\n2\nROUTETEC_EJE\n70\n0\n62\n2\n6\nCONTINUOUS\n"
         "0\nLAYER\n2\nROUTETEC_CORTE\n70\n0\n62\n1\n6\nCONTINUOUS\n"
         "0\nLAYER\n2\nROUTETEC_RAMPA\n70\n0\n62\n30\n6\nCONTINUOUS\n"
         "0\nLAYER\n2\nROUTETEC_A_B\n70\n0\n62\n3\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n"
         "0\nSECTION\n2\nENTITIES\n";
    std::size_t first=0;
    while(first+1<route.size()) {
        bool is_red=red(first,interventions);auto last=first+1;
        while(last+1<route.size()&&red(last,interventions)==is_red)++last;
        const char* layer=projected_ramp?"ROUTETEC_RAMPA":(is_red?"ROUTETEC_CORTE":"ROUTETEC_EJE");
        out<<"0\nPOLYLINE\n8\n"<<layer<<"\n66\n1\n70\n8\n10\n0\n20\n0\n30\n0\n";
        for(auto i=first;i<=last;++i){auto p=route[i];out<<"0\nVERTEX\n8\n"<<layer<<"\n70\n32\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';}
        out<<"0\nSEQEND\n8\n"<<layer<<'\n';first=last;
    }
    for(std::size_t i:{std::size_t{0},route.size()-1}) {
        auto p=route[i];out<<"0\nPOINT\n8\nROUTETEC_A_B\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';
        out<<"0\nTEXT\n8\nROUTETEC_A_B\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<"\n40\n10\n1\n"<<(i==0?"A":"B")<<'\n';
    }
    out<<"0\nENDSEC\n0\nEOF\n";if(!out)throw std::runtime_error("DXF write failed");
}
void write_classified_route_csv(std::ostream& out,const Terrain& terrain,std::span<const Vec3> route,double tolerance){
    auto segments=earthwork_segments(terrain,route,tolerance);out.imbue(std::locale::classic());out<<std::setprecision(17)<<"ID,Progresiva_m,X,Y,Z,Pendiente_tramo_pct,Tipo_tramo,Corte_m,Relleno_m\n";
    std::size_t id=0;double station=0;auto previous=route.front();
    auto row=[&](Vec3 p,const char* kind){auto z=terrain.elevation(p.x,p.y);if(!z)throw std::invalid_argument("Missing export terrain");double h=std::hypot(p.x-previous.x,p.y-previous.y),grade=h>1e-8?100*(p.z-previous.z)/h:0;station+=h;
        out<<++id<<','<<station<<','<<p.x<<','<<p.y<<','<<p.z<<','<<grade<<','<<kind<<','<<std::max(0.0,*z-p.z)<<','<<std::max(0.0,p.z-*z)<<'\n';previous=p;};
    row(route.front(),"INICIO");for(const auto& s:segments)row(s.last,s.kind==EarthworkKind::cut?"CORTE":s.kind==EarthworkKind::fill?"RELLENO":"SOBRE_TERRENO");
    if(!out)throw std::runtime_error("Classified CSV write failed");
}
void write_classified_route_dxf(std::ostream& out,const Terrain& terrain,std::span<const Vec3> route,double tolerance){
    auto segments=earthwork_segments(terrain,route,tolerance);out.imbue(std::locale::classic());out<<std::setprecision(17)
        <<"0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n4\n";
    for(auto [layer,color]:std::vector<std::pair<const char*,int>>{{"ROUTETEC_EJE",2},{"ROUTETEC_CORTE",1},{"ROUTETEC_RELLENO",3},{"ROUTETEC_A_B",7}})
        out<<"0\nLAYER\n2\n"<<layer<<"\n70\n0\n62\n"<<color<<"\n6\nCONTINUOUS\n";
    out<<"0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    std::size_t first=0;while(first<segments.size()){
        auto kind=segments[first].kind;auto last=first;while(last+1<segments.size()&&segments[last+1].kind==kind)++last;
        auto layer=kind==EarthworkKind::cut?"ROUTETEC_CORTE":kind==EarthworkKind::fill?"ROUTETEC_RELLENO":"ROUTETEC_EJE";
        out<<"0\nPOLYLINE\n8\n"<<layer<<"\n66\n1\n70\n8\n10\n0\n20\n0\n30\n0\n";
        auto vertex=[&](Vec3 p){out<<"0\nVERTEX\n8\n"<<layer<<"\n70\n32\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';};
        vertex(segments[first].first);for(auto i=first;i<=last;++i)vertex(segments[i].last);
        out<<"0\nSEQEND\n8\n"<<layer<<'\n';first=last+1;
    }
    for(auto i:{std::size_t{0},route.size()-1}){auto p=route[i];out<<"0\nPOINT\n8\nROUTETEC_A_B\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';}
    out<<"0\nENDSEC\n0\nEOF\n";if(!out)throw std::runtime_error("Classified DXF write failed");
}

}
