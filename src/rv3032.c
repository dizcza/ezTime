#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "rv3032.h"
#include "bsp.h"
#include <math.h>
#include <stdint.h>
#include <sys/param.h>


#ifndef ARDUINO_ARCH_ESP32
#include "i2cdev.h"
static i2c_dev_t i2cdev = {};
#endif  // ARDUINO_ARCH_ESP32


#ifndef TAG
static const char* TAG = "RV3032";
#endif  /* TAG */

static const int8_t offset_delta_max = 10;  // in [us]


static uint8_t bcd2bin(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0f);
}

static uint8_t bin2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}


static void rv3032_putUINT(uint8_t *buff, uint64_t val, int bytes) {
    for (int i = 0; i < bytes; i++) {
        const int shift = 8 * (bytes - i - 1);
        buff[i] = (uint8_t) ((val >> shift) & 0xFF);
    }
}


static uint64_t rv3032_extractUINT(const uint8_t *buff, int bytes) {
    uint64_t val = 0;
    for (int i = 0; i < bytes; i++) {
        const int shift = 8 * (bytes - i - 1);
        val |= ((uint64_t) buff[i]) << shift;
    }
    return val;
}


static esp_err_t rv3032_changeRegisterMask(uint8_t reg, uint8_t mask, bool enable) {
    uint8_t regVal;
    BSP_SOFTCHECK(rv3032_readReg(reg, &regVal));
    if (enable) {
		regVal |= mask;
	} else {
		regVal &= ~mask;
	}
    BSP_SOFTCHECK(rv3032_writeReg(reg, regVal));
    return ESP_OK;
}


static esp_err_t rv3032_setRegisterMask(uint8_t reg, uint8_t mask) {
	return rv3032_changeRegisterMask(reg, mask, true);
}


static esp_err_t rv3032_clearRegisterMask(uint8_t reg, uint8_t mask) {
	return rv3032_changeRegisterMask(reg, mask, false);
}


#ifndef ARDUINO_ARCH_ESP32
esp_err_t rv3032_init(int port, int sda_gpio, int scl_gpio)
{
    ESP_LOGI(TAG, "Initializing RV3032");
    memset(&i2cdev, 0, sizeof(i2c_dev_t));
    i2cdev.port = port;
    i2cdev.addr = RV3032_ADDRESS;
    i2cdev.cfg.sda_io_num = sda_gpio;
    i2cdev.cfg.scl_io_num = scl_gpio;
    i2cdev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2cdev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2cdev.cfg.master.clk_speed = 400000;
    i2c_dev_create_mutex(&i2cdev);
    BSP_SOFTCHECK(rv3032_ping());
    rv3032_writeReg(R_RV3032_STATUS, 0);  // clear the status
    rv3032_updateEEPROM(R_RV3032_STATUS);
    rv3032_writeReg(R_RV3032_EVI_CONTROL, 0);
    rv3032_updateEEPROM(R_RV3032_EVI_CONTROL);
    rv3032_setAgeOffset(rv3032_getEEPROMAgeBest());
    return ESP_OK;
}
#endif  // ARDUINO_ARCH_ESP32


void rv3032_deinit() {
#ifndef ARDUINO_ARCH_ESP32
    i2c_dev_delete_mutex(&i2cdev);
#endif
}


#ifndef ARDUINO_ARCH_ESP32
esp_err_t rv3032_ping() {
    return i2c_dev_probe(&i2cdev, I2C_DEV_WRITE);
}
#endif


#ifndef ARDUINO_ARCH_ESP32
esp_err_t rv3032_writeRegBuff(uint8_t reg_addr, const uint8_t* data, size_t len)
{
	if (i2cdev.addr == 0) {
		return ESP_ERR_INVALID_STATE;
	}
    I2C_DEV_TAKE_MUTEX(&i2cdev);
    I2C_DEV_CHECK(&i2cdev, i2c_dev_write_reg(&i2cdev, reg_addr, data, len));
    I2C_DEV_GIVE_MUTEX(&i2cdev);
    return ESP_OK;
}
#endif


