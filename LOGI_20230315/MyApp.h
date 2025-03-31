#pragma once
#include <map>
#include <deque>
#include <vector>
#include <string>

#include <atomic>
#include <thread>

#include <GLFW/glfw3.h>
#include <opencv2/opencv.hpp>

typedef long long LLONG;

namespace MyApp
{
    float ValueMapping(float value, float leftMin, float leftMax, float rightMin, float rightMax);
    void RenderDockerUI();

    void RenderImage(std::string winName, std::string filename, GLuint& imgTex);
    void RenderImage(std::string winName, std::string filename, cv::Mat& image, GLuint& imgTex);

    void ImgReadProc(std::string filename, cv::Mat& outMat);

    /*
    Timestamp Function Implementation
    */
    int64_t GetTimestamp();
    std::string GetTimestampStr();
    LLONG CvtTimestampStrToLong(std::string str);

    class Timer
    {
    private:
        std::chrono::duration<float, std::milli> interval_;
        std::atomic<bool> activateF_;
        std::atomic<bool> exitF_;
        std::atomic<bool> funcCallableF_;
        std::function<void()> func_;

        std::thread timerTH_;
        std::thread callbackTH_;

    private:
        void _timer()
        {
            while (!this->exitF_)
            {
                try
                {
                    auto st = std::chrono::steady_clock::now();
                    while (!this->exitF_ && this->activateF_ && (std::chrono::steady_clock::now() - st < this->interval_))
                        std::this_thread::yield();
                    if (!this->exitF_ && this->activateF_ && this->funcCallableF_)
                    {
                        if (this->callbackTH_.joinable())
                            this->callbackTH_.join();
                        this->callbackTH_ = std::thread(&Timer::_tick, this);
                    }
                    std::this_thread::yield();
                }
                catch (const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
            if (this->callbackTH_.joinable())
                this->callbackTH_.join();
        }

        void _timer_fixedRate()// Unfinished
        {
            auto st = std::chrono::steady_clock::now();
            int intervalMultiples = 1;
            while (!this->exitF_)
            {
                try
                {
                    while (!this->exitF_ && this->activateF_ && (std::chrono::steady_clock::now() - st < this->interval_ * intervalMultiples))
                        std::this_thread::yield();
                    intervalMultiples++;
                    if (!this->exitF_ && this->activateF_ && this->funcCallableF_)
                    {
                        if (this->callbackTH_.joinable())
                            this->callbackTH_.join();
                        this->callbackTH_ = std::thread(&Timer::_tick, this);
                    }
                    std::this_thread::yield();
                }
                catch (const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
            if (this->callbackTH_.joinable())
                this->callbackTH_.join();
        }

        void _tick()
        {
            this->funcCallableF_ = false;
            this->func_();
            this->funcCallableF_ = true;
        }

    public:
        Timer(int interval_ms, const std::function<void()>& callback) : activateF_(false), exitF_(false), funcCallableF_(true)
        {
            this->interval_ = std::chrono::milliseconds(interval_ms);
            this->func_ = callback;
            this->timerTH_ = std::thread(&Timer::_timer, this);
        }

        ~Timer()
        {
            this->destroy();
        }

        void start() { this->activateF_ = true; }

        void stop() { this->activateF_ = false; }

        void destroy()
        {
            this->activateF_ = false;
            this->exitF_ = true;
            if (this->timerTH_.joinable())
                this->timerTH_.join();
        }
    };
}
