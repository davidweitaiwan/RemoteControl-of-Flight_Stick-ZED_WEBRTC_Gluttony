#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <iomanip>
#include <thread>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

#include <LogitechSteeringWheelLib.h>

using namespace std::chrono_literals;

enum class GearState { Park, Reverse, Neutral, Drive };

class WheelProp
{
public:
	int initGasValue;
	int initBrakeValue;
	int initClutchValue;
	int sensitive;
	WheelProp() : initGasValue(-1), initBrakeValue(-1), initClutchValue(-1), sensitive(-1) {};
	WheelProp(int _gas, int _brake, int _clutch, int sensitive) : initGasValue(_gas), initBrakeValue(_brake), initClutchValue(_clutch), sensitive(sensitive) {};
};

class WheelState
{
private:
	int index_;
	WheelProp prop_;
	GearState gear_;
	int wheel_;
	int gas_;
	int brake_;
	int clutch_;

	int motionType_;

	std::atomic<bool> isInitF_;
	std::atomic<bool> GHubF_;
	std::string msg_;

	std::mutex internalLock_;
	std::mutex padelLock_;

private:
	template <typename T>
	void _safeSave(T* ptr, const T value)
	{
		this->internalLock_.lock();
		*ptr = value;
		this->internalLock_.unlock();
	}

	template <typename T>
	T _safeCall(const T* ptr)
	{
		this->internalLock_.lock();
		T tmp = *ptr;
		this->internalLock_.unlock();
		return tmp;
	}

	int _padelSafeCall(const int* ptr)
	{
		this->padelLock_.lock();
		int tmp = *ptr;
		this->padelLock_.unlock();
		return tmp;
	}

public:
	WheelState(int idx) : index_(idx), gear_(GearState::Neutral), wheel_(0), gas_(0), brake_(0), clutch_(0), motionType_(1), isInitF_(false), GHubF_(false) {};

	void Initial(WheelProp _property)
	{
		this->_safeSave(&this->prop_, _property);
		this->isInitF_ = true;
	}

	void setGHubF(bool _flag) { this->GHubF_ = _flag; }

	void setMsg(std::string _msg) { this->_safeSave(&this->msg_, _msg); }

	void setPadel(int _gas, int _brake, int _clutch)
	{
		if (!this->isInitF_) return;
		this->padelLock_.lock();
		this->gas_ = _gas;
		this->brake_ = _brake;
		this->clutch_ = _clutch;
		this->padelLock_.unlock();
	}

	void setWheel(int _wheel)
	{
		if (!this->isInitF_) return;
		this->_safeSave(&this->wheel_, _wheel);
	}

	void setMotionType(int _type)
	{
		if (!this->isInitF_) return;
		this->_safeSave(&this->motionType_, _type);
	}

	void setGear(GearState _gear)
	{
		if (!this->isInitF_) return;
		this->_safeSave(&this->gear_, _gear);
	}

	void shiftGearUp()
	{
		if (!this->isInitF_) return;
		this->internalLock_.lock();
		if (this->gear_ == GearState::Drive)
			this->gear_ = GearState::Drive;
		else if (this->gear_ == GearState::Neutral)
			this->gear_ = GearState::Drive;
		else if (this->gear_ == GearState::Reverse)
			this->gear_ = GearState::Neutral;
		else if (this->gear_ == GearState::Park)
			this->gear_ = GearState::Reverse;
		this->internalLock_.unlock();
	}

	void shiftGearDown()
	{
		if (!this->isInitF_) return;
		this->internalLock_.lock();
		if (this->gear_ == GearState::Park)
			this->gear_ = GearState::Park;
		else if (this->gear_ == GearState::Reverse)
			this->gear_ = GearState::Park;
		else if (this->gear_ == GearState::Neutral)
			this->gear_ = GearState::Reverse;
		else if (this->gear_ == GearState::Drive)
			this->gear_ = GearState::Neutral;
		this->internalLock_.unlock();
	}


	bool isInit() const { return this->isInitF_; }

	bool isGHub() const { return this->GHubF_; }

	int getIndex() const { return index_; }

	int getGas() { return this->_padelSafeCall(&this->gas_); }

	int getBrake() { return this->_padelSafeCall(&this->brake_); }

	int getClutch() { return this->_padelSafeCall(&this->clutch_); }

	int getWheel() { return this->_safeCall(&this->wheel_); }

