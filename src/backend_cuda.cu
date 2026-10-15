#include "routemine/core.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <stdexcept>
#include <string>
namespace routemine {
namespace {
void check(cudaError_t code) { if(code!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(code)); }
template<class T> struct Device {
    T* ptr{};
    explicit Device(std::size_t n) {check(cudaMalloc(reinterpret_cast<void**>(&ptr),n*sizeof(T)));}
    ~Device(){cudaFree(ptr);}
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
};
__global__ void kernel(const Vec3* route,double* result,std::size_t n) {
    const auto i=static_cast<std::size_t>(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i<n) { auto a=route[i],b=route[i+1];result[i]=fabs(b.z-a.z)/hypot(b.x-a.x,b.y-a.y)*100.0; }
}
}
std::vector<double> grades_cuda(std::span<const Vec3> route) {
    (void)measure(route);
    Device<Vec3> points(route.size()); Device<double> output(route.size()-1);
    check(cudaMemcpy(points.ptr,route.data(),route.size_bytes(),cudaMemcpyHostToDevice));
    const auto n=route.size()-1;
    // Bound grid and size conversion; practical batch allocations are much smaller.
    if(n>static_cast<std::size_t>(2147483647)*256) throw std::length_error("CUDA batch too large");
    kernel<<<static_cast<unsigned int>((n+255)/256),256>>>(points.ptr,output.ptr,n);
    check(cudaGetLastError()); check(cudaDeviceSynchronize());
    std::vector<double> result(n);
    check(cudaMemcpy(result.data(),output.ptr,n*sizeof(double),cudaMemcpyDeviceToHost));
    return result;
}
}
