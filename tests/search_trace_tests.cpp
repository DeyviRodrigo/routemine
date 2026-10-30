#include "../app/search_trace.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value){if(!value)throw std::runtime_error("Trace mailbox assertion failed");}
int main(){try{
    SearchTrace trace;
    std::array<routemine::Vec3,2> edge{{{500000,8000000,100},{500010,8000000,101}}};
    std::thread worker([&]{for(int i=0;i<4000;++i)trace.record(edge,false);});worker.join();
    auto batch=trace.take();check(batch.explored==4000&&batch.partial.size()==3000);
    check(trace.take().partial.empty());
    std::vector<routemine::Vec3> path(10000);
    for(std::size_t i=0;i<path.size();++i)path[i]={static_cast<double>(i),0,0};
    trace.record(path,true);trace.record(path,true);
    batch=trace.take();check(batch.complete.size()==1&&batch.connected==1);
    check(batch.complete.front().size()==4096&&batch.complete.front().front().x==0&&batch.complete.front().back().x==9999);
    for(int i=0;i<20;++i){edge[0].z=i;trace.record(edge,true);}
    batch=trace.take();check(batch.complete.size()==16&&batch.connected==20);
    check(batch.complete.front().front().z==4);
    SearchTrace history;
    for(int i=0;i<4000;++i){edge={{{500000.0+i*3,8000000,100},{500001.0+i*3,8000000,100}}};history.record(edge,false);}
    batch=history.take();check(batch.partial.size()==3000&&batch.footprint_changed&&batch.footprint.size()==4000);
    auto contains=[&](double x){return std::any_of(batch.footprint.begin(),batch.footprint.end(),[&](const auto& e){return e[0].x==x;});};
    check(contains(500000)&&contains(511997));
    check(!history.take().footprint_changed);
    // A later attempt at the other end must retain the first region.
    edge={{{520000,8000000,100},{520001,8000000,100}}};history.record(edge,false);
    batch=history.take();check(batch.footprint.size()==4001&&contains(500000)&&contains(520000));
    // Keep memory bounded by coarsening the spatial history, not by time eviction.
    for(std::size_t i=0;i<SearchTrace::footprint_limit+4000;++i){edge={{{600000.0+i*3,8000000,100},{600001.0+i*3,8000000,100}}};history.record(edge,false);}
    batch=history.take();check(batch.footprint.size()<=SearchTrace::footprint_limit);
    check(std::any_of(batch.footprint.begin(),batch.footprint.end(),[](const auto& e){return e[0].x<500010;}));
    check(std::any_of(batch.footprint.begin(),batch.footprint.end(),[](const auto& e){return e[0].x>800000;}));
    // Complete routes keep their own spatial archive after the recent mailbox
    // and the GUI's alternative list have both reached their display limits.
    SearchTrace verified;
    edge={{{499000,8000100,100},{499001,8000100,100}}};verified.record(edge,false);verified.take();
    for(int i=0;i<300;++i){edge={{{500000.0+i*3,8000000,100},{500001.0+i*3,8000000,100}}};verified.record(edge,true);}
    batch=verified.take();check(batch.complete.size()==16&&batch.connected==300&&batch.verified_footprint_changed&&batch.verified_footprint.size()==300);
    check(!batch.footprint_changed&&batch.footprint.empty());
    auto contains_verified=[&](double x){return std::any_of(batch.verified_footprint.begin(),batch.verified_footprint.end(),[&](const auto& e){return e[0].x==x;});};
    check(contains_verified(500000)&&contains_verified(500897));
    check(!verified.take().verified_footprint_changed);
    edge={{{520000,8000000,100},{520001,8000000,100}}};verified.record(edge,true);batch=verified.take();
    check(batch.verified_footprint.size()==301&&contains_verified(500000)&&contains_verified(520000));
    for(std::size_t i=0;i<SearchTrace::footprint_limit+4000;++i){edge={{{600000.0+i*3,8000000,100},{600001.0+i*3,8000000,100}}};verified.record(edge,true);}
    batch=verified.take();check(batch.complete.size()==16&&batch.verified_footprint.size()<=SearchTrace::footprint_limit);
    check(std::any_of(batch.verified_footprint.begin(),batch.verified_footprint.end(),[](const auto& e){return e[0].x<500010;}));
    check(std::any_of(batch.verified_footprint.begin(),batch.verified_footprint.end(),[](const auto& e){return e[0].x>800000;}));
    check(verified.spacing==2&&verified.verified_spacing>2&&verified.footprint.size()==1);
    // A long route is sampled for the recent preview, but the archive keeps
    // actual original edges instead of chords between sampled-away vertices.
    SearchTrace original_edges;for(std::size_t i=0;i<path.size();++i)path[i]={static_cast<double>(i),static_cast<double>(i%2)*.1,0};
    original_edges.record(path,true);batch=original_edges.take();
    check(batch.complete.front().size()==4096&&batch.verified_footprint_changed);
    check(std::all_of(batch.verified_footprint.begin(),batch.verified_footprint.end(),[](const auto& e){return e[1].x-e[0].x==1;}));
    std::cout<<"search_trace: PASS bounded mailbox, worker handoff, endpoint-preserving sampling, duplicate filtering\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