#ifndef ARDUINO_ARCH_ESP32
esp_err_t rv3032_readRegBuff(uint8_t reg_addr, uint8_t* data, size_t len)
{
	if (i2cdev.addr == 0) {
		return ESP_ERR_INVALID_STATE;
	}
    I2C_DEV_TAKE_MUTEX(&i2cdev);
    I2C_DEV_CHECK(&i2cdev, i2c_dev_read_reg(&i2cdev, reg_addr, data, len));
    I2C_DEV_GIVE_MUTEX(&i2cdev);
    return ESP_OK;
}
#endif


esp_err_t rv3032_writeReg(uint8_t reg_addr, uint8_t val)
{
	return rv3032_writeRegBuff(reg_addr, &val, 1);
}


esp_err_t rv3032_readReg(uint8_t reg_addr, uint8_t *val)
{
	return rv3032_readRegBuff(reg_addr, val, 1);
}


bool rv3032_isValid() {
    uint8_t val;
    if (rv3032_readReg(R_RV3032_STATUS, &val) != ESP_OK) {
        return false;
    }
    return ((val & 0b11) == 0); // both voltage low and POR flags are cleared
}


static void rv3032_packDateTime(uint8_t data[7], const struct tm *tm) {
    data[0] = bin2bcd(tm->tm_sec);
    data[1] = bin2bcd(tm->tm_min);
    data[2] = bin2bcd(tm->tm_hour);
    /* The week data must be in the range 1 to 7, and to keep the start on the
     * same day as for tm_wday have it start at 1 on Sunday. */
    data[3] = bin2bcd(tm->tm_wday + 1);
    data[4] = bin2bcd(tm->tm_mday);
    data[5] = bin2bcd(tm->tm_mon + 1);  // month + century
    if (tm->tm_year >= 100) {
        data[5] |= 0x80; // century bit
    }
    // year
    data[6] = (((tm->tm_year % 100) / 10) << 4);
    data[6] |= (tm->tm_year % 10);
}


esp_err_t rv3032_setTime(const struct tm *tm) {
	// This function does not clear the 100th Seconds
	uint8_t data[7] = {};
	rv3032_packDateTime(data, tm);
    rv3032_writeRegBuff(R_RV3032_SECONDS, data, sizeof(data));
    return ESP_OK;
}


esp_err_t rv3032_setTimePrecise(const struct tm *tm) {
	// This function clears the 100th Seconds
	// This function seems not to work!
	// Do not use it!
	// In details, clearing the 100th Seconds
	// register should synchronize the clock.
	// However, it failed to do so.
	// Need to test it again.
	// (Refer to TIME SYNCHRONIZATION.)
	uint8_t data[8] = {};
	rv3032_packDateTime(&data[1], tm);
    rv3032_writeRegBuff(R_RV3032_100TH_SECONDS, data, sizeof(data));
    return ESP_OK;
}


static void rv3032_unpackDateTime(struct tm *time, const uint8_t data[7]) {
    /* convert to unix time structure */
    memset(time, 0, sizeof(struct tm));
    time->tm_sec = bcd2bin(data[0]);
    time->tm_min = bcd2bin(data[1]);
    time->tm_hour = bcd2bin(data[2]); /* 24H */
    time->tm_wday = bcd2bin(data[3]) - 1;
    time->tm_mday = bcd2bin(data[4]);
    time->tm_mon  = bcd2bin(data[5]) - 1;
    time->tm_year = bcd2bin(data[6]) + 100;
    time->tm_isdst = 0;
}


int64_t rv3032_getMicroseconds() {
	int64_t microseconds = 0;
	rv3032_time_t timePrecise = {};
	if (rv3032_getTimePrecise(&timePrecise) == ESP_OK) {
	    const int64_t t_utc = mktime(&timePrecise.tm);
	    microseconds = t_utc * SECOND_US + ((int64_t) timePrecise.seconds100th) * 10000LL;	
	}
	return microseconds;
}


esp_err_t rv3032_getTime(struct tm *time) {
    memset(time, 0, sizeof(struct tm));
    uint8_t data[7] = { 0 };
    rv3032_readRegBuff(R_RV3032_SECONDS, data, sizeof(data));
	rv3032_unpackDateTime(time, data);
    return ESP_OK;
}


