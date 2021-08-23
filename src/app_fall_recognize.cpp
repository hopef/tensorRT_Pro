
#include <stdio.h>
#include <string.h>
#include <opencv2/opencv.hpp>
#include <builder/trt_builder.hpp>
#include <infer/trt_infer.hpp>
#include <common/ilogger.hpp>

#include "yolo/yolo.hpp"
#include "alphapose/alpha_pose.hpp"
#include "fall_gcn/fall_gcn.hpp"

using namespace cv;
using namespace std;

bool requires(const char* name);

static bool compile_models(){

    TRT::set_device(0);
    const char* onnx_files[]{"yolox_m", "sppe", "gcn-new-bp"};
    for(auto& name : onnx_files){
        if(not requires(name))
            return false;

        string onnx_file = iLogger::format("%s.onnx", name);
        string model_file = iLogger::format("%s.fp32.trtmodel", name);
        int test_batch_size = 1;  // 当你需要修改batch大于1时，请查看yolox.cpp:260行备注
        
        // 动态batch和静态batch，如果你想要弄清楚，请打开http://www.zifuture.com:8090/
        // 找到右边的二维码，扫码加好友后进群交流（免费哈，就是技术人员一起沟通）
        if(not iLogger::exists(model_file)){
            bool ok = TRT::compile(
                TRT::TRTMode_FP32,   // 编译方式有，FP32、FP16、INT8
                {},                         // onnx时无效，caffe的输出节点标记
                test_batch_size,            // 指定编译的batch size
                onnx_file,                  // 需要编译的onnx文件
                model_file,                 // 储存的模型文件
                {},                         // 指定需要重定义的输入shape，这里可以对onnx的输入shape进行重定义
                false                       // 是否采用动态batch维度，true采用，false不采用，使用静态固定的batch size
            );

            if(!ok) return false;
        }
    }
    return true;
}

int app_fall_recognize(){
    cv::setNumThreads(0);

    INFO("===================== test alphapose fp32 ==================================");
    if(!compile_models())
        return 0;
    
    auto pose_model_file     = "sppe.fp32.trtmodel";
    auto detector_model_file = "yolox_m.fp32.trtmodel";
    auto gcn_model_file      = "gcn-new-bp.fp32.trtmodel";
    
    auto pose_model     = AlphaPose::create_infer(pose_model_file, 0);
    auto detector_model = Yolo::create_infer(detector_model_file, Yolo::Type::X, 0, 0.4f);
    auto gcn_model      = FallGCN::create_infer(gcn_model_file, 0);

    Mat image;
    VideoCapture cap("fall_video.mp4");
    INFO("%d, %d, %d", 
        (int)cap.get(cv::CAP_PROP_FPS), 
        (int)cap.get(cv::CAP_PROP_FRAME_WIDTH), 
        (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT)
    );

    VideoWriter writer("fall_video.result.avi", cv::VideoWriter::fourcc('X', 'V', 'I', 'D'), 
        30,
        Size(cap.get(cv::CAP_PROP_FRAME_WIDTH), cap.get(cv::CAP_PROP_FRAME_HEIGHT))
    );
    if(!writer.isOpened()){
        INFOE("Writer failed.");
        return 0;
    }

    while(cap.read(image)){
        auto objects = detector_model->commit(image).get();
        for(int i = 0; i < objects.size(); ++i){
            auto& person = objects[i];
            if(person.class_label != 0) continue;

            Rect box(person.left, person.top, person.right-person.left, person.bottom-person.top);
            auto keys   = pose_model->commit(image, box).get();
            auto statev = gcn_model->commit(keys, box).get();

            for(int i = 0; i < keys.size(); ++i){
                float x = keys[i].x;
                float y = keys[i].y;
                cv::circle(image, Point(x, y), 5, Scalar(0, 255, 0), -1, 16);
            }

            FallGCN::FallState state = get<0>(statev);
            float confidence         = get<1>(statev);
            const char* label_name   = FallGCN::state_name(state);
            putText(image, iLogger::format("[%s] %.2f %%", label_name, confidence * 100), Point(person.left, person.top), 0, 1, Scalar(0, 255, 0), 2, 16);
            INFO("Predict is [%s], %.2f %%", label_name, confidence * 100);
        }
        writer.write(image);
    }
    INFO("Done");
    return 0;
}