	int getMotionType() { return this->_safeCall(&this->motionType_); }

	GearState getGear() { return this->_safeCall(&this->gear_); }

	std::string getGearString()
	{
		GearState gear = this->_safeCall(&this->gear_);
		if (gear == GearState::Drive)
			return "Drive";
		else if (gear == GearState::Neutral)
			return "Neutral";
		else if (gear == GearState::Reverse)
			return "Reverse";
		else if (gear == GearState::Park)
			return "Park";
	}

	std::string getMsg() { return this->_safeCall(&this->msg_); }

	friend class SimpleWheelState;
};

class SimpleWheelState
{
private:
	WheelState* _ws;
	std::mutex internal_lock;
public:
	int wheelState;
	int accelerator;
	int brake;
	int clutch;
	GearState gear;
public:
	SimpleWheelState(WheelState* wh)
	{
		_ws = wh;
		wheelState = 1;
		accelerator = 0;
		brake = 0;
		gear = GearState::Neutral;
	}

	void Update()
	{
		internal_lock.lock();
		wheelState = _ws->getWheel();
		accelerator = -(_ws->getGas() - _ws->prop_.initGasValue) * 255.0 / 65535;
		brake = -(_ws->getBrake() - _ws->prop_.initBrakeValue) * 255.0 / 65535;
		clutch = -(_ws->getClutch() - _ws->prop_.initClutchValue) * 255.0 / 65535;
		gear = _ws->getGear();
		internal_lock.unlock();
	}

	int getWheelState() const { return wheelState; }

	int getAccelerator() const { return accelerator; }

	int getBrake() const { return brake; }

	int getClutch() const { return clutch; }
};

inline void wctoc(const wchar_t* wchar, char* dst, size_t BUFFSIZE)
{
	memset(dst, '\0', BUFFSIZE);
	size_t l = 0;
	while (*(wchar + l) != 0 && l < BUFFSIZE - 1)
	{
		*(dst + l) = *(wchar + l);
		l++;
	}
}

void main_logi(bool& stopF, bool& showF, WheelState& logiState)
{
	logiState.setGHubF(false);
	while (!LogiSteeringInitialize(true) && !stopF)
	{
		logiState.setMsg("LogiSteering Initialize Failed.");
		std::this_thread::sleep_for(500ms);
	}
	logiState.setGHubF(true);
	logiState.setMsg("LogiSteering Initialized.");
	int controller_idx = logiState.getIndex();
	DIJOYSTATE2* SWState = LogiGetState(controller_idx);
	wchar_t buffer[1024];
	char bufferTrans[1024];
	if (LogiGetFriendlyProductName(controller_idx, buffer, 1024))
	{
		wctoc(buffer, bufferTrans, 1024);
		char buf[1024];
		sprintf(buf, "Controller: %s", bufferTrans);
		logiState.setMsg(buf);
	}
	LogiPlayDamperForce(controller_idx, -40);
	while (LogiUpdate())
	{
		try
		{
			if (SWState == nullptr)
				throw - 1;
			LogiPlayLeds(controller_idx, 32768 - SWState->lY, 2000, 50000);
			logiState.setWheel(SWState->lX);
			logiState.setPadel(SWState->lY, SWState->lRz, SWState->rglSlider[0]);
			if (LogiButtonTriggered(controller_idx, 23))
			{
				WheelProp initProp(SWState->lY, SWState->lRz, SWState->rglSlider[0], 3000);
				logiState.Initial(initProp);
			}
			if (LogiButtonTriggered(controller_idx, 4))
				logiState.shiftGearUp();
			else if (LogiButtonTriggered(controller_idx, 5))
				logiState.shiftGearDown();
			if (LogiButtonTriggered(controller_idx, 1))
				logiState.setMotionType(1);// Ackermann turn
			else if (LogiButtonTriggered(controller_idx, 3))
				logiState.setMotionType(2);// 4ws center baseline
			else if (LogiButtonTriggered(controller_idx, 2))
				logiState.setMotionType(3);// Zero turn
			else if (LogiButtonTriggered(controller_idx, 0))
				logiState.setMotionType(4);// Parallel turn
		}
		catch (...)
		{
			logiState.setWheel(0);
			logiState.setPadel(0, 0, 0);
			logiState.setMsg("Null pointer, please open G Hub");
		}
		if (stopF)
			break;
	}
	LogiStopSpringForce(controller_idx);
	LogiSteeringShutdown();
}