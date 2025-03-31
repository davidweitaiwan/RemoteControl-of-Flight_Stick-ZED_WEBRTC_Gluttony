#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <cstring>
#include <regex>

#ifdef _WIN32// windows
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

 #else// linux
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include<netdb.h>
 #include <unistd.h>

 typedef unsigned char UCHAR;
#endif

//#define SHOW_LOG

/**
 * This code is referenced from https://rigtorp.se/spinlock/
 */
struct spinlock
{
    std::atomic<bool> lock_ = {0};

    void lock() noexcept
    {
        for (;;)
        {
            // Optimistically assume the lock is free on the first try
            if (!lock_.exchange(true, std::memory_order_acquire))
            {
                return;
            }
            // Wait for lock to be released without generating cache misses
            while (lock_.load(std::memory_order_relaxed))
            {
                // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                // hyper-threads
#ifdef __aarch64__
                std::this_thread::yield();
#elif _WIN32
                std::this_thread::yield();
#else
                __builtin_ia32_pause();
#endif
            }
        }
    }

    bool try_lock() noexcept
    {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load(std::memory_order_relaxed) && 
                !lock_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept
    {
        lock_.store(false, std::memory_order_release);
    }
};

#define PACKET_HEADER_SIZE	22
#define PACKET_PAYLOAD_SIZE	1450

enum IDClientException {SocketSetupError, SocketConnectError, SocketSendError, SocketReceiveError, SocketDisconnectError, 
                        NoRegistrationError, NoExistClientError};

/**
 * @brief For update method use
 * Replace: replace value while key exist.
 * Keep: preserve value while key exist.
 * Refresh: preserve value while key exist, delete non-exist key.
 * Append: preserve value while key exist, preserve non-exist key.
 */
enum UpdateMethod {Replace, Keep, Refresh, Append};

/**
 * @brief MySocket definition
 * MySocket must be inherited by Windows socket or linux socket object, such as WinSock.
 */
class MySocket
{
public:
    virtual void send(const char* msg, int size) { return; };
    virtual std::string recv(int size) { return ""; };
    virtual void disconnectSocket() { return; };
};


#ifdef __WIN32// windows
/**
 * @brief WinSock definition
 * WinSock is a windows socket object that inherit from MySocket.
 */
class WinSock : public MySocket
{
private:
    std::string _host;
    std::string _port;
    SOCKET _sock;
    struct addrinfo *_addr;

private:
    void _getAddrInfo();
    void _getConnectedSocket();
    
public:
    WinSock(std::string host, std::string port);
    SOCKET getConnectedSocket();
    void disconnectSocket();
    void send(const char* msg, int size);
    std::string recv(int size);
};

#else// linux
/**
 * @brief Sock definition
 * Sock is a linux socket object that inherit from MySocket.
 */
class Sock : public MySocket
{
private:
    std::string _host;
    int _port;
    int _socketFD;
    struct sockaddr_in _stSockAddr;

private:
    void _getSocket();
    void _getAddrInfo();
    void _socketConnection();
    
public:
    Sock(std::string host, std::string port);
    void disconnectSocket();
    void send(const char* msg, int size);
    std::string recv(int size);
};

#endif

/**
 * @brief IDServerProp definition
 * IDServerProp describes the packet header size, payload size and ID server address.
 */
class IDServerProp
{
public:
    std::string host;
    std::string port;
    size_t packetHeaderSize;
    size_t packetPayloadSize;
    size_t packetSize;

