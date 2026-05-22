/*
 * SCSCL.h — SCSCL serial servo application layer
 * Source: M5Stack StackChan-BSP / FTServo_Arduino (MIT License)
 */
#ifndef _SCSCL_H
#define _SCSCL_H

#define SCSCL_VERSION_L 3
#define SCSCL_VERSION_H 4
#define SCSCL_ID 5
#define SCSCL_BAUD_RATE 6
#define SCSCL_MIN_ANGLE_LIMIT_L 9
#define SCSCL_MIN_ANGLE_LIMIT_H 10
#define SCSCL_MAX_ANGLE_LIMIT_L 11
#define SCSCL_MAX_ANGLE_LIMIT_H 12
#define SCSCL_CW_DEAD 26
#define SCSCL_CCW_DEAD 27

#define SCSCL_TORQUE_ENABLE 40
#define SCSCL_GOAL_POSITION_L 42
#define SCSCL_GOAL_POSITION_H 43
#define SCSCL_GOAL_TIME_L 44
#define SCSCL_GOAL_TIME_H 45
#define SCSCL_GOAL_SPEED_L 46
#define SCSCL_GOAL_SPEED_H 47
#define SCSCL_LOCK 48

#define SCSCL_PRESENT_POSITION_L 56
#define SCSCL_PRESENT_POSITION_H 57
#define SCSCL_PRESENT_SPEED_L 58
#define SCSCL_PRESENT_SPEED_H 59
#define SCSCL_PRESENT_LOAD_L 60
#define SCSCL_PRESENT_LOAD_H 61
#define SCSCL_PRESENT_VOLTAGE 62
#define SCSCL_PRESENT_TEMPERATURE 63
#define SCSCL_MOVING 66
#define SCSCL_PRESENT_CURRENT_L 69
#define SCSCL_PRESENT_CURRENT_H 70

#include "SCSerial.h"

class SCSCL : public SCSerial
{
public:
	SCSCL();
	SCSCL(u8 End);
	SCSCL(u8 End, u8 Level);
	int WritePos(u8 ID, u16 Position, u16 Time, u16 Speed = 0);
	int RegWritePos(u8 ID, u16 Position, u16 Time, u16 Speed = 0);
	void SyncWritePos(u8 ID[], u8 IDN, u16 Position[], u16 Time[], u16 Speed[]);
	int PWMMode(u8 ID);
	int WritePWM(u8 ID, s16 pwmOut);
	int SwitchMode(int ID, u8 mode);
	int EnableTorque(u8 ID, u8 Enable);
	int unLockEprom(u8 ID);
	int LockEprom(u8 ID);
	int FeedBack(int ID);
	int ReadPos(int ID);
	int ReadSpeed(int ID);
	int ReadLoad(int ID);
	int ReadVoltage(int ID);
	int ReadTemper(int ID);
	int ReadMove(int ID);
	int ReadCurrent(int ID);
	int ReadToqueEnable(int ID);
private:
	u8 Mem[SCSCL_PRESENT_CURRENT_H-SCSCL_PRESENT_POSITION_L+1];
	int min_angle[256] = {0};
	int max_angle[256] = {0};
	bool angle_limit_cached[256] = {false};
};

#endif
