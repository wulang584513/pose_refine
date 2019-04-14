#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "np2mat/ndarray_converter.h"
#include "pose_refine.h"
namespace py = pybind11;

PYBIND11_MODULE(pose_refine_pybind, m) {
    NDArrayConverter::init_numpy();

    py::class_<Mat4x4f>(m, "Mat4x4f", py::buffer_protocol())
       .def_buffer([](Mat4x4f &m) -> py::buffer_info {
            return py::buffer_info(
                &m[0][0],                               /* Pointer to buffer */
                sizeof(float),                          /* Size of one scalar */
                py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
                2,                                      /* Number of dimensions */
                { 4, 4 },                 /* Buffer dimensions */
                { sizeof(float) * 4,             /* Strides (in bytes) for each index */
                  sizeof(float) }
            );
        });

    py::class_<cuda_icp::RegistrationResult>(m,"RegistrationResult")
            .def(py::init<>())
            .def_readwrite("fitness_", &cuda_icp::RegistrationResult::fitness_)
            .def_readwrite("inlier_rmse_", &cuda_icp::RegistrationResult::inlier_rmse_)
            .def_readwrite("transformation_", &cuda_icp::RegistrationResult::transformation_);

    py::class_<PoseRefine>(m, "PoseRefine")
            .def(py::init<std::string, cv::Mat, cv::Mat>(), py::arg("model_path"),
                 py::arg("depth") = cv::Mat(), py::arg("K") = cv::Mat())
            .def_readwrite("scene_dep_edge", &PoseRefine::scene_dep_edge)
            .def("view_dep", &PoseRefine::view_dep)
            .def("set_depth", &PoseRefine::set_depth)
            .def("set_K", &PoseRefine::set_K)
            .def("set_K_width_height", &PoseRefine::set_K_width_height)
            .def("render_depth", &PoseRefine::render_depth, py::arg("init_poses"), py::arg("down_sample") = 1)
            .def("render_mask", &PoseRefine::render_mask, py::arg("init_poses"), py::arg("down_sample") = 1)
            .def("render_depth_mask", &PoseRefine::render_depth_mask, py::arg("init_poses"), py::arg("down_sample") = 1)
            .def("process_batch", &PoseRefine::process_batch, py::arg("init_poses"),
                  py::arg("down_sample") = 2, py::arg("depth_aligned") = false)
            .def("poses_extend", &PoseRefine::poses_extend, py::arg("init_poses"),
                  py::arg("degree_var") = CV_PI/10)
            .def("results_filter", &PoseRefine::results_filter, py::arg("results"),
                  py::arg("edge_hit_rate_thresh") = 0.5f, py::arg("fitness_thresh") = 0.7f,
                  py::arg("rmse_thresh") = 0.05f);
}