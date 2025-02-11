/*
 * rv3032.cpp
 *
 *  Created on: Feb 10, 2025
 *      Author: dizcza
 */

#ifdef ARDUINO_ARCH_ESP32

#include <Wire.h>
#include "rv3032.h"

static TwoWire* _i2cPort = nullptr;


extern "C" void rv3032_init(void *i2cPort)
{
	_i2cPort = (TwoWire*) i2cPort;
}


extern "C" esp_err_t rv3032_ping() {
    _i2cPort->beginTransmission(RV3032_ADDRESS);
    bool success = _i2cPort->endTransmission() == 0;
    return success ? ESP_OK : ESP_FAIL;
}


extern "C" esp_err_t rv3032_writeRegBuff(uint8_t reg_addr, const uint8_t* data, size_t len)
{
	_i2cPort->beginTransmission(RV3032_ADDRESS);
	_i2cPort->write(reg_addr);
	for (int i = 0; i < len; i++) {
		_i2cPort->write(data[i]);
	}
	bool success = _i2cPort->endTransmission() == 0;
	return success ? ESP_OK : ESP_FAIL; 
}


extern "C" esp_err_t rv3032_readRegBuff(uint8_t reg_addr, uint8_t* data, size_t len)
{
	_i2cPort->beginTransmission(RV3032_ADDRESS);
	_i2cPort->write(reg_addr);
	_i2cPort->endTransmission(false);
	if (_i2cPort->requestFrom((uint8_t) RV3032_ADDRESS, (uint8_t) len) != len) {
		return ESP_FAIL;
	}
	for (int i = 0; i < len; i++) {
		data[i] = _i2cPort->read();
	}
	return ESP_OK;
}


#endif /* ARDUINO_ARCH_ESP32 */
