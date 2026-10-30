#pragma once
#include "routemine/core.hpp"
#include <algorithm>
#include <deque>
#include <mutex>
#include <cmath>
#include <numeric>
#include <unordered_map>

// Bounded mailbox between the planner and the GUI. Display sampling never
// changes search states, validation, costs, or exported route coordinates.
struct SearchTrace {
    using Paths=std::deque<std::vector<routemine::Vec3>>;
    using Footprint=std::vector<std::array<routemine::Vec3,2>>;
    struct Batch {Paths partial,complete;std::size_t explored{},connected{};Footprint footprint;bool footprint_changed{};Footprint verified_footprint;bool verified_footprint_changed{};};
    struct Cell {long long x,y;bool operator==(const Cell&)const=default;};
    struct CellHash {std::size_t operator()(Cell c)const{return std::hash<long long>{}(c.x)^(std::hash<long long>{}(c.y)<<1);}};
    // Spatial history survives mailbox drains and eviction of recent branches.
    // Coarsen only the display grid when needed; never discard an entire old region.
    std::unordered_map<Cell,std::array<routemine::Vec3,2>,CellHash> footprint;
    std::unordered_map<Cell,std::array<routemine::Vec3,2>,CellHash> verified_footprint;
    routemine::Vec3 origin{};double spacing{2},verified_spacing{2};bool initialized{},dirty{},verified_dirty{};
    static constexpr std::size_t footprint_limit=65536;
    Cell cell(routemine::Vec3 p,double grid_spacing)const{return {static_cast<long long>(std::floor((p.x-origin.x)/grid_spacing)),static_cast<long long>(std::floor((p.y-origin.y)/grid_spacing))};}
    Cell cell(routemine::Vec3 p)const{return cell(p,spacing);}
    void archive(std::span<const routemine::Vec3> path,bool complete=false){
        if(!initialized){origin=path.front();initialized=true;}
        auto& history=complete?verified_footprint:footprint;
        auto& grid_spacing=complete?verified_spacing:spacing;
        auto& changed=complete?verified_dirty:dirty;
        for(std::size_t i=1;i<path.size();++i){
            auto a=path[i-1],b=path[i];auto key=cell({std::midpoint(a.x,b.x),std::midpoint(a.y,b.y),0},grid_spacing);
            if(history.try_emplace(key,std::array<routemine::Vec3,2>{a,b}).second)changed=true;
            if(history.size()>footprint_limit){
                do{grid_spacing*=2;decltype(footprint) compact;
                    for(const auto& [old,edge]:history){(void)old;auto x=edge[0],y=edge[1];compact.try_emplace(cell({std::midpoint(x.x,y.x),std::midpoint(x.y,y.y),0},grid_spacing),edge);}
                    history.swap(compact);
                }while(history.size()>footprint_limit);
                changed=true;
            }
        }
    }
    std::mutex mutex;
    Batch pending;
    void record(std::span<const routemine::Vec3> path,bool complete){
        if(path.size()<2)return;
        std::lock_guard lock(mutex);
        // Archive only adjacent original vertices. Display thinning below must
        // never create a verified shortcut across curves or missing terrain.
        archive(path,complete);
        const auto count=std::min<std::size_t>(path.size(),complete?4096:64);
        std::vector<routemine::Vec3> copy;copy.reserve(count);
        for(std::size_t i=0;i<count;++i)copy.push_back(path[i*(path.size()-1)/(count-1)]);
        if(complete){
            for(const auto& old:pending.complete)if(old.size()==copy.size()&&std::equal(old.begin(),old.end(),copy.begin(),[](auto a,auto b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}))return;
        }
        auto& paths=complete?pending.complete:pending.partial;
        ++(complete?pending.connected:pending.explored);
        paths.push_back(std::move(copy));if(paths.size()>(complete?16:3000))paths.pop_front();
    }
    Batch take(){std::lock_guard lock(mutex);Batch result;std::swap(result,pending);
        if(dirty){result.footprint.reserve(footprint.size());for(const auto& [key,edge]:footprint){(void)key;result.footprint.push_back(edge);}result.footprint_changed=true;dirty=false;}
        if(verified_dirty){result.verified_footprint.reserve(verified_footprint.size());for(const auto& [key,edge]:verified_footprint){(void)key;result.verified_footprint.push_back(edge);}result.verified_footprint_changed=true;verified_dirty=false;}
        return result;}
};