esp_err_t rv3032_getTimePrecise(rv3032_time_t* timePrecise) {
    uint8_t data[8] = { 0 };
    rv3032_readRegBuff(R_RV3032_100TH_SECONDS, data, sizeof(data));
	rv3032_unpackDateTime(&timePrecise->tm, &data[1]);
	timePrecise->seconds100th = bcd2bin(data[0]);
	return ESP_OK;
}


esp_err_t rv3032_getTemperature(float *temperature)
{
    uint8_t data[2] = { 0 };
    BSP_SOFTCHECK(rv3032_readRegBuff(R_RV3032_TEMPERATURE_L, data, sizeof(data)));
    int16_t calcVar = (((int16_t) data[1]) << 4) | ((data[0] & 0xF0) >> 4);
    if (data[1] & (1 << 7)) {
    	// Temp is 12-bit signed, inverse
        calcVar -= (1 << 12);
    }
    *temperature = (float)calcVar * 0.0625;
    return ESP_OK;
}


esp_err_t rv3032_waitBusy() {
    uint8_t busy;
    BSP_SOFTCHECK(rv3032_readReg(R_RV3032_TEMPERATURE_L, &busy));
    while (busy & R_RV3032_TEMPERATURE_L_EEBUSY) {
        // busy with reading/writing EEPROM
        vTaskDelay(pdMS_TO_TICKS(10));
        BSP_SOFTCHECK(rv3032_readReg(R_RV3032_TEMPERATURE_L, &busy));
    }
    return ESP_OK;
}


esp_err_t rv3032_writeEEPROMBuff(uint8_t addr, const uint8_t *buff, size_t len) {
    rv3032_setRegisterMask(R_RV3032_CONTROL_1, R_RV3032_CONTROL_1_EERD);  // set EERD = 1

    for (int i = 0; i < len; i++) {
	    rv3032_writeReg(R_RV3032_EE_ADDRESS, addr + i);
	    rv3032_waitBusy();
	    rv3032_writeReg(R_RV3032_EE_DATA, buff[i]);
	    rv3032_waitBusy();
	    rv3032_writeReg(R_RV3032_EE_COMMAND, R_RV3032_EE_COMMAND_WRITE);
	    rv3032_waitBusy();
	}
    
    rv3032_clearRegisterMask(R_RV3032_CONTROL_1, R_RV3032_CONTROL_1_EERD);  // set EERD = 0
    return ESP_OK;
}


esp_err_t rv3032_refreshEEPROM() {
	// Copy EEPROM -> RAM
    rv3032_setRegisterMask(R_RV3032_CONTROL_1, R_RV3032_CONTROL_1_EERD);  // set EERD = 1
    rv3032_writeReg(R_RV3032_EE_COMMAND, R_RV3032_EE_COMMAND_REFRESH);
    rv3032_waitBusy();
    rv3032_clearRegisterMask(R_RV3032_CONTROL_1, R_RV3032_CONTROL_1_EERD);  // set EERD = 0
    return ESP_OK;
}


esp_err_t rv3032_readEEPROMBuff(uint8_t addr, uint8_t *buff, size_t len) {
	uint8_t cntrl_val;
    BSP_SOFTCHECK(rv3032_readReg(R_RV3032_CONTROL_1, &cntrl_val));
    rv3032_writeReg(R_RV3032_CONTROL_1, cntrl_val | R_RV3032_CONTROL_1_EERD); // set EERD = 1

    for (int i = 0; i < len; i++) {
	    rv3032_writeReg(R_RV3032_EE_ADDRESS, addr + i);
	    rv3032_waitBusy();
	    rv3032_writeReg(R_RV3032_EE_COMMAND, R_RV3032_EE_COMMAND_READ);
	    rv3032_waitBusy();
	    rv3032_readReg(R_RV3032_EE_DATA, &buff[i]);
	    rv3032_waitBusy();
    }
	    
    rv3032_clearRegisterMask(R_RV3032_CONTROL_1, R_RV3032_CONTROL_1_EERD);  // set EERD = 0
    return ESP_OK;
}


