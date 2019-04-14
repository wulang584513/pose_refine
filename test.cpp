#include "pose_refine.h"
#include "cuda_icp/icp.h"

#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;

using namespace cv;
using namespace std;

static std::string prefix = "/home/meiqua/pose_refine/test/";

//#define USE_PROJ

#ifdef CUDA_ON
void test_cuda_icp(){

    {  // gpu need sometime to warm up
        cudaFree(0);
//        cudaSetDevice(0);

        // cublas also need
//        cublasStatus_t stat;  // CUBLAS functions status
        cublasHandle_t cublas_handle;  // CUBLAS context
        /*stat = */cublasCreate(&cublas_handle);
    }

    int width = 640; int height = 480;

    cuda_renderer::Model model(prefix+"obj_06.ply");

    Mat K = (Mat_<float>(3,3) << 572.4114, 0.0, 325.2611, 0.0, 573.57043, 242.04899, 0.0, 0.0, 1.0);
    auto proj = cuda_renderer::compute_proj(K, width, height);

    Mat R_ren = (Mat_<float>(3,3) << 0.34768538, 0.93761126, 0.00000000, 0.70540612,
                 -0.26157897, -0.65877056, -0.61767070, 0.22904489, -0.75234390);
    Mat t_ren = (Mat_<float>(3,1) << 0.0, 0.0, 300.0);
    Mat t_ren2 = (Mat_<float>(3,1) << 20.0, 20.0, 320.0);

    float angle_y = 10.0f/180.0f*3.14f;
    float angle_z = angle_y;
    float angle_x = angle_y;
    Mat rot_mat = helper::eulerAnglesToRotationMatrix({angle_x, angle_y, angle_z,});
    cout << "init angle diff y: " << angle_y*180/3.14f << endl << endl;

    Mat R_ren2 = rot_mat * R_ren;

    cuda_renderer::Model::mat4x4 mat4, mat4_2;
    mat4.init_from_cv(R_ren, t_ren);
    mat4_2.init_from_cv(R_ren2, t_ren2);

    std::vector<cuda_renderer::Model::mat4x4> mat4_v = {mat4, mat4_2};

    helper::Timer timer;

    std::vector<int> depth_cpu = cuda_renderer::render_cpu(model.tris, mat4_v, width, height, proj);
    timer.out("cpu render");

    cv::Mat depth_1 = cv::Mat(height, width, CV_32SC1, depth_cpu.data());
    cv::Mat depth_2 = cv::Mat(height, width, CV_32SC1, depth_cpu.data() + height*width);

    auto bbox1 = helper::get_bbox(depth_1);
    auto bbox2 = helper::get_bbox(depth_2);
    cout << "\nbbox:" << endl;
    cout << "depth 1: " << bbox1 << endl;
    cout << "depth 2: " << bbox2 << endl;
    cout << "init pixel diff xy: "
         << abs(bbox1.x - bbox2.x) << "----" <<  abs(bbox1.y - bbox2.y) << endl << endl;

//    cv::imshow("depth_1", helper::view_dep(depth_1));
//    cv::imshow("depth_2", helper::view_dep(depth_2));
//    cv::waitKey(0);

    Mat3x3f K_((float*)K.data); // ugly but useful
//    cout << K << endl;
//    cout << K_ << endl;
timer.reset();
    std::vector<::Vec3f> pcd1 = cuda_icp::depth2cloud_cpu(depth_cpu.data(), width, height, K_);
//    helper::view_pcd(pcd1);
timer.out("depth2cloud_cpu");
    cv::Mat scene_depth(height, width, CV_32S, depth_cpu.data() + width*height);

timer.reset();
#ifdef USE_PROJ
    Scene_projective scene;
    vector<::Vec3f> pcd_buffer, normal_buffer;
    scene.init_Scene_projective_cpu(scene_depth, K_, pcd_buffer, normal_buffer);
#else
    Scene_nn scene;
    KDTree_cpu kdtree_cpu;
    scene.init_Scene_nn_cpu(scene_depth, K_, kdtree_cpu);
#endif
timer.out("init scene cpu");

#ifdef USE_PROJ
    //view init cloud; the far point is 0 in scene
//    helper::view_pcd(pcd1, pcd_buffer);
#else
//    helper::view_pcd(pcd1, kdtree_cpu.pcd_buffer);
#endif

    {  // open3d
        open3d::PointCloud model_pcd, scene_pcd;
        for(auto& p: pcd1){
            if(p.z > 0)
            model_pcd.points_.emplace_back(float(p.x), float(p.y), float(p.z));
        }
#ifndef USE_PROJ
        for(auto& p: kdtree_cpu.pcd_buffer)
#else
        for(auto& p: pcd_buffer)
#endif
        {
            if(p.z > 0)
            scene_pcd.points_.emplace_back(float(p.x), float(p.y), float(p.z));
        }

        open3d::EstimateNormals(scene_pcd);
        open3d::EstimateNormals(model_pcd);

        timer.reset();
        auto final_result = open3d::RegistrationICP(model_pcd, scene_pcd, 0.1,
                                                    Eigen::Matrix4d::Identity(4, 4),
                                                    open3d::TransformationEstimationPointToPlane());
        timer.out("open3d icp");

        model_pcd.Transform(final_result.transformation_);
        cout << "open3d final rmse: " << final_result.inlier_rmse_ << endl;
        cout << "open3d final fitness: " << final_result.fitness_ << endl;
        cout << "open3d final transformation_:\n" << final_result.transformation_ << endl << endl;
//        helper::view_pcd(model_pcd, scene_pcd);
    }

timer.reset();
    auto result = cuda_icp::ICP_Point2Plane_cpu(pcd1, scene);  // notice, pcd1 are changed due to icp
timer.out("ICP_Point2Plane_cpu");
    Mat result_cv = helper::mat4x4f2cv(result.transformation_);
    Mat R = result_cv(cv::Rect(0, 0, 3, 3));
    auto R_v = helper::rotationMatrixToEulerAngles(R);

    //view icp cloud
#ifdef USE_PROJ
//    helper::view_pcd(pcd_buffer, pcd1);
#else
//    helper::view_pcd(kdtree_cpu.pcd_buffer, pcd1);
#endif

timer.reset();
    auto depth_cuda = cuda_renderer::render_cuda_keep_in_gpu(model.tris, mat4_v, width, height, proj);
timer.out("gpu render");
    // view gpu depth
//    vector<int> depth_host(depth_cuda.size());
//    thrust::copy(depth_cuda.begin_thr(), depth_cuda.end_thr(), depth_host.begin());
//    cv::Mat depth_1_cuda = cv::Mat(height, width, CV_32SC1, depth_host.data());
//    imshow("depth 1 cuda", helper::view_dep(depth_1_cuda));
//    waitKey(0);

timer.reset();
    auto pcd1_cuda = cuda_icp::depth2cloud_cuda(depth_cuda.data(), width, height, K_);
timer.out("depth2cloud_cuda");
// view gpu pcd
//    std::vector<::Vec3f> pcd1_host(pcd1_cuda.size());
//    thrust::copy(pcd1_cuda.begin_thr(), pcd1_cuda.end_thr(), pcd1_host.begin());
//    helper::view_pcd(pcd1_host);

timer.reset();
#ifdef USE_PROJ
    device_vector_holder<::Vec3f> pcd_buffer_cuda, normal_buffer_cuda;
    scene.init_Scene_projective_cuda(scene_depth, K_, pcd_buffer_cuda, normal_buffer_cuda);
#else
    KDTree_cuda kdtree_cuda;
    scene.init_Scene_nn_cuda(scene_depth, K_, kdtree_cuda);
#endif
timer.out("init scene cuda");


timer.reset();
    auto result_cuda = cuda_icp::ICP_Point2Plane_cuda(pcd1_cuda, scene);
timer.out("ICP_Point2Plane_cuda");
    Mat result_cv_cuda = helper::mat4x4f2cv(result_cuda.transformation_);


    cout << "\nresult: " << endl;
    cout << "result fitness: " << result.fitness_ << endl;
    cout << "result mse: " << result.inlier_rmse_ << endl;
    cout << "\nresult_cv:" << endl;
    cout << result_cv << endl;

    cout << "\nresult_cuda: " << endl;
    cout << "result fitness: " << result_cuda.fitness_ << endl;
    cout << "result mse: " << result_cuda.inlier_rmse_ << endl;
    cout << "\nresult_cv_cuda:" << endl;
    cout << result_cv_cuda << endl;

    cout << "\nerror in degree:" << endl;
    cout << "x: " << abs(R_v[0] - angle_x)/3.14f*180  << endl;
    cout << "y: " << abs(R_v[1] - angle_y)/3.14f*180 << endl;
    cout << "z: " << abs(R_v[2] - angle_z)/3.14f*180  << endl;
}
#endif

