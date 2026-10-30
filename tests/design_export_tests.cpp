#include "../app/design_export.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void require(bool value){if(!value)throw std::runtime_error("Design export assertion failed");}
std::size_t count(const std::string& text,const std::string& token){std::size_t n=0,pos=0;while((pos=text.find(token,pos))!=std::string::npos){++n;pos+=token.size();}return n;}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}require(bad);}
int main(){try{
    DesignReview review;SearchResult a,b;a.route={{0,0,0},{20,0,1}};b.route={{100,0,4},{120,0,5}};
    review.sections={{a,0,20},{b,100,120}};review.gaps={{20,100,a.route.back(),b.route.front(),"No connection"}};
    std::ostringstream csv,dxf;write_design_review_csv(csv,review);write_design_review_dxf(dxf,review);auto rows=csv.str(),drawing=dxf.str();
    require(rows.find("Section_ID,Point_ID,")==0&&count(rows,"SECTION_1,")==2&&count(rows,"SECTION_2,")==2);
    require(rows.find("SECTION_2,1,0,100,0,4,0,")!=std::string::npos);
    require(count(drawing,"0\nPOLYLINE\n")==2&&count(drawing,"0\nSEQEND\n")==2&&count(drawing,"0\nVERTEX\n")==4);
    require(count(drawing,"0\nPOINT\n")==2&&drawing.find("0\nLINE\n")==std::string::npos);
    require(drawing.find("0\nSEQEND\n8\nROUTETEC_VALIDO_CORTO\n999\nSECTION_2\n0\nPOLYLINE\n")!=std::string::npos);
    require(drawing.find("ROUTETEC_HUECO\n70\n0\n62\n30")!=std::string::npos);
    // A complete candidate remains its own part, not a join of the two fragments.
    review.connected=a;std::ostringstream connected;write_design_review_csv(connected,review);require(count(connected.str(),"CONNECTED,")==2);
    review.connected.reset();SearchResult alternative;alternative.route={{0,0,0},{10,10,0.5},{20,0,1}};review.sections.push_back({alternative,0,20});
    std::ostringstream alternatives;write_design_review_csv(alternatives,review);require(count(alternatives.str(),"VERIFICADO_ALTERNATIVO")==3);
    // No shortest-path comparison across different endpoints/station coverage.
    require(count(alternatives.str(),"VERIFICADO_PREFERIDO")==4);
    DesignReview empty_review;std::vector<SearchResult> branches{a,b};std::ostringstream branch_csv,branch_dxf;
    write_design_review_csv(branch_csv,empty_review,branches);write_design_review_dxf(branch_dxf,empty_review,branches);
    require(count(branch_csv.str(),"BRANCH_1,")==2&&count(branch_csv.str(),"BRANCH_2,")==2);
    require(branch_csv.str().find("BRANCH_2,1,0,100,0,4,0,VERIFICADO_RAMA_PREFERIDA,,\n")!=std::string::npos);
    require(count(branch_dxf.str(),"0\nPOLYLINE\n")==2&&count(branch_dxf.str(),"0\nSEQEND\n")==2);
    require(branch_dxf.str().find("999\nBRANCH_2\n0\nPOLYLINE\n")!=std::string::npos);
    require(branch_dxf.str().find("0\nLINE\n")==std::string::npos&&branch_dxf.str().find("GAP_")==std::string::npos);
    branches[1].route[0].z=std::numeric_limits<double>::infinity();rejects([&]{std::ostringstream out;write_design_review_csv(out,empty_review,branches);});
    review.sections[0].candidate.route[0].x=std::numeric_limits<double>::infinity();rejects([&]{std::ostringstream out;write_design_review_csv(out,review);});
    review.sections[0].candidate=a;review.sections[0].candidate.route.resize(1);rejects([&]{std::ostringstream out;write_design_review_dxf(out,review);});
    review.sections[0].candidate=a;review.gaps[0].end.z=std::numeric_limits<double>::quiet_NaN();rejects([&]{std::ostringstream out;write_design_review_dxf(out,review);});
    std::cout<<"design_export: PASS separate parts, local stations, no gap edges, preferred equivalence, finite geometry\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