esp_err_t rv3032_writeEEPROM(uint8_t addr, uint8_t regVal) {
	return rv3032_writeEEPROMBuff(addr, &regVal, 1);
}


esp_err_t rv3032_readEEPROM(uint8_t addr, uint8_t *regVal) {
	return rv3032_readEEPROMBuff(addr, regVal, 1);
}


esp_err_t rv3032_updateEEPROM(uint8_t reg) {
    uint8_t regVal;
    BSP_SOFTCHECK(rv3032_readReg(reg, &regVal));
    BSP_SOFTCHECK(rv3032_writeEEPROM(reg, regVal));
    return ESP_OK;
}


esp_err_t rv3032_setClockOutHF(uint16_t hfClock_steps) {
    BSP_SOFTCHECK(rv3032_writeReg(E_RV3032_CLKOUT1, hfClock_steps));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_CLKOUT1));
    BSP_SOFTCHECK(rv3032_writeReg(E_RV3032_CLKOUT2, RV3032_HF_MODE | ((hfClock_steps >> 8) & 0x1F)));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_CLKOUT2));
    BSP_SOFTCHECK(rv3032_enableClockOut(true));
    return ESP_OK;
}


const char* rv3032_ClockOut2Str(enum RV3032_CLKOUT clockOut) {
    switch (clockOut) {
    case RV3032_DISABLE_CLKOUT:
        return "RV3032_DISABLE_CLKOUT";
    case RV3032_XTAL_32KHZ_OUT:
        return "RV3032_32KHZ";
    case RV3032_XTAL_1024HZ_OUT:
        return "RV3032_1KHZ";
    case RV3032_XTAL_64HZ_OUT:
        return "RV3032_64HZ";
    case RV3032_XTAL_1HZ_OUT:
        return "RV3032_1HZ";
    case RV3032_HF_MODE:
        return "RV3032_HF_MODE";
    default:
        return "RV3032 CLKOUT UNKNOWN";
    }
}


esp_err_t rv3032_getClockOut(enum RV3032_CLKOUT *clockOut) {
    uint8_t val = 0;
    esp_err_t err = rv3032_readReg(E_RV3032_CLKOUT2, &val);
    *clockOut = val;
    return err;
}


static uint8_t rv3032_convertAgeToByte(int8_t age) {
    if (age < RV3032_AGE_MIN) age = RV3032_AGE_MIN;
    if (age > RV3032_AGE_MAX) age = RV3032_AGE_MAX;
    age &= 0x3F;  // [-32, 31] --> [0, 63] range
    return age;
}


static int8_t rv3032_convertByteToAge(uint8_t val) {
    val &= 0x3f;
    int8_t age = 0;
    if (val > RV3032_AGE_MAX) {
        // [-32, -1]
        age = val - 64;
    } else {
        age = val;
    }
    return age;
}


esp_err_t rv3032_getAgeOffset(int8_t *age) {
    uint8_t val = 0;
    BSP_SOFTCHECK(rv3032_readReg(E_RV3032_OFFSET, &val));
	*age = rv3032_convertByteToAge(val);
    return ESP_OK;
}


esp_err_t rv3032_setAgeOffset(int8_t age) {
    BSP_SOFTCHECK(rv3032_writeReg(E_RV3032_OFFSET, rv3032_convertAgeToByte(age)));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_OFFSET));
    return ESP_OK;
}


esp_err_t rv3032_setClockOut(enum RV3032_CLKOUT clockOut)
{
    if (clockOut == RV3032_HF_MODE) {
        ESP_LOGW(TAG, "For RV3032_HF_MODE, use rv3032_setClockOutHF()");
        return ESP_ERR_INVALID_ARG;
    }
    BSP_SOFTCHECK(rv3032_writeReg(E_RV3032_CLKOUT2, clockOut));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_CLKOUT2));
    ESP_LOGI(TAG, "%s %s OK", __func__, rv3032_ClockOut2Str(clockOut));
    return ESP_OK;
}


