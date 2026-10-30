#pragma once
#include "routemine/design_review.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string>
#include <span>

namespace design_export_detail {
struct Part {const routemine::SearchResult* candidate;std::string id;double first,last;bool shortest;bool anchored{true};};
inline bool same_endpoint(routemine::Vec3 a,routemine::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y)<=1e-6&&std::abs(a.z-b.z)<=1e-6;}
inline void stations(double first,double last){if(!std::isfinite(first)||!std::isfinite(last)||first<0||last<=first)throw std::invalid_argument("Invalid design export station range");}
inline std::vector<Part> parts(const routemine::DesignReview& review,std::span<const routemine::SearchResult> branches={}){
    std::vector<Part> result;double last_station=0;
    for(std::size_t i=0;i<review.sections.size();++i){const auto& s=review.sections[i];stations(s.first_station_m,s.last_station_m);
        (void)routemine::measure(s.candidate.route);last_station=std::max(last_station,s.last_station_m);bool shortest=true;
        const double length=routemine::measure(s.candidate.route).horizontal_m;
        for(std::size_t j=0;j<review.sections.size();++j){if(i==j)continue;const auto& other=review.sections[j];
            if(std::abs(s.first_station_m-other.first_station_m)>1e-6||std::abs(s.last_station_m-other.last_station_m)>1e-6)continue;
            (void)routemine::measure(other.candidate.route);
            if(!same_endpoint(s.candidate.route.front(),other.candidate.route.front())||!same_endpoint(s.candidate.route.back(),other.candidate.route.back()))continue;
            const double alternative=routemine::measure(other.candidate.route).horizontal_m;
            if(alternative<length-1e-8||(std::abs(alternative-length)<=1e-8&&j<i)){shortest=false;break;}
        }
        result.push_back({&s.candidate,"SECTION_"+std::to_string(i+1),s.first_station_m,s.last_station_m,shortest});
    }
    for(const auto& gap:review.gaps){stations(gap.first_station_m,gap.last_station_m);if(!routemine::finite(gap.start)||!routemine::finite(gap.end))throw std::invalid_argument("Invalid design export gap endpoint");last_station=std::max(last_station,gap.last_station_m);}
    if(review.connected){const auto metrics=routemine::measure(review.connected->route);result.push_back({&*review.connected,"CONNECTED",0,std::max(last_station,metrics.horizontal_m),true});}
    if(branches.size()>256)throw std::invalid_argument("Too many independent branch exports");
    for(std::size_t i=0;i<branches.size();++i){const auto& branch=branches[i];const auto length=routemine::measure(branch.route).horizontal_m;bool shortest=true;
        for(std::size_t j=0;j<branches.size();++j){if(i==j)continue;const auto& other=branches[j];(void)routemine::measure(other.route);
            if(!same_endpoint(branch.route.front(),other.route.front())||!same_endpoint(branch.route.back(),other.route.back()))continue;
            const auto alternative=routemine::measure(other.route).horizontal_m;
            if(alternative<length-1e-8||(std::abs(alternative-length)<=1e-8&&j<i)){shortest=false;break;}
        }
        result.push_back({&branch,"BRANCH_"+std::to_string(i+1),0,0,shortest,false});
    }
    return result;
}
}

// The caller must validate the review against its terrain and original design.
// Every part has its own point numbering/station origin; gaps never become edges.
inline void write_design_review_csv(std::ostream& out,const routemine::DesignReview& review,std::span<const routemine::SearchResult> branches={}){
    auto parts=design_export_detail::parts(review,branches);out.imbue(std::locale::classic());out<<std::setprecision(17)
        <<"Section_ID,Point_ID,Progresiva_tramo_m,X,Y,Z,Pendiente_tramo_pct,Clase,Origen_base_m,Fin_base_m\n";
    for(const auto& part:parts){double station=0;
        for(std::size_t i=0;i<part.candidate->route.size();++i){auto p=part.candidate->route[i];double grade=0;
            if(i){auto before=part.candidate->route[i-1];double horizontal=std::hypot(p.x-before.x,p.y-before.y);station+=horizontal;grade=100*(p.z-before.z)/horizontal;}
            out<<part.id<<','<<i+1<<','<<station<<','<<p.x<<','<<p.y<<','<<p.z<<','<<grade<<','
                <<(part.anchored?(part.shortest?"VERIFICADO_PREFERIDO":"VERIFICADO_ALTERNATIVO"):(part.shortest?"VERIFICADO_RAMA_PREFERIDA":"VERIFICADO_RAMA_ALTERNATIVA"))<<',';
            if(part.anchored)out<<part.first;out<<',';if(part.anchored)out<<part.last;out<<'\n';
        }
    }
    if(!out)throw std::runtime_error("Design review CSV write failed");
}
inline void write_design_review_dxf(std::ostream& out,const routemine::DesignReview& review,std::span<const routemine::SearchResult> branches={}){
    auto parts=design_export_detail::parts(review,branches);out.imbue(std::locale::classic());out<<std::setprecision(17)
        <<"0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n"
          "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n3\n"
          "0\nLAYER\n2\nROUTETEC_VALIDO_CORTO\n70\n0\n62\n4\n6\nCONTINUOUS\n"
          "0\nLAYER\n2\nROUTETEC_VALIDO_ALTERNATIVO\n70\n0\n62\n5\n6\nCONTINUOUS\n"
          "0\nLAYER\n2\nROUTETEC_HUECO\n70\n0\n62\n30\n6\nCONTINUOUS\n"
          "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    for(const auto& part:parts){auto layer=part.shortest?"ROUTETEC_VALIDO_CORTO":"ROUTETEC_VALIDO_ALTERNATIVO";
        out<<"999\n"<<part.id<<"\n0\nPOLYLINE\n8\n"<<layer<<"\n66\n1\n70\n8\n10\n0\n20\n0\n30\n0\n";
        for(auto p:part.candidate->route)out<<"0\nVERTEX\n8\n"<<layer<<"\n70\n32\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';
        out<<"0\nSEQEND\n8\n"<<layer<<'\n';
    }
    for(std::size_t i=0;i<review.gaps.size();++i){const auto& gap=review.gaps[i];out<<"999\nGAP_"<<i+1<<'\n';
        for(auto p:{gap.start,gap.end})out<<"0\nPOINT\n8\nROUTETEC_HUECO\n10\n"<<p.x<<"\n20\n"<<p.y<<"\n30\n"<<p.z<<'\n';
    }
    out<<"0\nENDSEC\n0\nEOF\n";if(!out)throw std::runtime_error("Design review DXF write failed");
}
