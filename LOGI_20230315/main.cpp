// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#define _CRT_SECURE_NO_WARNINGS
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>

#include "IDClient.h"
#include "LogiV4.h"
#include "MyApp.h"
#include <stdio.h>
#include <Windows.h>
#include "GameInput.h"

#include "imgui.h"
//#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers


typedef long long LLONG;
using namespace std::chrono_literals;

#define SERVER_IP "61.220.23.240"
#define SERVER_PORT "10000"

#define DEVICE_NAME "LOGI_WHEEL"
#define CONTROLLER_INDEX 0

#define REMOTE_DEV_NAME "CAR1"

#define CAR_STATUS_DATA_LEN 4

#define SEND_TIMESTAMP 1
#define TIMESTAMP_LOG

struct WheelMsg
{
    int8_t motorPWM = 0;// -100 to 100
    uint8_t motorPark = 0;// 0 or 1
    float steeringAng = 0;// -20 to 20
    float brake_percentage = 0;// 0 to 100
    std::string deviceTs = "";// Millisecond in Hex string
    int64_t recvTs = 0;
};

struct RVStatus
{
    std::vector<WheelMsg> wheelMsgs;
    uint8_t gear = 0;
    uint8_t motionType = 1;
    uint8_t wheelNum = 4;
    int64_t recvTs = 0;
    RVStatus(int numOfWheel = 4)
    {
        wheelNum = numOfWheel;
        wheelMsgs = std::vector<WheelMsg>(numOfWheel);
    }
};

struct WheelComponent
{
    std::pair<std::string, bool> motorID;
    std::pair<std::string, bool> steerID;
    std::pair<std::string, bool> brakeID;
};

class RemoteClient : public IDClient
{
private:
    bool remoteF_;
    std::vector<std::string> remoteDevCandidateName_;// Unuse
    std::string remoteDevName_;
    std::mutex remoteDevNameLock_;

    LLONG transTime_ = 0;
    std::mutex transTimeLock_;

    size_t numOfWheel_;
    std::vector<WheelComponent> wheelCompVec_;
    std::mutex wheelCompLock_;

    RVStatus rvStatus_;
    std::mutex rvStatusLock_;

    bool msgMonitorF_;
    uint32_t monitorInterval_;
    MyApp::Timer* msgMonitorTimer_;

private:
    template <typename T>
    void _safeSave(T* ptr, const T value, std::mutex& lock)
    {
        std::lock_guard<std::mutex> _lock(lock);
        *ptr = value;
    }

    template <typename T>
    T _safeCall(const T* ptr, std::mutex& lock)
    {
        std::lock_guard<std::mutex> _lock(lock);
        return *ptr;
    }

    void _msgMonitorCallback()
    {
        auto msg = this->_safeCall(&this->rvStatus_, this->rvStatusLock_);
        if (MyApp::GetTimestamp() - msg.recvTs > this->monitorInterval_)
        {
            this->setRemoteFlag(false, "");
            this->msgMonitorTimer_->stop();
            this->msgMonitorF_ = false;
            std::cerr << "[RemoteClient::_msgMonitorCallback] Msg timeout. Deregister vehicle." << '\n';
        }
    }

public:
    RemoteClient(IDServerProp& prop, uint32_t monitorInterval_ms) : IDClient(prop),
        remoteF_(false),
        remoteDevName_(""),
        transTime_(0),
        msgMonitorF_(false),
        monitorInterval_(monitorInterval_ms),
        numOfWheel_(0)
    {
        this->msgMonitorTimer_ = new MyApp::Timer(monitorInterval_ms, std::bind(&RemoteClient::_msgMonitorCallback, this));
    }

    void startRemoteConnMonitor()
    {
        if (!this->msgMonitorF_)
        {
            this->msgMonitorTimer_->start();
            this->msgMonitorF_ = true;
        }
    }

    void setRemoteFlag(bool flag, std::string deviceName)
    {
        this->remoteF_ = flag;
        this->_safeSave(&this->remoteDevName_, deviceName, this->remoteDevNameLock_);
        if (!flag)
            this->msgMonitorF_ = false;
    }

    void addRemoteDevCandidate(std::string deviceName)// Unuse
    {
        if (deviceName[0] != '@')
            return;
        std::lock_guard<std::mutex> locker(this->remoteDevNameLock_);
        for (const auto& i : this->remoteDevCandidateName_)
            if (i == deviceName)
                return;
        this->remoteDevCandidateName_.push_back(deviceName);
    }

    std::vector<std::string> getRemoteDevCandidate()// Unuse
    {
        return this->_safeCall(&this->remoteDevCandidateName_, this->remoteDevNameLock_);
    }

    std::string getRemoteDevName() { return this->_safeCall(&this->remoteDevName_, this->remoteDevNameLock_); }

    bool isRemoted() { return this->remoteF_; }

    void setTransTime(LLONG t) { this->_safeSave(&this->transTime_, t, this->transTimeLock_); }

    LLONG getTransTime() { return this->_safeCall(&this->transTime_, this->transTimeLock_); }

    void setRVStatus(RVStatus status) { this->_safeSave(&this->rvStatus_, status, this->rvStatusLock_); }

