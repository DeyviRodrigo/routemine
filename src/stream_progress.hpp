#pragma once
#include "routemine/core.hpp"
#include <algorithm>
#include <istream>
#include <utility>

namespace routemine {
// Reading occupies 85% of import work; constructing/validating the TIN finishes it.
class StreamProgress {
public:
    StreamProgress(std::istream& input,ProgressCallback callback):input_(input),callback_(std::move(callback)) {
        if(!callback_)return;
        callback_(0);
        const auto state=input_.rdstate();begin_=input_.tellg();
        if(begin_!=std::streampos(-1)) {
            input_.seekg(0,std::ios::end);end_=input_.tellg();
            input_.clear();input_.seekg(begin_);
        }
        input_.clear(state);
    }
    void reading() {
        if(!callback_||++lines_%1024!=0||begin_==std::streampos(-1)||end_<=begin_)return;
        const auto position=input_.tellg();
        if(position!=std::streampos(-1))report(std::clamp(static_cast<int>(85.0*double(position-begin_)/double(end_-begin_)),0,85));
    }
    void report(int percent) {if(callback_&&percent>last_){last_=percent;callback_(percent);}}
private:
    std::istream& input_;ProgressCallback callback_;std::streampos begin_{-1},end_{-1};
    std::size_t lines_{};int last_{};
};
}