    int descripID_reg = 0x0001;
    int descripID_tab = 0x0002;
    int descripID_msg = 0x0003;

public:
    IDServerProp();
    IDServerProp(const IDServerProp& prop);
    IDServerProp(std::string host, std::string port, size_t packetHeaderSize, size_t packetPayloadSize);
};


/**
 * @brief PacketInfo definition
 * PacketInfo is use to storage send/receive data, including descriptor ID, device name, message and timestamp
 * descriptorID: a function code that ID server received.
 * device: in send function, <device> describes register device name or the device that message sends to.
 * In receive function, <device> describes the device which sends the message.
 * msg: in send function, <msg> means the sending message. In receive function, <msg> means the received message.
 * timestamp: not use.
 */
class PacketInfo
{
public:
    int descriptorID;
    std::string device;
    std::string msg;
    size_t timestamp;

public:
    PacketInfo() { this->timestamp = -1; };
    PacketInfo(int id, std::string device, std::string msg) : descriptorID(id), device(device), msg(msg) { this->timestamp = -1; };
    void setPacketInfo(int id, std::string device, std::string msg, size_t timestamp);
};


/**
 * @brief IDClient definition
 * IDClient is a object providing simple functions connecting to ID server, such as ID server registration, 
 * ID table request and client message sending/receiving.
 * regToServer(): register <deviceID> ID server.
 * requestIDTableFromServer(): request current ID table. (no return value)
 * sendMsgToClient(): send <msg> to <toDeviceID>.
 * getIDTable(): get current ID table.
 * getLatestRecvMsg(): get current message from <deviceID>.
 */
class IDClient
{
private:
    MySocket* _sock;
    IDServerProp _prop;

    std::string _deviceID;

    UCHAR* _sendBuf;
    int _currentPacketNum = 0;
    int _lastPacketNum = 0;

    std::thread _recvTH;
    spinlock _recvLock;
    spinlock _sendLock;// lock send event such as connToServer(), regToServer(), requestIDTableFromServer() and sendMsgToClient()
    spinlock _IDTableLock;// Prevent _IDTable data races
    spinlock _recvPkgLock;// Prevent _recvPacketInfo data races

    std::atomic<bool> _runState;// atomic
    std::atomic<bool> _regState;// atomic
    std::atomic<bool> _recvState;// atomic

    std::vector<std::string> _IDTable;
    PacketInfo _sendPacketInfo;
    std::vector<PacketInfo> _recvPacketInfo;// Record latest msg from each device (dynamic increasing)

    std::atomic<bool> _updateRecvPacketF;// atomic
    std::atomic<bool> _setrecvMsgFuncPtrF;// atomic

    std::function<void(IDClient*, std::string, std::string)> _recvMsgFuncPtr;// Receive message descriptor event handler function pointer

private:
    void _PerfectHeader();// Called by _sendEvent()
    void _PerfectPacket();// Called by _sendEvent()

    void _sendEvent();// Not thread safe, call this function after thread lock
    void _recvThread();// Called if connToServer() succeed
    void _updateRecvPacketInfo(std::string deviceID, std::string msg, UpdateMethod md);// Thread safe
    void _updateRecvPacketInfo(std::vector<PacketInfo>& recvPacketInfoVec, UpdateMethod md);// Thread safe
    bool _checkExistRecvPacket(std::string deviceID) const;// Not thread safe, do not call
    bool _checkExistID(std::string deviceID);// Thread safe

    void _recvMsgDescriptorEvent(std::string fromDevice, std::string recvMsg);// Raise message descriptor event

public:
    IDClient(IDServerProp& prop);
    ~IDClient();
    void connToServer();// Thread safe
    void regToServer(std::string deviceID);// Thread safe
    void requestIDTableFromServer();// Thread safe
    void sendMsgToClient(std::string toDeviceID, std::string msg);// Thread safe
    void close();

    IDServerProp& getIDServerProp();
    std::vector<std::string> getIDTable();
    std::string getLatestRecvMsg(std::string deviceID);
    bool isServerConn() const;
    bool isServerReg() const;
    bool isReceving() const;

    void setRecvMsgEventHandler(const std::function<void(IDClient*, std::string, std::string)> &f, bool updateRecvPacketF);// Set Receive message descriptor event handler function pointer
};

bool CheckIPv4Format(std::string ip);
std::string GetIPFromHostName(std::string hostname);