    RVStatus getRVStatus() { return this->_safeCall(&this->rvStatus_, this->rvStatusLock_); }

    void setWheelComponents(std::vector<WheelComponent> wComps)
    {
        this->_safeSave(&this->wheelCompVec_, wComps, this->wheelCompLock_);
        // this->_safeSave(&this->numOfWheel_, wComps.size(), this->wheelCompLock_);
    }

    std::vector<WheelComponent> getWheelComponents()
    {
        return this->_safeCall(&this->wheelCompVec_, this->wheelCompLock_);
    }

    void setNumOfWheels(size_t num) { this->_safeSave(&this->numOfWheel_, num, this->wheelCompLock_); }

    size_t getNumOfWheels() { return this->_safeCall(&this->numOfWheel_, this->wheelCompLock_); }
};

std::vector<std::string> split(std::string str, std::string delimiter)
{
    std::vector<std::string> splitStrings;
    int encodingStep = 0;
    for (int i = 0; i < str.length(); i++)
    {
        bool isDelimiter = false;
        for (auto& j : delimiter)
            if (str[i] == j)
            {
                isDelimiter = true;
                break;
            }
        if (!isDelimiter)// Is the spliting character
        {
            encodingStep++;
            if (i == str.length() - 1)
                splitStrings.push_back(str.substr(str.length() - encodingStep, encodingStep));
        }
        else// Is delimiter
        {
            if (encodingStep > 0)// Have characters need to split
                splitStrings.push_back(str.substr(i - encodingStep, encodingStep));
            encodingStep = 0;
        }
    }
    return splitStrings;
}

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

static inline ImVec2 ImRotate(const ImVec2& v, float cos_a, float sin_a)
{
    return ImVec2(v.x * cos_a - v.y * sin_a, v.x * sin_a + v.y * cos_a);
}

void ImageRotated(ImTextureID tex_id, ImVec2 center, ImVec2 size, float angle)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float cos_a = cosf(angle * 3.14159 / 180.0);
    float sin_a = sinf(angle * 3.14159 / 180.0);
    ImVec2 pos[4] =
    {
        center + ImRotate(ImVec2(-size.x * 0.5f, -size.y * 0.5f), cos_a, sin_a),
        center + ImRotate(ImVec2(+size.x * 0.5f, -size.y * 0.5f), cos_a, sin_a),
        center + ImRotate(ImVec2(+size.x * 0.5f, +size.y * 0.5f), cos_a, sin_a),
        center + ImRotate(ImVec2(-size.x * 0.5f, +size.y * 0.5f), cos_a, sin_a)
    };
    ImVec2 uvs[4] =
    {
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImVec2(0.0f, 1.0f)
    };

    draw_list->AddImageQuad(tex_id, pos[0], pos[1], pos[2], pos[3], uvs[0], uvs[1], uvs[2], uvs[3], IM_COL32_WHITE);
}


std::string getTimestampStr()
{
    std::stringstream ss;
    auto t_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    auto val = t_ms.time_since_epoch();
    ss << std::hex << val.count();
    return ss.str();
}

LLONG cvtTimestampStrToLong(std::string str)
{
    LLONG val = strtoll(str.c_str(), nullptr, 16);
    return val;
}

