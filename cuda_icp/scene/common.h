#pragma once

// common function frequently used by others

#include "../geometry.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/core/core.hpp>

struct Scene_projective; // defined in depth scene

// dep: mm
template <class T>
__device__ __host__ inline
Vec3f dep2pcd(size_t x, size_t y, T dep, Mat3x3f& K, size_t tl_x=0, size_t tl_y=0){
    float z_pcd = dep/1000.0f;
    float x_pcd = (x + tl_x - K[0][2])/K[0][0]*z_pcd;
    float y_pcd = (y + tl_y - K[1][2])/K[1][1]*z_pcd;
    return {
        x_pcd,
        y_pcd,
        z_pcd
    };
}

__device__ __host__ inline
Vec3i pcd2dep(const Vec3f& pcd, const Mat3x3f& K, size_t tl_x=0, size_t tl_y=0){
    int dep = int(pcd.z*1000.0f + 0.5f);
    int x = int(pcd.x/pcd.z*K[0][0] + K[0][2] - tl_x +0.5f);
    int y = int(pcd.y/pcd.z*K[1][1] + K[1][2] - tl_y +0.5f);
    return {
        x,
        y,
        dep
    };
}

template<typename T>
__device__ __host__ inline
T std__abs(T in){return (in > 0)? in: (-in);}

std::vector<Vec3f> get_normal(const cv::Mat& depth, const Mat3x3f& K);