// hinterstoisser doumanoglou tejani
string dataset_prefix = "/home/meiqua/patch_linemod/public/datasets/hinterstoisser/test/06/";

void depth_edge_test(){
    vector<string> rgb_paths, depth_paths;
    for (const auto & p : fs::directory_iterator(dataset_prefix + "rgb/"))
        rgb_paths.push_back(p.path());
    for (const auto & p : fs::directory_iterator(dataset_prefix + "depth/"))
        depth_paths.push_back(p.path());

    std::sort(rgb_paths.begin(), rgb_paths.end());
    std::sort(depth_paths.begin(), depth_paths.end());

    // from hinter dataset
    Mat modelK = (cv::Mat_<float>(3,3) << 572.4114, 0.0, 325.2611, 0.0, 573.57043, 242.04899, 0.0, 0.0, 1.0);

    for(size_t i=0; i<rgb_paths.size(); i++){
        Mat rgb = imread(rgb_paths[i], CV_LOAD_IMAGE_ANYCOLOR);
        Mat depth = cv::imread(depth_paths[i], CV_LOAD_IMAGE_ANYCOLOR | CV_LOAD_IMAGE_ANYDEPTH);

        Mat depth_edge = PoseRefine::get_depth_edge(depth);
        imshow("depth", helper::view_dep(depth));
        imshow("depth edge", depth_edge);
        waitKey(0);
    }
}