void RecvMsgEventHandler(IDClient* idc, std::string fromDevice, std::string recvMsg)
{
    RemoteClient* ridc = static_cast<RemoteClient*>(idc);
    if (recvMsg == "ControlRegister" && !ridc->isRemoted())// Set remote device name
    {
        printf("Recv ControlRegister from %s\n", fromDevice.c_str());
        auto vhcCandidate = ridc->getRemoteDevCandidate();
        for (auto& i : vhcCandidate)
            if (i == fromDevice)
            {
                try
                {
                    std::string wNumStr = fromDevice.substr(1, fromDevice.find('!') - 1);
                    ridc->setNumOfWheels(std::stoi(wNumStr));
                    ridc->setRemoteFlag(true, fromDevice);
                    return;
                }
                catch (...)
                {
                    std::cerr << "[RecvRemoteMsgEventHandler][ControlRegister][" << fromDevice << " " << recvMsg << "] Caught unknown exception" << '\n';
                }
            }
    }
    else if (ridc->isRemoted() && recvMsg != "ControlRegister")
    {
        try
        {
            if (recvMsg[0] == '#')
            {
                std::string ts_now = getTimestampStr();
                std::string recvTimeStr = recvMsg.substr(1, recvMsg.find('!') - 1);
                LLONG tSend = cvtTimestampStrToLong(recvTimeStr);
                LLONG tRecv = cvtTimestampStrToLong(ts_now);
                ridc->setTransTime(tRecv - tSend);// ms

                std::string rvStatusStr = recvMsg.substr(recvMsg.find('!') + 1);
                // printf("Recv [%s]: %s\n", fromDevice.c_str(), rvStatusStr.c_str());
                auto rvStatusStrSplit = split(rvStatusStr, ":");
                if (rvStatusStrSplit.size() >= 10)
                {
                    auto n = ridc->getNumOfWheels();
                    if (n <= 0)
                        return;

                    RVStatus rvs(n);
                    rvs.wheelMsgs[0].motorPWM = std::stof(rvStatusStrSplit[0]);
                    rvs.wheelMsgs[0].steeringAng = std::stof(rvStatusStrSplit[1]);
                    rvs.wheelMsgs[1].motorPWM = std::stof(rvStatusStrSplit[2]);
                    rvs.wheelMsgs[1].steeringAng = std::stof(rvStatusStrSplit[3]);
                    rvs.wheelMsgs[2].motorPWM = std::stof(rvStatusStrSplit[4]);
                    rvs.wheelMsgs[2].steeringAng = std::stof(rvStatusStrSplit[5]);
                    rvs.wheelMsgs[3].motorPWM = std::stof(rvStatusStrSplit[6]);
                    rvs.wheelMsgs[3].steeringAng = std::stof(rvStatusStrSplit[7]);
                    rvs.gear = std::stoi(rvStatusStrSplit[8]);
                    rvs.motionType = std::stoi(rvStatusStrSplit[9]);
                    rvs.recvTs = tRecv;
                    ridc->setRVStatus(rvs);
                    ridc->startRemoteConnMonitor();
                }
            }
            else if (recvMsg[0] == '@')
            {
                // printf("[%s][%s]\n", fromDevice.c_str(), recvMsg.c_str());
                std::string funcType = recvMsg.substr(1, recvMsg.find('!') - 1);
                if (funcType == "COMP")// Read composition
                {
                    // @COMP!$@M!@M!M0_0:@M!M1_0:@M!M2_0:@M!M3_0$@S!@S!S0_0:@S!S1_0:@S!S2_0:@S!S3_0$@B!@B!B0_0:@B!B1_0:@B!B2_0:@B!B3_0
                    auto sectionVec = split(recvMsg.substr(recvMsg.find('!') + 2), "$");
                    std::vector<WheelComponent> wCompVec(ridc->getNumOfWheels());
                    for (auto& s : sectionVec)
                    {
                        if (s[0] != '@')// Component descriptor
                            continue;
                        std::string compType = s.substr(1, s.find('!') - 1);// M, S or B
                        std::string compStr = s.substr(s.find('!') + 1);
                        auto compVec = split(compStr, ":");
                        if (compVec.size() != wCompVec.size())
                            continue;
                        printf("[%s][%s]\n", compType.c_str(), compStr.c_str());
                        for (int i = 0; i < compVec.size(); i++)
                        {
                            auto id = compVec[i].substr(0, compVec[i].find('_'));
                            auto act = std::stoi(compVec[i].substr(compVec[i].find('_') + 1));
                            if (compType == "M")// Motor
                                wCompVec[i].motorID = { id, act };
                            else if (compType == "S")// Steering motor
                                wCompVec[i].steerID = { id, act };
                            else if (compType == "B")// Brake motor
                                wCompVec[i].brakeID = { id, act };
                        }
                    }
                    ridc->setWheelComponents(wCompVec);
                }
            }
        }
        catch (...)
        {
            std::cerr << "!!!RecvMsgEventHandler[Proc] Exception!!!\n\t" << recvMsg << std::endl;
        }
    }
}

//std::string CreateSendStr(WheelState& ws, SimpleWheelState& sws)
//{
//    char sendBuf[128];
//    sprintf(sendBuf, "%s:%d:%d:%d:%d:%d", ws.getGearString(), sws.getWheelState(), sws.getAccelerator(), sws.getBrake(), sws.getClutch(), ws.getMotionType());
//    if (SEND_TIMESTAMP)
//        return (std::string)sendBuf + ":" + getTimestampStr();
//    return (std::string)sendBuf;
//}

std::string CreateSendStr(std::string& Gear, int& WheelState, int& Accelerator, int& Brake, int& Clutch, int& MotionType)
{
    char sendBuf[128];
    if (Gear == "Drive")
    {
        sprintf(sendBuf, "%s:%d:%d:%d:%d:%d", "Drive", WheelState, Accelerator, Brake, Clutch, MotionType);
    }
    if (Gear == "Neutral")
    {
        sprintf(sendBuf, "%s:%d:%d:%d:%d:%d", "Neutral", WheelState, Accelerator, Brake, Clutch, MotionType);
    }
    if (Gear == "Reverse")
    {
        sprintf(sendBuf, "%s:%d:%d:%d:%d:%d", "Reverse", WheelState, Accelerator, Brake, Clutch, MotionType);
    }
    if (Gear == "Park")
    {
        sprintf(sendBuf, "%s:%d:%d:%d:%d:%d", "Park", WheelState, Accelerator, Brake, Clutch, MotionType);
    }
    //sprintf(sendBuf, "%s:%d:%d:%d:%d:%d", Gear, WheelState, Accelerator, Brake, Clutch, MotionType);
    if (SEND_TIMESTAMP)
        return (std::string)sendBuf + ":" + getTimestampStr();
    return (std::string)sendBuf;
}

