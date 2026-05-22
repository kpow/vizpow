/*
 * SCSerial.h — FTServo hardware interface layer (ESP-IDF UART)
 * Source: M5Stack StackChan-BSP / FTServo_Arduino (MIT License)
 */
#ifndef _SCSERIAL_H
#define _SCSERIAL_H

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "SCS.h"

class SCSerial : public SCS
{
public:
	SCSerial();
	SCSerial(u8 End);
	SCSerial(u8 End, u8 Level);
	bool begin(uart_port_t uart_num, int baud_rate, int tx_pin, int rx_pin, int buf_size = 1024);
	void end();

protected:
	int writeSCS(unsigned char *nDat, int nLen);
	int readSCS(unsigned char *nDat, int nLen);
	int readSCS(unsigned char *nDat, int nLen, unsigned long TimeOut);
	int writeSCS(unsigned char bDat);
	void rFlushSCS();
	void wFlushSCS();
public:
	unsigned long IOTimeOut;
	uart_port_t uart_num;
};

#endif