esp_err_t rv3032_enableClockOut(bool enable)
{
	// We need to negate the E_RV3032_PMU_NCLKE flag
    BSP_SOFTCHECK(rv3032_changeRegisterMask(E_RV3032_PMU, E_RV3032_PMU_NCLKE, !enable));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_PMU));
    ESP_LOGI(TAG, "%s CLKOUT", enable ? "Enabled" : "Disabled");
    return ESP_OK;
}


esp_err_t rv3032_setBSM(enum RV3032_BSM bsm)
{
    uint8_t regVal;
    BSP_SOFTCHECK(rv3032_readReg(E_RV3032_PMU, &regVal));
    regVal &= ~(E_RV3032_PMU_BSM_1 | E_RV3032_PMU_BSM_1);
    regVal |= bsm;
    BSP_SOFTCHECK(rv3032_writeReg(E_RV3032_PMU, regVal));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_PMU));
    ESP_LOGI(TAG, "%s 0x%02x", __func__, bsm);
    return ESP_OK;
}

esp_err_t rv3032_setTrickleCharge(enum RV3032_TCR tcr, enum RV3032_TCM tcm)
{
    uint8_t regVal;
    BSP_SOFTCHECK(rv3032_readReg(E_RV3032_PMU, &regVal));
    regVal &= (E_RV3032_PMU_NCLKE | E_RV3032_PMU_BSM_1 | E_RV3032_PMU_BSM_1);
    regVal |= tcr | tcm;
    BSP_SOFTCHECK(rv3032_writeReg(E_RV3032_PMU, regVal));
    BSP_SOFTCHECK(rv3032_updateEEPROM(E_RV3032_PMU));
    ESP_LOGI(TAG, "%s TCR 0x%02x TCM 0x%02x", __func__, tcr, tcm);
    return ESP_OK;
}


esp_err_t rv3032_enableEVI(bool enable) {
    // Initialize bit EIE to 0.
    rv3032_clearRegisterMask(R_RV3032_CONTROL_2, R_RV3032_CONTROL_2_EIE);

	// Clear flag EVF to 0.
    rv3032_clearRegisterMask(R_RV3032_STATUS, R_RV3032_STATUS_EVF);

	// Write 1 to EVR bit, to reset all Time Stamp EVI registers to 00h
    rv3032_setRegisterMask(R_RV3032_TIMESTAMP_CTRL, R_RV3032_TIMESTAMP_CTRL_EVR);
	
    uint8_t regVal;
	// Set EHL bit to 0 or 1 to choose falling edge/low level or rising edge/high level detection on pin EVI.
    BSP_SOFTCHECK(rv3032_readReg(R_RV3032_EVI_CONTROL, &regVal));
    regVal |= R_RV3032_EVI_CONTROL_EHL;  // PPS ticks on rising edge
    if (enable) {
    	regVal |= R_RV3032_EVI_CONTROL_ESYN;
	} else {
		regVal &= ~R_RV3032_EVI_CONTROL_ESYN;
	}
    BSP_SOFTCHECK(rv3032_writeReg(R_RV3032_EVI_CONTROL, regVal));

    // Set CEIE bit to 1 if you want to enable clock output when external event occurs
    rv3032_setRegisterMask(R_RV3032_COCK_INT_MASK, R_RV3032_COCK_INT_MASK_CEIE);
    
    ESP_LOGI(TAG, "%s %s", __func__, enable ? "enabled" : "disabled");
    return ESP_OK;
}


uint8_t rv3032_readUserEEPROM(uint8_t addr) {
	if (addr < E_RV3032_USER_EEPROM_START || addr > E_RV3032_USER_EEPROM_END) {
		return 0;
	}
	uint8_t regVal = 0;
	BSP_SOFTCHECK(rv3032_readEEPROM(addr, &regVal));
	return regVal;
}


esp_err_t rv3032_writeUserEEPROM(uint8_t addr, uint8_t val) {
	if (addr < E_RV3032_USER_EEPROM_START || addr > E_RV3032_USER_EEPROM_END) {
		return ESP_ERR_INVALID_ARG;
	}
    BSP_SOFTCHECK(rv3032_writeEEPROM(addr, val));
    return ESP_OK;
}