int ConnectToIDServer(RemoteClient& client, std::string deviceID)
{
    int errCnt = 1;
    while (!client.isServerConn())
    {
        try
        {
            client.connToServer();
            std::cerr << "Connected to ID server: " << client.getIDServerProp().host << ":" << client.getIDServerProp().port << std::endl;
            std::this_thread::sleep_for(100ms);
        }
        catch (const IDClientException& e)
        {
            std::cerr << "Caught IDClientException while connecting to ID server: " << e << ". Retry in 0.5s" << std::endl;
            std::this_thread::sleep_for(500ms);
        }
        catch (...)
        {
            std::cerr << "Caught unknown exception while connecting to ID server." << std::endl;
            return -1;
        }
        if (errCnt-- < 1)
        {
            std::cerr << "Connection failed." << std::endl;
            return -1;
        }
    }
    try
    {
        client.regToServer(deviceID);
        std::cerr << "Registered to ID server: " << deviceID << std::endl;
        std::this_thread::sleep_for(500ms);
        client.requestIDTableFromServer();
        std::this_thread::sleep_for(100ms);
    }
    catch (const IDClientException& e)
    {
        std::cerr << "Caught IDClientException while register to ID server: " << e << std::endl;
        return -1;
    }
    catch (...)
    {
        std::cerr << "Caught unknown exception while register to ID server." << std::endl;
        return -1;
    }
    return 0;
}

int VehicleRegistration(RemoteClient& client, std::string vehicleID)
{
    if (!client.isServerConn() || !client.isServerReg() || client.isRemoted())
        return -1;
    try// Register remote device
    {
        client.requestIDTableFromServer();
        std::this_thread::sleep_for(500ms);
        client.sendMsgToClient(vehicleID, "RemoteRegister");// Send apply signal.
        client.addRemoteDevCandidate(vehicleID);
    }
    catch (const IDClientException& e)
    {
        std::cerr << "[VehicleRegistration] Caught IDClientException: " << e << std::endl;
    }
    catch (...)
    {
        std::cerr << "[VehicleRegistration] Caught Unknow Exception" << std::endl;
    }
}


void SendControllerMsgTh(RemoteClient& ridc, std::string& Gear, int& WheelState, int& Accelerator, int& Brake, int& Clutch, int& MotionType, std::atomic<uint64_t>& interval_ms, bool& stopF)
{

#ifdef TIMESTAMP_LOG
    std::chrono::steady_clock execClock;
    auto execSt = execClock.now();
    double execTime = 0;
    FILE* fd_tlog = fopen("timestamp_log.txt", "w");
#endif

    while (!stopF)
    {
        auto startT = std::chrono::steady_clock::now();
        while (!ridc.isServerConn() && !stopF)
            std::this_thread::sleep_for(500ms);
        while (!ridc.isRemoted() && !stopF)
            std::this_thread::sleep_for(500ms);

        try
        {

            auto sendStr = CreateSendStr(Gear, WheelState, Accelerator, Brake, Clutch, MotionType);
            if (ridc.isServerConn() && ridc.isRemoted() && !stopF)
            {
                ridc.sendMsgToClient(ridc.getRemoteDevName(), sendStr);
                // std::cerr << "[SendControllerMsgTh] Send Msg" << std::endl;
            }

#ifdef TIMESTAMP_LOG
            auto execNow = execClock.now();
            auto tSpan = static_cast<std::chrono::duration<double>>(execNow - execSt);
            execTime = tSpan.count();
            fprintf(fd_tlog, "%.2f,%s,%d,%s\n", execTime, getTimestampStr().c_str(), ridc.getTransTime(), sendStr.c_str());
#endif
        }
        catch (const IDClientException& e)
        {
            std::cerr << "[SendControllerMsgTh] Caught exception: " << e << std::endl;
        }
        uint64_t dur = (uint64_t)((std::chrono::steady_clock::now() - startT).count() / 1000000.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(dur > interval_ms ? 0 : interval_ms - dur));
    }

#ifdef TIMESTAMP_LOG
    fclose(fd_tlog);
#endif

}

void UpdateIDTableTh(RemoteClient& ridc, uint64_t interval_ms, bool& stopF)
{
    while (!stopF)
    {
        auto startT = std::chrono::steady_clock::now();
        while (!ridc.isServerConn() && !stopF)
            std::this_thread::sleep_for(500ms);
        while (!ridc.isServerReg() && !stopF)
            std::this_thread::sleep_for(500ms);

        try
        {
            if (ridc.isServerConn() && ridc.isServerReg() && !stopF)
                ridc.requestIDTableFromServer();
        }
        catch (const IDClientException& e)
        {
            std::cerr << "[UpdateIDTableTh] Caught IDClientException: " << e << std::endl;
        }

        uint64_t dur = (uint64_t)((std::chrono::steady_clock::now() - startT).count() / 1000000.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(dur > interval_ms ? 1 : interval_ms - dur));
    }
}


void UpdateSteeringWheelStateTh(SimpleWheelState& sws, bool& stopF)
{
    while (!stopF)
    {
        sws.Update();
        std::this_thread::sleep_for(10ms);
    }
}


// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif


static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
struct Joysticks
{
    uint32_t deviceCount;
    IGameInputDevice** devices;
};