void renderer_interface_test(){
    int width = 640; int height = 480;

    Mat K = (Mat_<float>(3,3) << 572.4114, 0.0, 325.2611, 0.0, 573.57043, 242.04899, 0.0, 0.0, 1.0);
    PoseRefine refiner(prefix + "obj_06.ply");
    refiner.set_K_width_height(K, width, height);

    Mat R_ren = (Mat_<float>(3,3) << 0.34768538, 0.93761126, 0.00000000, 0.70540612,
                 -0.26157897, -0.65877056, -0.61767070, 0.22904489, -0.75234390);
    Mat t_ren = (Mat_<float>(3,1) << 0.0, 0.0, 300.0);

    Mat init = Mat::eye(4, 4, CV_32F);
    R_ren.copyTo(init(Rect(0, 0, 3, 3)));
    t_ren.copyTo(init(Rect(3, 0, 1, 3)));
    cout << "init" << init << endl;

    vector<Mat> poses(1, init);
    auto deps = refiner.render_depth(poses);
    auto masks = refiner.render_mask(poses);
    auto dep_masks = refiner.render_depth_mask(poses);

    imshow("mask1", masks[0]);
    imshow("mask2", dep_masks[0][1]);
    waitKey(0);
}

void process_batch_test(){
    Mat K = (Mat_<float>(3,3) << 572.4114, 0.0, 325.2611,
             0.0, 573.57043, 242.04899, 0.0, 0.0, 1.0);
    PoseRefine refiner("/home/meiqua/patch_linemod/public/datasets/hinterstoisser/models/obj_06.ply");

    cout << refiner.model.tris[0].v0 << endl;

    Mat R_ren = (Mat_<float>(3,3) <<
                  0.04994138,  0.98619508, -0.15787705,
                  0.72859222, -0.14409367, -0.66961957,
                  -0.68312458, -0.08158627, -0.7257303 );
    Mat t_ren = (Mat_<float>(3,1) << 74.90934988, -156.41012715, 1051.41730131);

    Mat right_mask = imread("/home/meiqua/mask.png", 0);

    Mat init = Mat::eye(4, 4, CV_32F);
    R_ren.copyTo(init(Rect(0, 0, 3, 3)));
    t_ren.copyTo(init(Rect(3, 0, 1, 3)));

    vector<string> rgb_paths, depth_paths;
    for (const auto & p : fs::directory_iterator(dataset_prefix + "rgb/"))
        rgb_paths.push_back(p.path());
    for (const auto & p : fs::directory_iterator(dataset_prefix + "depth/"))
        depth_paths.push_back(p.path());

    std::sort(rgb_paths.begin(), rgb_paths.end());
    std::sort(depth_paths.begin(), depth_paths.end());

    // from hinter dataset
    Mat modelK = (cv::Mat_<float>(3,3) << 572.4114, 0.0, 325.2611, 0.0, 573.57043, 242.04899, 0.0, 0.0, 1.0);
    refiner.set_K(modelK);
    for(size_t i=0; i<rgb_paths.size(); i++){
        Mat rgb = imread(rgb_paths[i], CV_LOAD_IMAGE_ANYCOLOR);
        Mat depth = cv::imread(depth_paths[i], CV_LOAD_IMAGE_ANYCOLOR | CV_LOAD_IMAGE_ANYDEPTH);

        refiner.set_depth(depth);
        cout << refiner.proj_mat <<endl;

        vector<Mat> init_poses(50);
        for(auto& pose: init_poses){
            pose = init.clone();
        }

        vector<Mat> test(1, init);
        auto masks = refiner.render_mask(test);
        auto depths = refiner.render_depth(test);

        auto pcd1 = cuda_icp::depth2cloud_cpu((uint16_t*)depth.data, 640, 480, *reinterpret_cast<::Mat3x3f*>(K.data));
        auto pcd2 = cuda_icp::depth2cloud_cpu((uint16_t*)depths[0].data, 640, 480, *reinterpret_cast<::Mat3x3f*>(K.data));
//        helper::view_pcd(pcd1, pcd2);

        double min;
        double max;
        cv::minMaxIdx(depths[0], &min, &max);
        cout << "max: " << max << endl;

        Mat show, show_right;
        rgb.copyTo(show, 255 - masks[0]);
        rgb.copyTo(show_right, 255 - right_mask);

//        for(int i=0; i<50; i++){
//            Mat mask_shift = Mat::zeros(masks[0].rows, masks[0].cols, masks[0].type());
//            right_mask(Rect(0, 0, masks[0].cols, masks[0].rows-i)).copyTo
//                    (mask_shift(Rect(0, i, masks[0].cols, masks[0].rows-i)));

//            Mat test;
//            bitwise_xor(mask_shift, masks[0], test);
//            imshow("test", test);
//            waitKey(0);
//        }


        Mat point = modelK * t_ren;
        int x = int(point.at<float>(0, 0) / point.at<float>(2, 0));
        int y = int(point.at<float>(1, 0) / point.at<float>(2, 0));
        cv::circle(rgb, {x, y}, 3, {0, 0, 255}, -1);

//        cout << rgb_paths[i] << endl;
//        cout << init << endl;
//        imshow("rgb", rgb);
//        imshow("mask", show);
////        imshow("right_mask", show_right);
//        waitKey(0);
        auto pose_extended = refiner.poses_extend(init_poses);
        auto results_unfiltered = refiner.process_batch(pose_extended, 2, true);

        Mat pose_test(4, 4, CV_32F, &results_unfiltered[0].transformation_);
        vector<Mat> pose_test_v(1, pose_test);
        auto depths2 = refiner.render_depth(pose_test_v);
        auto pcd3 = cuda_icp::depth2cloud_cpu((uint16_t*)depths2[0].data, 640, 480, *reinterpret_cast<::Mat3x3f*>(K.data));
//        helper::view_pcd(pcd1, pcd3);

        results_unfiltered.clear();
        auto results = refiner.results_filter(results_unfiltered);

        cout << results.size() << endl;
        cout << "end test" << endl;
        return;
    }
}

int main(int argc, char const *argv[]){

#ifdef CUDA_ON
//    test_cuda_icp();
#endif
    process_batch_test();
    return 0;
}