bool rv3032_getEEPROMESYNSupported() {
	return rv3032_readUserEEPROM(E_RV3032_EEPROM_ESYN_SUPPORTED);
}


esp_err_t rv3032_writeEEPROMESYNSupported(bool val) {
	return rv3032_writeUserEEPROM(E_RV3032_EEPROM_ESYN_SUPPORTED, val);
}


esp_err_t rv3032_writeEEPROMAgeBest(int8_t ageBest) {
	BSP_SOFTCHECK(rv3032_writeUserEEPROM(E_RV3032_EEPROM_AGE_BEST, rv3032_convertAgeToByte(ageBest)));
	ESP_LOGI(TAG, "Saved AGE BEST %d", ageBest);
	return ESP_OK;
}


int8_t rv3032_getEEPROMAgeBest() {
	return rv3032_convertByteToAge(rv3032_readUserEEPROM(E_RV3032_EEPROM_AGE_BEST));
}


esp_err_t rv3032_writeEEPROMClkoutOffset(int32_t offset) {
	uint8_t data[2];
	rv3032_putUINT(data, offset / 20, sizeof(data));
	return rv3032_writeEEPROMBuff(E_RV3032_EEPROM_CLKOUT_OFFSET, data, sizeof(data));
}


int32_t rv3032_getEEPROMClkoutOffset() {
	uint8_t data[2] = {};
	rv3032_readEEPROMBuff(E_RV3032_EEPROM_CLKOUT_OFFSET, data, sizeof(data));
	uint64_t val = rv3032_extractUINT(data, sizeof(data));
	int32_t offset = (int32_t) (val * 20);
	return offset;
}


esp_err_t rv3032_writeEEPROMAgeStat(const rtc_age_stat_t* ageStat) {
	uint8_t data[9] = {};
	for (int i = 0; i < 3; i++) {
		if (ageStat->age[i] < RV3032_AGE_MIN || ageStat->age[i] > RV3032_AGE_MAX) {
			return ESP_FAIL;
		}
		if (fabsf(ageStat->offset_delta[i]) > offset_delta_max) {
			return ESP_FAIL;
		}
		data[i * 3] = (uint8_t) ageStat->age[i];
		const float val = MAX(-offset_delta_max, MIN(offset_delta_max, ageStat->offset_delta[i]));
		const int16_t val_int = (int16_t) (val * 10000);
		data[i * 3 + 1] = (uint8_t) (val_int >> 8);
		data[i * 3 + 2] = (uint8_t) (val_int & 0xFF);
	}
	return rv3032_writeEEPROMBuff(E_RV3032_EEEROM_AGE_STAT, data, sizeof(data));
}


esp_err_t rv3032_getEEPROMAgeStat(rtc_age_stat_t* ageStat) {
	uint8_t data[9] = {};
	BSP_SOFTCHECK(rv3032_readEEPROMBuff(E_RV3032_EEEROM_AGE_STAT, data, sizeof(data)));
	bool all_zero = true;
	for (int i = 0; i < sizeof(data); i++) {
		all_zero &= data[i] == 0;
	}
	if (all_zero) {
		return ESP_FAIL;
	}
	for (int i = 0; i < 3; i++) {
		ageStat->age[i] = (int8_t) data[i * 3];
		if (ageStat->age[i] < RV3032_AGE_MIN || ageStat->age[i] > RV3032_AGE_MAX) {
			memset(ageStat, 0, sizeof(rtc_age_stat_t));
			return ESP_FAIL;
		}
		uint16_t val = (((uint16_t) data[i * 3 + 1]) << 8) | data[i * 3 + 2];
		ageStat->offset_delta[i] = ((int16_t) val) / 10000.0;
		if (fabsf(ageStat->offset_delta[i]) > offset_delta_max) {
			memset(ageStat, 0, sizeof(rtc_age_stat_t));
			return ESP_FAIL;
		}
	}
	// Fix EEPROM cell bug!
	ageStat->age[2] = MIN(RV3032_AGE_MAX, ageStat->age[1] + 1);
	return ESP_OK;
}