void CALLBACK deviceChangeCallback(GameInputCallbackToken callbackToken, void* context, IGameInputDevice* device, uint64_t timestamp, GameInputDeviceStatus currentStatus, GameInputDeviceStatus previousStatus)
{
    Joysticks* joysticks = (Joysticks*)context;
    if (currentStatus & GameInputDeviceConnected)
    {
        for (uint32_t i = 0; i < joysticks->deviceCount; ++i)
        {
            if (joysticks->devices[i] == device) return;
        }
        ++joysticks->deviceCount;
        joysticks->devices = (IGameInputDevice**)realloc(joysticks->devices, joysticks->deviceCount * sizeof(IGameInputDevice*));
        joysticks->devices[joysticks->deviceCount - 1] = device;
        uint16_t ProductId = joysticks->devices[joysticks->deviceCount - 1]->GetDeviceInfo()->productId;

        
    }
}

// Main code
int main(int argc, char** argv)
{

    Joysticks joysticks = { 0 };
    IGameInput* input;
    GameInputCreate(&input);
    IGameInputDispatcher* dispatcher;
    input->CreateDispatcher(&dispatcher);
    GameInputCallbackToken callbackId;
    input->RegisterDeviceCallback(0, GameInputKindController, GameInputDeviceAnyStatus, GameInputBlockingEnumeration, &joysticks, deviceChangeCallback, &callbackId);

    bool buttons[64];
    int steeringButton = -1;

    GameInputSwitchPosition switches[64];
    float axes[64];
    float previousValue = 0;
    dispatcher->Dispatch(0);


    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;


    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Remote Panel", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);


    std::map<std::string, GLuint> imgTexMap;
    imgTexMap["steeringwheel"] = GLuint();
    glGenTextures(1, &imgTexMap["steeringwheel"]);
    cv::Mat steeringImgSrc = cv::imread("./img/steering-wheel.png", cv::IMREAD_UNCHANGED);
    int paddingSz = (steeringImgSrc.rows / 2) * 0.1;
    cv::copyMakeBorder(steeringImgSrc, steeringImgSrc, paddingSz, paddingSz, paddingSz, paddingSz, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::cvtColor(steeringImgSrc, steeringImgSrc, cv::COLOR_BGRA2RGBA);

    bool stopF = false;
    bool showF = false;
    bool sockF = false;

    std::string Gear = "";
    int WheelState = 0;
    int Accelerator = 0;
    int Brake = 0;
    int Clutch = 0;
    int MotionType = 0;
    // LOGI wheel
    //WheelState logiWheel(0);
    //SimpleWheelState wh(&logiWheel);
    //std::thread logiTH(main_logi, std::ref(stopF), std::ref(showF), std::ref(logiWheel));
    //std::thread whTH(UpdateSteeringWheelStateTh, std::ref(wh), std::ref(stopF));

    // IDClient
    IDServerProp idcProp(SERVER_IP, SERVER_PORT, PACKET_HEADER_SIZE, PACKET_PAYLOAD_SIZE);
    RemoteClient idclient(idcProp, 2000);
    idclient.setRecvMsgEventHandler(RecvMsgEventHandler, true);

    // Sending parameters
    std::atomic<uint64_t> sendInterval_ms = 10;




    std::thread sendTh(SendControllerMsgTh, std::ref(idclient), std::ref(Gear), std::ref(WheelState), std::ref(Accelerator), std::ref(Brake), std::ref(Clutch), std::ref(MotionType), std::ref(sendInterval_ms), std::ref(stopF));
    std::thread updateIDTableTh(UpdateIDTableTh, std::ref(idclient), 3000, std::ref(stopF));

    //ConnectToIDServer(idclient, DEVICE_NAME);


    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = NULL;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!glfwWindowShouldClose(window))
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int motionType = 0;
        float axes_x = -1;
        float axes_thr = -1;
        float axes_brk = -1;
        int gearButton = 0;
        // My code goes here
        MyApp::RenderDockerUI();
        for (uint32_t i = 0; i < joysticks.deviceCount; ++i)
        {
            IGameInputReading* reading;
            if (SUCCEEDED(input->GetCurrentReading(GameInputKindController, joysticks.devices[i], &reading)))
            {
                ImGui::Text("Joystick = %d", i);
                reading->GetControllerAxisState(ARRAYSIZE(axes), axes);
                reading->GetControllerSwitchState(ARRAYSIZE(switches), switches);
                reading->GetControllerButtonState(ARRAYSIZE(buttons), buttons);

                for (uint32_t j = 0; j < reading->GetControllerAxisCount(); ++j) {
                    ImGui::Text("axes = %d ,vlaue = %f", j, axes[j]);
                    if (reading->GetControllerAxisCount() == 2 && j == 0) {
                        uint16_t test = joysticks.devices[i]->GetDeviceInfo()->productId; //1026
                        axes_x = axes[j];
                    }
                    if (reading->GetControllerAxisCount() == 5 && j == 2) {
                        uint16_t test = joysticks.devices[i]->GetDeviceInfo()->productId; //1028

                        axes_thr = axes[j];
                    }
                    if (reading->GetControllerAxisCount() == 5 && j == 3) {
                        axes_brk = axes[j];
                    }

                }

                //for (uint32_t i = 0; i < reading->GetControllerSwitchCount(); ++i) {
                //    printf("%d:%d ", i, switches[i]);
                //}

                for (uint32_t z = 0; z < reading->GetControllerButtonCount(); ++z) {
                    if (buttons[z]) {
                        if(reading->GetControllerAxisCount() == 2)
                        if (z == 14 || z == 15 || z == 16 || z == 17)
                            steeringButton = z;
                        if(reading->GetControllerAxisCount() == 5)
                        if (i == 0 && (z == 26 || z == 27 || z == 24))
                            gearButton = z;
                    }
                }
                puts("");
                reading->Release();
            }
        }
        ImFont* mainFont = ImGui::GetFont();
        mainFont->Scale = 2;
        {
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("ID_server"))
                {
                    if (idclient.isServerConn())
                        ImGui::BeginDisabled(true);
                    static char hostStr[128] = SERVER_IP;
                    ImGui::InputTextWithHint("Server host", "IP or host name", hostStr, IM_ARRAYSIZE(hostStr));
                    static char portStr[6] = SERVER_PORT;
                    ImGui::InputTextWithHint("Port", "0~65535", portStr, IM_ARRAYSIZE(portStr));
                    static char devName[128] = DEVICE_NAME;
                    ImGui::InputTextWithHint("Device ID", "Name register to server", devName, IM_ARRAYSIZE(devName));

                    if (ImGui::Button("Connect")) {
                        ConnectToIDServer(idclient, devName);
                        ImGui::BeginDisabled(true);
                    }


                    if (idclient.isServerConn())
                        ImGui::EndDisabled();
                    else
                    {
                        idclient.getIDServerProp().host = hostStr;
                        idclient.getIDServerProp().port = portStr;
                    }

                    if (!idclient.isServerConn())
                        ImGui::BeginDisabled(true);

                    ImGui::SameLine();
                    if (ImGui::Button("Disconnect"))
                    {
                        try
                        {
                            idclient.setRemoteFlag(false, "");
                            idclient.close();
                            ImGui::BeginDisabled(true);

                            std::cout << "Disconnected from ID server." << std::endl;
                        }
                        catch (...)
                        {
                            std::cout << "Exception caught while disconnecting from ID server." << std::endl;
                        }
                    }
                    if (!idclient.isServerConn())
                        ImGui::EndDisabled();

                    ImGui::EndMenu();
                }

                if (!idclient.isServerConn())
                    ImGui::BeginDisabled(true);
                if (ImGui::BeginMenu("Remote"))
                {
                    if (idclient.isRemoted())
                        ImGui::BeginDisabled(true);

                    auto vhcCandidates = idclient.getIDTable();
                    static int vhcCandidateIdx = -1;
                    static std::string vhcStr = "";

                    if (ImGui::BeginCombo("Vehicle ID", vhcStr.c_str()))
                    {
                        for (int n = 0; n < vhcCandidates.size(); n++)
                        {
                            const bool is_selected = (vhcCandidateIdx == n);
                            if (ImGui::Selectable(vhcCandidates[n].c_str(), is_selected))
                                vhcCandidateIdx = n;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (vhcCandidateIdx >= 0 && vhcCandidateIdx < vhcCandidates.size())
                        vhcStr = vhcCandidates[vhcCandidateIdx];

                    if (ImGui::Button("Register") && vhcStr.length() > 0)
                        VehicleRegistration(idclient, vhcStr);

                    if (idclient.isRemoted())
                        ImGui::EndDisabled();

                    if (!idclient.isRemoted())
                        ImGui::BeginDisabled(true);

                    ImGui::SameLine();
                    if (ImGui::Button("Deregister") && vhcStr.length() > 0) {
                        idclient.setRemoteFlag(false, "");
                        ImGui::BeginDisabled(true);
                    }


                    if (!idclient.isRemoted())
                        ImGui::EndDisabled();

                    static char sendIntervalStr[10] = "10";
                    ImGui::InputTextWithHint("Send interval (ms)", "Send intervals", sendIntervalStr, IM_ARRAYSIZE(sendIntervalStr));
                    ImGui::SameLine();
                    if (ImGui::Button("Set"))
                    {
                        try
                        {
                            sendInterval_ms = std::stoull(sendIntervalStr);
                        }
                        catch (...)
                        {
                            std::cout << "Exception caught while setting send interval." << std::endl;
                        }
                    }

                    ImGui::EndMenu();
                }
                if (!idclient.isServerConn())
                    ImGui::EndDisabled();

                if (!idclient.isServerConn() || !idclient.isRemoted())
                    ImGui::BeginDisabled(true);
                if (ImGui::BeginMenu("Vehicle"))
                {
                    ImGui::SeparatorText("Vehicle Information");
                    ImGui::Text(idclient.getRemoteDevName().c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("Scan"))
                    {
                        idclient.sendMsgToClient(idclient.getRemoteDevName(), "RequestComposition");
                    }
                    if (idclient.isRemoted())
                    {
                        ImGui::SeparatorText("Vehicle Composition");
                        auto wCompVec = idclient.getWheelComponents();
                        static auto colorInactive = ImVec4(1, 0, 0, 1);
                        static auto colorActive = ImVec4(0, 1, 0, 1);

                        if (ImGui::BeginTable("compTable", 4))
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Index");
                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Motor");
                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Steering");
                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Braking");
                            for (int i = 0; i < wCompVec.size(); i++)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::Text(std::to_string(i).c_str());
                                ImGui::TableNextColumn();
                                auto id = wCompVec[i].motorID;
                                ImGui::TextColored(id.second ? colorActive : colorInactive, id.first.c_str());
                                ImGui::TableNextColumn();
                                id = wCompVec[i].steerID;
                                ImGui::TextColored(id.second ? colorActive : colorInactive, id.first.c_str());
                                ImGui::TableNextColumn();
                                id = wCompVec[i].brakeID;
                                ImGui::TextColored(id.second ? colorActive : colorInactive, id.first.c_str());
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (!idclient.isServerConn() || !idclient.isRemoted())
                    ImGui::EndDisabled();

                ImGui::EndMainMenuBar();
            }
        }

        {
            ImGui::Begin("Steering_Wheel");
            ImVec2 contentSz = ImGui::GetContentRegionAvail();

            auto steeringwheel = int(((32767 * 2) * (axes_x)) - 32767);
            WheelState = steeringwheel;
            /*  auto steeringwheel = wh.getWheelState();*/
            ImGui::PushItemWidth(contentSz.x);
            ImGui::SliderInt("##something", &steeringwheel, -32768, 32767, "%d");
            ImGui::PopItemWidth();

            ImVec2 p = ImGui::GetCursorScreenPos();
            contentSz = ImGui::GetContentRegionAvail();

            cv::Mat steeringImgClone = steeringImgSrc.clone();
            unsigned char* image_data = steeringImgClone.ptr();
            glBindTexture(GL_TEXTURE_2D, imgTexMap["steeringwheel"]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, steeringImgSrc.cols, steeringImgSrc.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
            float imgAngle = MyApp::ValueMapping(steeringwheel, -32768, 32767, -450, 450);
            ImageRotated((void*)(intptr_t)imgTexMap["steeringwheel"], p + ImVec2(contentSz.y / 2.0, contentSz.y / 2.0), ImVec2(contentSz.y, contentSz.y), imgAngle);
            ImGui::End();
        }

        {
            ImGui::Begin("Transmission");

            float gearColorHue[] = { 0.6f, 0.0f, 0.1f, 0.32f };
            const char* gearText[] = { "P", "R", "N", "D" };
            int gear = 2;

            if (int(4 * axes_brk) == 4 && int(4 * axes_thr) == 4)
            {
                gear = 2;
                Gear = "Neutral";

            }
            if (gearButton == 26) {
                gear = 1;
                Gear = "Reverse";

            }
            if (gearButton == 27)
            {
                gear = 3;
                Gear = "Drive";

            }
            if (gearButton == 24)
            {
                gear = 0;
                Gear = "Park";

            }

            ImFont font = *ImGui::GetFont();
            font.Scale = 2;
            ImGui::BeginGroup();
            ImGui::SeparatorText("Gear");
            ImVec2 contentSz = ImGui::GetContentRegionAvail();
            for (int i = 0; i < 4; i++)
            {
                ImVec4 color = ImColor::HSV(gearColorHue[i], 0.8f, i == (int)gear ? 0.8f : 0.2f);
                /* ImVec4 color = ImColor::HSV(gearColorHue[i], 0.8f, i == (int)logiWheel.getGear() ? 0.8f : 0.2f);*/
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
                ImGui::PushFont(&font);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
                ImGui::Button(gearText[i], ImVec2(contentSz.y / 4.2, contentSz.y / 4.2));
                ImGui::PopStyleVar();
                ImGui::PopFont();
                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
            ImGui::EndGroup();

            ImGui::SameLine(0, contentSz.y / 10);

            float motionColorHue[] = { 0.32f, 0.6f, 0.0f, 0.1f };
            const char* motionButtonText[] = { "1", "2", "3", "4" };
            const char* motionText[] = { "Ackermann", "4WS", "Zero Turn", "Parallel" };
            ImGui::BeginGroup();
            switch (int(steeringButton))
            {
            case 14:
                motionType = 0;
                break;
            case 15:
                motionType = 1;
                break;
            case 16:
                motionType = 2;
                break;
            case 17:
                motionType = 3;
                break;
            default:
                break;
            }

            ImGui::SeparatorText("Steering");
            for (int i = 0; i < 4; i++)
            {
                ImVec4 color = ImColor::HSV(motionColorHue[i], 0.8f, i == (int)motionType ? 0.8f : 0.2f);
                MotionType = motionType;
                //ImVec4 color = ImColor::HSV(motionColorHue[i], 0.8f, i == (int)logiWheel.getMotionType() - 1 ? 0.8f : 0.2f);

                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
                ImGui::Button(motionButtonText[i], ImVec2(contentSz.y / 10, contentSz.y / 10));
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::Text(motionText[i]);
            }

            ImGui::SeparatorText("Pedal");
            contentSz = ImGui::GetContentRegionAvail();
            Brake = 255 - (255 * axes_brk);
            Accelerator = 255 - (255 * axes_thr);
            Clutch = 0;
            float pedalStateArr[] = { 0, 255 - (255 * axes_brk), 255 - (255 * axes_thr) };
            const char* pedalText[] = { "clu:\n%.2f", "brk:\n%.2f", "thr:\n%.2f" };
            for (int i = 0; i < 3; i++)
            {
                if (i > 0)
                    ImGui::SameLine();
                ImGui::PushID(i);
                ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, contentSz.y / 10);
                ImGui::VSliderFloat("##v", ImVec2(contentSz.x / 3.1, contentSz.y), &pedalStateArr[i], 0.0f, 255.0f, pedalText[i]);
                ImGui::PopStyleVar();
                ImGui::PopID();
            }
            ImGui::EndGroup();
            ImGui::End();
        }
        {
            ImGui::Begin("Vehicle State");
            if (idclient.isRemoted())
            {
                static char transTimeStr[128];
                sprintf(transTimeStr, "Transmission time: %lld ms", idclient.getTransTime());
                ImGui::Text(transTimeStr);

                auto rvState = idclient.getRVStatus();
                if (ImGui::BeginTable("rvTable", 5))
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Index");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Motor PWM");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Steering");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Braking");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Parking");
                    for (int i = 0; i < rvState.wheelNum; i++)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text(std::to_string(i).c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(std::to_string(rvState.wheelMsgs[i].motorPWM).c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(std::to_string(rvState.wheelMsgs[i].steeringAng).c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(std::to_string(rvState.wheelMsgs[i].brake_percentage).c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(std::to_string(rvState.wheelMsgs[i].motorPark).c_str());
                    }
                    ImGui::EndTable();
                }
                static char buf[128];
                sprintf(buf, "Gear Type: %d", rvState.gear);
                ImGui::Text(buf);
                sprintf(buf, "Steering Type: %d", rvState.motionType);
                ImGui::Text(buf);
            }

            ImGui::End();
        }


        ImGui::Begin("State");
        float logiStateButtonColorHue[] = { 0.0f, 0.1f, 0.32f };
        const char* logiStateText[] = { "LOGI GHUB ERROR", "LOGI Not Init",  "LOGI OK" };
        static int logiStateIdx = 0;


        logiStateIdx = 1;

        ImVec4 logiStateButtonColor = ImColor::HSV(logiStateButtonColorHue[logiStateIdx], 0.8f, 0.8f);
        ImGui::PushStyleColor(ImGuiCol_Button, logiStateButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, logiStateButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, logiStateButtonColor);
        ImGui::SmallButton("L");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(logiStateText[logiStateIdx]);
        ImGui::PopStyleColor(3);
        ImGui::SameLine();

        float IDClientStateButtonColorHue[] = { 0.0f, 0.1f, 0.32f };
        const char* IDClientStateText[] = { "Server Not Connect", "ID Not Registered",  "ID Server OK" };
        static int IDClientStateIdx = 0;

        if (!idclient.isServerConn())
            IDClientStateIdx = 0;
        else if (!idclient.isServerReg())
            IDClientStateIdx = 1;
        else
            IDClientStateIdx = 2;
        ImVec4 IDClientStateButtonColor = ImColor::HSV(IDClientStateButtonColorHue[IDClientStateIdx], 0.8f, 0.8f);
        ImGui::PushStyleColor(ImGuiCol_Button, IDClientStateButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IDClientStateButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IDClientStateButtonColor);
        ImGui::SmallButton("I");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(IDClientStateText[IDClientStateIdx]);
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        float vehicleStateButtonColorHue[] = { 0.0f, 0.1f, 0.32f };
        const char* vehicleStateText[] = { "Vehicle Not Remote", "Preserve",  "Vehicle OK" };
        static int vehicleStateIdx = 0;

        if (!idclient.isRemoted())
            vehicleStateIdx = 0;
        else
            vehicleStateIdx = 2;
        ImVec4 vehicleStateButtonColor = ImColor::HSV(vehicleStateButtonColorHue[vehicleStateIdx], 0.8f, 0.8f);
        ImGui::PushStyleColor(ImGuiCol_Button, vehicleStateButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vehicleStateButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, vehicleStateButtonColor);
        ImGui::SmallButton("V");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(vehicleStateText[vehicleStateIdx]);
        ImGui::PopStyleColor(3);

        ImGui::End();

        // ImGui::ShowDemoWindow();



        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Close LOGI wheel and IDClient
    stopF = true;
    sendTh.join();
    updateIDTableTh.join();
    //whTH.join();
    //logiTH.join();
    try
    {
        idclient.close();
    }
    catch (const IDClientException& e)
    {
        std::cerr << "[main] Caught IDClientException: " << e << std::endl;
    }
    catch (...)
    {
        std::cerr << "[main] Caught Unknown Exception: " << std::endl;
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    // system("pause");
    return 0;
}
