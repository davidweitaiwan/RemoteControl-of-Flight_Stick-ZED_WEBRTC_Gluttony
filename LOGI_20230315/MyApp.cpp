#include "MyApp.h"
#include "imgui.h"

// #define STB_IMAGE_IMPLEMENTATION
// #include "stb_image.h"

#include <opencv2/opencv.hpp>
#include <math.h>

namespace MyApp
{
    std::string cvType2Str(int type)
    {
        std::string r;

        uchar depth = type & CV_MAT_DEPTH_MASK;
        uchar chans = 1 + (type >> CV_CN_SHIFT);

        switch (depth)
        {
            case CV_8U:  r = "8U"; break;
            case CV_8S:  r = "8S"; break;
            case CV_16U: r = "16U"; break;
            case CV_16S: r = "16S"; break;
            case CV_32S: r = "32S"; break;
            case CV_32F: r = "32F"; break;
            case CV_64F: r = "64F"; break;
            default:     r = "User"; break;
        }

        r += "C";
        r += (chans + '0');

        return r;
    }

    // Rewrite Ref: https://stackoverflow.com/questions/1969240/mapping-a-range-of-values-to-another
    float ValueMapping(float value, float leftMin, float leftMax, float rightMin, float rightMax)
    {
        float leftSpan = leftMax - leftMin;
        float rightSpan = rightMax - rightMin;
        float valueScaled = float(value - leftMin) / float(leftSpan);
        return rightMin + (valueScaled * rightSpan);
    }

    void RenderDockerUI()
    {
        static bool opt_fullscreen = true;
        static bool opt_padding = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        else
        {
            dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
        // and handle the pass-thru hole, so we ask Begin() to not render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
        // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
        // all active windows docked into it will lose their parent and become undocked.
        // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
        // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
        if (!opt_padding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace", nullptr, window_flags);
        if (!opt_padding)
            ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        // Submit the DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
        ImGui::End();
    }

    void RenderImage(std::string winName, std::string filename, GLuint& imgTex)
    {
        glBindTexture(GL_TEXTURE_2D, imgTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int image_width = 0;
        int image_height = 0;

        cv::Mat src = cv::imread(filename, cv::IMREAD_UNCHANGED);
        image_width = src.cols;
        image_height = src.rows;

        if (src.type() == CV_32FC1)
        {
            cv::threshold(src, src, 20000.0, 20000.0, cv::THRESH_TRUNC);
            cv::threshold(src, src, 0, 0, cv::THRESH_TOZERO);
            cv::normalize(src, src, 0, 255, cv::NORM_MINMAX, CV_8U);
            cv::cvtColor(src, src, cv::COLOR_GRAY2RGBA);
        }
        else
            cv::cvtColor(src, src, cv::COLOR_BGR2RGBA);
        unsigned char* image_data = src.ptr();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        // unsigned char* image_data = stbi_load(imgPath.c_str(), &image_width, &image_height, NULL, 4);
        // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
        // stbi_image_free(image_data);

        ImGui::Begin(winName.c_str());
        ImVec2 contentSz = ImGui::GetContentRegionAvail();
        ImGui::Text(filename.c_str());
        // ImGui::Text("pointer = %p", imgTex);
        ImGui::Text("size = %d x %d", image_width, image_height);

        double contentWinRatio, imgRatio, showRatio;

        if (image_width <= 0 || image_height <= 0)
            goto IMG_END;

        contentWinRatio = contentSz.y / contentSz.x;
        imgRatio = (double)image_height / image_width;

        showRatio = 1.0;
        if (contentWinRatio > imgRatio)
            showRatio = (double)contentSz.x / image_width;
        else
            showRatio = (double)contentSz.y / image_height;

        ImGui::Image((void*)(intptr_t)imgTex, ImVec2(image_width * showRatio, image_height * showRatio));
IMG_END:
        ImGui::End();
    }

    void RenderImage(std::string winName, std::string filename, cv::Mat& image, GLuint& imgTex)
    {
        glBindTexture(GL_TEXTURE_2D, imgTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int image_width = image.cols;
        int image_height = image.rows;
        unsigned char* image_data = image.ptr();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

        ImGui::Begin(winName.c_str());
        ImGui::Text(filename.c_str());
        // ImGui::Text("pointer = %p", imgTex);
        ImGui::Text("size = %d x %d", image_width, image_height);
        ImVec2 contentSz = ImGui::GetContentRegionAvail();

        double contentWinRatio, imgRatio, showRatio;

        if (image_width <= 0 || image_height <= 0)
            goto IMG_END;

        contentWinRatio = contentSz.y / contentSz.x;
        imgRatio = (double)image_height / image_width;

        showRatio = 1.0;
        if (contentWinRatio > imgRatio)
            showRatio = (double)contentSz.x / image_width;
        else
            showRatio = (double)contentSz.y / image_height;

        ImGui::Image((void*)(intptr_t)imgTex, ImVec2(image_width * showRatio, image_height * showRatio));
IMG_END:
        ImGui::End();
    }

    void ImgReadProc(std::string filename, cv::Mat& outMat)
    {
        outMat = cv::imread(filename, cv::IMREAD_UNCHANGED);

        if (outMat.type() == CV_32FC1)
        {
            cv::threshold(outMat, outMat, 20000.0, 20000.0, cv::THRESH_TRUNC);
            cv::threshold(outMat, outMat, 0, 0, cv::THRESH_TOZERO);
            cv::normalize(outMat, outMat, 0, 255, cv::NORM_MINMAX, CV_8U);
            cv::cvtColor(outMat, outMat, cv::COLOR_GRAY2RGBA);
        }
        else
            cv::cvtColor(outMat, outMat, cv::COLOR_BGR2RGBA);
    }

    /*
    Timestamp Function Implementation
    */
    int64_t GetTimestamp()
    {
        auto t_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        return t_ms.time_since_epoch().count();
    }

    std::string GetTimestampStr()
    {
        std::stringstream ss;
        auto t_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        auto val = t_ms.time_since_epoch();
        ss << std::hex << val.count();
        return ss.str();
    }

    LLONG CvtTimestampStrToLong(std::string str)
    {
        LLONG val = strtoll(str.c_str(), nullptr, 16);
        return val;
    }
}
