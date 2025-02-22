/**
 * RV-3032-C7 Library
 *
 * Basic library for the swiss real-time-clock RV-3032-C7. The chip has a ton of features but the ones
 * needed for basic rtc functionality are programmed. More advanced features of the chip like the timer,
 * event interrupt system and such are left unimplemented (yet)
 */


#ifndef _RTC_RV3032_H_
#define _RTC_RV3032_H_

#include "esp_err.h"
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#ifdef __cplusplus
 extern "C" {
#endif

#define RV3032_ADDRESS          0x51

/* Registers of the RV-3032-C7 */
#define R_RV3032_100TH_SECONDS  0x00
#define R_RV3032_SECONDS        0x01
#define R_RV3032_MINUTES        0x02
#define R_RV3032_HOURS          0x03
#define R_RV3032_WEEKDAY        0x04
#define R_RV3032_DATE           0x05
#define R_RV3032_MONTH          0x06
#define R_RV3032_YEAR           0x07
#define R_RV3032_MIN_ALARM      0x08
#define R_RV3032_MIN_ALARM_AE_M     0x80
#define R_RV3032_HOUR_ALARM     0x09
#define R_RV3032_HOUR_ALARM_AE_H    0x80
#define R_RV3032_DATE_ALARM     0x0A
#define R_RV3032_DATE_ALARM_AE_D    0x80
#define R_RV3032_TIMER_VALUE_0  0x0B
#define R_RV3032_TIMER_VALUE_1  0x0C
#define R_RV3032_STATUS         0x0D
#define R_RV3032_STATUS_THX         0x80
#define R_RV3032_STATUS_TLF         0x40
#define R_RV3032_STATUS_UF          0x20
#define R_RV3032_STATUS_TF          0x10
#define R_RV3032_STATUS_AF          0x08
#define R_RV3032_STATUS_EVF         0x04
#define R_RV3032_STATUS_PORF        0x02
#define R_RV3032_STATUS_VLF         0x01
#define R_RV3032_TEMPERATURE_L      0x0E  // contains EE busy bit
#define R_RV3032_TEMPERATURE_L_EEF      0x08
#define R_RV3032_TEMPERATURE_L_EEBUSY   0x04
#define R_RV3032_TEMPERATURE_L_CLKF     0x02
#define R_RV3032_TEMPERATURE_L_BSF      0x01
#define R_RV3032_TEMPERATURE_H  0x0F
#define R_RV3032_CONTROL_1      0x10
#define R_RV3032_CONTROL_1_x        0x20
#define R_RV3032_CONTROL_1_USEL     0x10
#define R_RV3032_CONTROL_1_TE       0x08
#define R_RV3032_CONTROL_1_EERD     0x04
#define R_RV3032_CONTROL_1_TD1      0x02
#define R_RV3032_CONTROL_1_TD0      0x01
#define R_RV3032_CONTROL_2      0x11
#define R_RV3032_CONTROL_2_CLKIE    0x40
#define R_RV3032_CONTROL_2_UIE      0x20
#define R_RV3032_CONTROL_2_TIE      0x10
#define R_RV3032_CONTROL_2_AIE      0x08
#define R_RV3032_CONTROL_2_EIE      0x04
#define R_RV3032_CONTROL_2_X        0x02
#define R_RV3032_CONTROL_2_STOP     0x01
#define R_RV3032_CONTROL_3      0x12
#define R_RV3032_CONTROL_3_BSIE     0x10
#define R_RV3032_CONTROL_3_THE      0x08
#define R_RV3032_CONTROL_3_TLE      0x04
#define R_RV3032_CONTROL_3_THIE     0x02
#define R_RV3032_CONTROL_3_TLIE     0x01
#define R_RV3032_TIMESTAMP_CTRL 0x13
#define R_RV3032_TIMESTAMP_CTRL_EVR     0x20
#define R_RV3032_TIMESTAMP_CTRL_THR     0x10
#define R_RV3032_TIMESTAMP_CTRL_TLR     0x08
#define R_RV3032_TIMESTAMP_CTRL_EVOW    0x04
#define R_RV3032_TIMESTAMP_CTRL_THOW    0x02
#define R_RV3032_TIMESTAMP_CTRL_TLIE    0x01
#define R_RV3032_COCK_INT_MASK  0x14
#define R_RV3032_COCK_INT_MASK_CLKD     0x80
#define R_RV3032_COCK_INT_MASK_INTDE    0x40
#define R_RV3032_COCK_INT_MASK_CEIE     0x20
#define R_RV3032_COCK_INT_MASK_CAIE     0x10
#define R_RV3032_COCK_INT_MASK_CTIE     0x08
#define R_RV3032_COCK_INT_MASK_CUIE     0x04
#define R_RV3032_COCK_INT_MASK_CTHIE    0x02
#define R_RV3032_COCK_INT_MASK_CTLIE    0x01
#define R_RV3032_EVI_CONTROL    0x15
#define R_RV3032_EVI_CONTROL_CLKDE      0x80
#define R_RV3032_EVI_CONTROL_EHL        0x40
#define R_RV3032_EVI_CONTROL_ET1        0x20
#define R_RV3032_EVI_CONTROL_ET0        0x10
#define R_RV3032_EVI_CONTROL_ESYN       0x01
#define R_RV3032_TLOW_THRESHLD  0x16
#define R_RV3032_THIGH_THRESHLD 0x17
#define R_RV3032_TS_TLOW_COUNT  0x18
#define R_RV3032_TS_TLOW_SEC    0x19
#define R_RV3032_TS_TLOW_MIN    0x1A
#define R_RV3032_TS_TLOW_HOUR   0x1B
#define R_RV3032_TS_TLOW_DATE   0x1C
#define R_RV3032_TS_TLOW_MONTH  0x1D
#define R_RV3032_TS_TLOW_YEAR   0x1E
#define R_RV3032_TS_THIGH_COUNT 0x1F
#define R_RV3032_TS_THIGH_SEC   0x20
#define R_RV3032_TS_THIGH_MIN   0x21
#define R_RV3032_TS_THIGH_HOUR  0x22
#define R_RV3032_TS_THIGH_DATE  0x23
#define R_RV3032_TS_THIGH_MONTH 0x24
#define R_RV3032_TS_THIGH_YEAR  0x25
#define R_RV3032_TS_EVI_COUNT   0x26
#define R_RV3032_TS_EVI_100THS  0x27
#define R_RV3032_TS_EVI_SEC     0x28
#define R_RV3032_TS_EVI_MIN     0x29
#define R_RV3032_TS_EVI_HOUR    0x2A
#define R_RV3032_TS_EVI_DATE    0x2B
#define R_RV3032_TS_EVI_MONTH   0x2C
#define R_RV3032_TS_EVI_YEAR    0x2D
#define R_RV3032_PASSWORD_0     0x39
#define R_RV3032_PASSWORD_1     0x3A
#define R_RV3032_PASSWORD_2     0x3B
#define R_RV3032_PASSWORD_3     0x3C
#define R_RV3032_EE_ADDRESS     0x3D
#define R_RV3032_EE_DATA        0x3E
#define R_RV3032_EE_COMMAND     0x3F
#define R_RV3032_EE_COMMAND_UPDATE  0x11
#define R_RV3032_EE_COMMAND_REFRESH 0x12
#define R_RV3032_EE_COMMAND_WRITE   0x21
#define R_RV3032_EE_COMMAND_READ    0x22
#define R_RV3032_USER_RAM_START 0x40
#define R_RV3032_USER_RAM_END   0x4F

#define E_RV3032_PMU                0xC0
#define E_RV3032_PMU_NCLKE          0x40
#define E_RV3032_PMU_BSM_1          0x20
#define E_RV3032_PMU_BSM_0          0x10
#define E_RV3032_PMU_TCR_1          0x08
#define E_RV3032_PMU_TCR_0          0x04
#define E_RV3032_PMU_TCM_1          0x02
#define E_RV3032_PMU_TCM_0          0x01
#define E_RV3032_OFFSET             0xC1
#define E_RV3032_CLKOUT1            0xC2
#define E_RV3032_CLKOUT2            0xC3
#define E_RV3032_CLKOUT2_OS         0x80
#define E_RV3032_CLKOUT2_FD_1       0x40
#define E_RV3032_CLKOUT2_FD_0       0x20
#define E_RV3032_TREFERENCE0    0xC4
#define E_RV3032_TREFERENCE1    0xC5
#define E_RV3032_PASSWORD0      0xC6
#define E_RV3032_PASSWORD1      0xC7
#define E_RV3032_PASSWORD2      0xC8
#define E_RV3032_PASSWORD3      0xC9
#define E_RV3032_EEPWE          0xCA
#define E_RV3032_USER_EEPROM_START  0xCB
#define E_RV3032_USER_EEPROM_END    0xEA

#define RV3032_AGE_MIN              (-32)
#define RV3032_AGE_MAX              ( 31)
#define RV3032_AGE_RANGE            (RV3032_AGE_MAX - RV3032_AGE_MIN + 1)

#define E_RV3032_EEPROM_ESYN_SUPPORTED  (E_RV3032_USER_EEPROM_START + 0)  // 1 byte
#define E_RV3032_EEPROM_AGE_BEST        (E_RV3032_USER_EEPROM_START + 1)  // 1 byte
#define E_RV3032_EEPROM_CLKOUT_OFFSET   (E_RV3032_USER_EEPROM_START + 2)  // 2 bytes
#define E_RV3032_EEEROM_AGE_ROOM        (E_RV3032_USER_EEPROM_START + 4)  // 1 byte
#define E_RV3032_EEEROM_TEMP_COEF       (E_RV3032_USER_EEPROM_START + 5)  // 3 bytes

#define E_RV3032_EEPROM_VERSION         (E_RV3032_USER_EEPROM_END   - 1)  // 1 byte


typedef struct {
	struct tm tm;
	uint8_t seconds100th;
} rv3032_time_t;


enum RV3032_CLKOUT{
    RV3032_DISABLE_CLKOUT   = 0xFF,
    RV3032_XTAL_32KHZ_OUT   = 0,
    RV3032_XTAL_1024HZ_OUT  = E_RV3032_CLKOUT2_FD_0,
    RV3032_XTAL_64HZ_OUT    = E_RV3032_CLKOUT2_FD_1,
    RV3032_XTAL_1HZ_OUT     = E_RV3032_CLKOUT2_FD_0 | E_RV3032_CLKOUT2_FD_1,
    RV3032_HF_MODE          = E_RV3032_CLKOUT2_OS
};


/**
 * DIRECT:
 *   - If VDD < VBACKUP, switchover occurs from VDD to VBACKUP.
 * LEVEL:
 *   - If VBACKUP > VTH:LSM (typical 2.0 V) AND VDD < VTH:LSM (typical 2.0 V),
 *     switchover occurs from VDD to VBACKUP.
 *   - If VDD < VTH:LSM (typical 2.0 V), the module is automatically in DSM Mode.
 */
enum RV3032_BSM{
    RV3032_BSM_DISABLED  = 0,
    RV3032_BSM_DIRECT    = E_RV3032_PMU_BSM_0,
    RV3032_BSM_LEVEL     = E_RV3032_PMU_BSM_1
};

enum RV3032_TCR{
    RV3032_TCR_0_6kOhm   = 0,
    RV3032_TCR_2kOhm     = E_RV3032_PMU_TCR_0,
    RV3032_TCR_7kOhm     = E_RV3032_PMU_TCR_1,
    RV3032_TCR_12kOhm    = E_RV3032_PMU_TCR_0 | E_RV3032_PMU_TCR_1
};

enum RV3032_TCM{
    RV3032_TCM_OFF  = 0,                                     /**< Disabled                               */
    RV3032_TCM_175  = E_RV3032_PMU_TCM_0,                    /**< VDD (BSM=DIRECT) or 1.75V (BSM=LEVEL)  */
    RV3032_TCM_300  = E_RV3032_PMU_TCM_1,                    /**< 3.0V (BSM=LEVEL)                       */
    RV3032_TCM_440  = E_RV3032_PMU_TCM_0 | E_RV3032_PMU_TCM_1/**< 4.4V (BSM=LEVEL)                       */
};


#ifdef ARDUINO_ARCH_ESP32
void rv3032_init(void *i2cPort);
#else  // ESP-IDF
esp_err_t rv3032_init(int port, int sda_gpio, int scl_gpio);
#endif

void rv3032_postInit();  // Users should not call this function

void rv3032_deinit();

esp_err_t rv3032_ping();

esp_err_t rv3032_writeReg(uint8_t reg_addr, uint8_t val);
esp_err_t rv3032_writeRegBuff(uint8_t reg_addr, const uint8_t* data, size_t len);

esp_err_t rv3032_readReg(uint8_t reg_addr, uint8_t *val);
esp_err_t rv3032_readRegBuff(uint8_t reg_addr, uint8_t* data, size_t len);

bool rv3032_isValid();
esp_err_t rv3032_waitBusy();
esp_err_t rv3032_refreshEEPROM();  // Copy EEPROM -> RAM

esp_err_t rv3032_setTime(const struct tm *tm);
esp_err_t rv3032_setTimePrecise(const struct tm *tm);
esp_err_t rv3032_getTime(struct tm *tm);
esp_err_t rv3032_getTimePrecise(rv3032_time_t* timePrecise);

esp_err_t rv3032_getTemperature(float *temperature);

esp_err_t rv3032_getClockOut(enum RV3032_CLKOUT *clockOut);
esp_err_t rv3032_setClockOut(enum RV3032_CLKOUT clockOut);
esp_err_t rv3032_enableClockOut(bool enable);
const char* rv3032_ClockOut2Str(enum RV3032_CLKOUT clockOut);
esp_err_t rv3032_setClockOutHF(uint16_t hfClock_steps);

esp_err_t rv3032_updateEEPROM(uint8_t reg);

esp_err_t rv3032_setAgeOffset(int8_t age);
esp_err_t rv3032_getAgeOffset(int8_t *age);

esp_err_t rv3032_setBSM(enum RV3032_BSM bsm);

esp_err_t rv3032_setTrickleCharge(enum RV3032_TCR tcr, enum RV3032_TCM tcm);

esp_err_t rv3032_enableEVI(bool enable);

uint8_t rv3032_readUserEEPROM(uint8_t offset);
esp_err_t rv3032_writeUserEEPROM(uint8_t offset, uint8_t val);
bool rv3032_getEEPROMESYNSupported();
esp_err_t rv3032_writeEEPROMESYNSupported(bool val);
esp_err_t rv3032_writeEEPROMAgeBest(int8_t ageBest);
int8_t rv3032_getEEPROMAgeBest();
double rv3032_getEEPROMAgeRoom();
esp_err_t rv3032_writeEEPROMAgeRoom(double ageRoom);
esp_err_t rv3032_writeEEPROMClkoutOffset(int32_t offset);
int32_t rv3032_getEEPROMClkoutOffset();
esp_err_t rv3032_writeEEPROMTempCoef(const double tempCoef[3]);
esp_err_t rv3032_getEEPROMTempCoef(double tempCoef[3]);
esp_err_t rv3032_writeEEPROMVersion(uint8_t version);
uint8_t rv3032_getEEPROMVersion();

#ifdef __cplusplus
}
#endif

#endif  /* _RTC_RV3032_H_ */
