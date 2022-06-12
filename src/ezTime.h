/* Extensive API documentation is at https://github.com/ropg/ezTime */

#ifndef _EZTIME_H_
#ifdef __cplusplus
#define _EZTIME_H_

//Sets the language for the names of Months and Days. See the src/lang directory for supported languages
#define EZTIME_LANGUAGE EN

// Compiles in NTP updating, timezoned fetching and caching 
#define EZTIME_NETWORK_ENABLE

// Arduino Ethernet shields
// #define EZTIME_ETHERNET

// Arduino board with ESP8266 shield
// #define EZTIME_WIFIESP

// Uncomment one of the below to only put only messages up to a certain level in the compiled code
// (You still need to turn them on with setDebug(someLevel) to see them)
// #define EZTIME_MAX_DEBUGLEVEL_NONE
// #define EZTIME_MAX_DEBUGLEVEL_ERROR
// #define EZTIME_MAX_DEBUGLEVEL_INFO

// Cache mechanism, either EEPROM or NVS, not both. (See README)
//#define EZTIME_CACHE_EEPROM
#define EZTIME_CACHE_NVS

// Uncomment if you want to access ezTime functions only after "ezt."
// (to avoid naming conflicts in bigger projects, e.g.) 
// #define EZTIME_EZT_NAMESPACE

// DS3231xx RTC connected
//#define EZTIME_DS3231_ENABLE

// RV-3028-C7 RTC connected
//#define EZTIME_RV3028_ENABLE

// Warranty void if edited below this point...



#if !defined(__time_t_defined) // avoid conflict with newlib or other posix libc
	typedef unsigned long time_t;
#endif

#include <inttypes.h>
#ifndef __AVR__
	#include <sys/types.h> // for __time_t_defined, but avr libc lacks sys/types.h
#endif

#if !defined(__time_t_defined) // avoid conflict with newlib or other posix libc
typedef unsigned long time_t;
#endif


extern "C++" {

////////// Error handing

typedef enum {
	NO_ERROR,
	LAST_ERROR,			// Pseudo-error: replaced by last error
	NO_NETWORK, 
	TIMEOUT,
	CONNECT_FAILED,
	DATA_NOT_FOUND,
	LOCKED_TO_UTC,
	NO_CACHE_SET,
	CACHE_TOO_SMALL,
	TOO_MANY_EVENTS,
	INVALID_DATA,
	SERVER_ERROR
} ezError_t;

typedef enum {
	NONE, 
	ERROR,
	INFO,
	DEBUG
} ezDebugLevel_t;

typedef enum {
	LOCAL_TIME,
	UTC_TIME
} ezLocalOrUTC_t;

// Defines that can make your code more readable. For example, if you are looking for the first
// Thursday in a year, you could write:  time.makeOrdinalTime(0, 0, 0, JANUARY, FIRST, THURSDAY, year)

#define SUNDAY			1
#define MONDAY			2
#define TUESDAY			3
#define WEDNESDAY		4
#define THURSDAY		5
#define FRIDAY			6
#define SATURDAY		7

#define JANUARY			1
#define FEBRUARY		2
#define MARCH			3
#define APRIL			4
#define MAY				5
#define JUNE			6
#define JULY			7
#define AUGUST			8
#define SEPTEMBER		9
#define OCTOBER			10
#define NOVEMBER		11
#define DECEMBER		12

#define	FIRST			1
#define	SECOND			2
#define	THIRD			3
#define FOURTH			4
#define LAST			5

////////////////////////


#define LEAP_YEAR(Y)	( ((1970+Y)>0) && !((1970+Y)%4) && ( ((1970+Y)%100) || !((1970+Y)%400) ) )
#define SECS_PER_DAY	(86400UL)

#define MICROS_IN_SEC_MIN 999969
#define MICROS_IN_SEC_MAX 1000031

//convenience macros to convert to and from tm years 
#define  tmYearToCalendar(Y) ((Y) + 1970)  // full four digit year 
#define  CalendarYrToTm(Y)   ((Y) - 1970)
#define  tmYearToY2k(Y)      ((Y) - 30)    // offset is from 2000
#define  y2kYearToTm(Y)      ((Y) + 30)

typedef struct  { 
	uint8_t Second; 
	uint8_t Minute; 
	uint8_t Hour; 
	uint8_t Wday;   // day of week, sunday is day 1
	uint8_t Day;
	uint8_t Month; 
	uint8_t Year;   // offset from 1970; 
} tmElements_t;

typedef enum { 
	timeNotSet,
	timeNeedsSync,
	timeSet
} timeStatus_t;

typedef struct {
	time_t time;
	void (*function)();
} ezEvent_t;

#define MAX_EVENTS				8

#define TIME_NOW				(int32_t)0x7FFFFFFF			// Two special-meaning time_t values ...
#define LAST_READ				(int32_t)0x7FFFFFFE			// (So yes, ezTime might malfunction two seconds before everything else...)

#define NTP_PACKET_SIZE			48
#define NTP_LOCAL_PORT			4242
#define NTP_SERVER				"pool.ntp.org"
#define NTP_TIMEOUT				1500000			// microseconds
#define NTP_INTERVAL			1801				// default update interval in seconds
#define NTP_RETRY				20				// Retry after this many seconds on failed NTP
#define NTP_STALE_AFTER			3602				// If update due for this many seconds, set timeStatus to timeNeedsSync

#define TIMEZONED_REMOTE_HOST	"timezoned.rop.nl"
#define TIMEZONED_REMOTE_PORT	2342
#define TIMEZONED_LOCAL_PORT	2342
#define TIMEZONED_TIMEOUT		2000000			// microseconds

#define EEPROM_CACHE_LEN		50
#define MAX_CACHE_PAYLOAD		((EEPROM_CACHE_LEN - 3) / 3) * 4 + ( (EEPROM_CACHE_LEN - 3) % 3)	// 2 bytes for len and date, then 4 to 3 (6-bit) compression on rest 
#define MAX_CACHE_AGE_MONTHS	6

// Various date-time formats
#define ATOM 				"Y-m-d\\TH:i:sP"
#define COOKIE				"l, d-M-Y H:i:s T"
#define ISO8601				"Y-m-d\\TH:i:sO"
#define RFC822				"D, d M y H:i:s O"
#define RFC850				COOKIE
#define RFC1036				RFC822
#define RFC1123				RFC822
#define RFC2822				RFC822
#define RFC3339 			ATOM
#define RFC3339_EXT			"Y-m-d\\TH:i:s.vP"
#define RSS					RFC822
#define W3C					ATOM
#define ISO8601_YWD			"X-\\WW-N"
#define DEFAULT_TIMEFORMAT	COOKIE

namespace ezt {
	IRAM_ATTR void syncToPPS(void);
	bool setPPSMicros(const uint32_t us = 1000000);
	uint32_t getPPSMicros();
	void breakTime(const time_t time, tmElements_t &tm);
	time_t compileTime(const String compile_date = __DATE__, const String compile_time = __TIME__);
	String dayShortStr(const uint8_t month);
	String dayStr(const uint8_t month);
	void deleteEvent(const uint8_t event_handle);
	void deleteEvent(void (*function)());
	ezError_t error(const bool reset = false);
	String errorString(const ezError_t err = LAST_ERROR);
	void events();
	time_t makeOrdinalTime(const uint8_t hour, const uint8_t minute, const uint8_t second, uint8_t ordinal, const uint8_t wday, const uint8_t month, uint16_t year);
	time_t makeTime(const uint8_t hour, const uint8_t minute, const uint8_t second, const uint8_t day, const uint8_t month, const uint16_t year);
	time_t makeTime(tmElements_t &tm);
	bool minuteChanged();
	String monthShortStr(const uint8_t month);
	String monthStr(const uint8_t month);
	bool secondChanged();
	void setDebug(const ezDebugLevel_t level);
	void setDebug(const ezDebugLevel_t level, Print &device);
	timeStatus_t timeStatus();
	String urlEncode(const String str);
	String zeropad(const uint32_t number, const uint8_t length);

	#ifdef EZTIME_NETWORK_ENABLE
		bool queryNTP(const String server, time_t &t, uint64_t &measured_at);
		void setInterval(const uint16_t seconds = 0);
		void setServer(const String ntp_server = NTP_SERVER);
		void updateNTP();
		bool waitForSync(const uint16_t timeout = 0);
		time_t lastNtpUpdateTime();
	#endif
}

//
//				T i m e z o n e   c l a s s
//

class Timezone {

	public:
		Timezone(const bool locked_to_UTC = false);
		String dateTime(const String format = DEFAULT_TIMEFORMAT);
		String dateTime(time_t t, const String format = DEFAULT_TIMEFORMAT);
		String dateTime(time_t t, const ezLocalOrUTC_t local_or_utc, const String format = DEFAULT_TIMEFORMAT);
		uint8_t day(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint16_t dayOfYear(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		int16_t getOffset(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		String getPosix();
		String getTimezoneName(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint8_t hour(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint8_t hourFormat12(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		bool isAM(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		bool isDST(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		bool isPM(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);		
		String militaryTZ(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint8_t minute(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint8_t month(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);	
		uint16_t ms(time_t t = TIME_NOW);
		uint32_t us(time_t t = TIME_NOW);
		time_t now();
		uint8_t second(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		void setDefault();
		uint8_t setEvent(void (*function)(), const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr);
		uint8_t setEvent(void (*function)(), time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		bool setPosix(const String posix);
		void setTime(const time_t t, const uint32_t us = 0);
		void setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr);
		time_t tzTime(time_t t = TIME_NOW, ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		time_t tzTime(time_t t, ezLocalOrUTC_t local_or_utc, String &tzname, bool &is_dst, int16_t &offset);		
		uint8_t weekISO(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint8_t weekday(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
		uint16_t year(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);	
		uint16_t yearISO(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	private:
		String _posix, _olson;
		bool _locked_to_UTC;
 		
	#ifdef EZTIME_NETWORK_ENABLE
		public:
			bool setLocation(const String location = "GeoIP");
			String getOlson();
			String getOlsen();
		#ifdef EZTIME_CACHE_EEPROM
			public:
				bool setCache(const int16_t address);
			private:
				int16_t _eeprom_address;
		#endif
		#ifdef EZTIME_CACHE_NVS
			public:
				bool setCache(const String name, const String key);
			private:
				String _nvs_name, _nvs_key;
		#endif
 		#if defined(EZTIME_CACHE_EEPROM) || defined(EZTIME_CACHE_NVS)
 			public:
				void clearCache(const bool delete_section = false);
 			private:
 				bool setCache();
  				bool writeCache(String &str);
 				bool readCache(String &olson, String &posix, uint8_t &months_since_jan_2018);
 				uint8_t _cache_month;
		#endif
	#endif	

};

extern Timezone UTC;
extern Timezone *defaultTZ;

namespace ezt {
	// These bounce through to same-named methods in defaultTZ 
	String dateTime(const String format = DEFAULT_TIMEFORMAT);
	String dateTime(time_t t, const String format = DEFAULT_TIMEFORMAT);
	String dateTime(time_t t, const ezLocalOrUTC_t local_or_utc, const String format = DEFAULT_TIMEFORMAT);
	uint8_t day(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME); 
	uint16_t dayOfYear(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	int16_t getOffset(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	String getTimezoneName(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint8_t hour(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint8_t hourFormat12(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	bool isAM(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	bool isDST(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	bool isPM(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	String militaryTZ(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint8_t minute(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint8_t month(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME); 
	uint16_t ms(time_t t = TIME_NOW);
	time_t now();
	uint8_t second(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint8_t setEvent(void (*function)(), const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr);
	uint8_t setEvent(void (*function)(), time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	void setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t month, const uint16_t yr);
	void setTime(time_t t);
	uint8_t weekISO(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint8_t weekday(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
	uint16_t year(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME); 
	uint16_t yearISO(time_t t = TIME_NOW, const ezLocalOrUTC_t local_or_utc = LOCAL_TIME);
}

#ifndef EZTIME_EZT_NAMESPACE
	using namespace ezt;
#endif


// The following defines all copied from the original Time lib to keep existing code working

/* Useful Constants */
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define DAYS_PER_WEEK (7UL)
#define SECS_PER_WEEK (SECS_PER_DAY * DAYS_PER_WEEK)
#define SECS_PER_YEAR (SECS_PER_WEEK * 52UL)
#define SECS_YR_2000  (946684800UL) // the time at the start of y2k

/* Useful Macros for getting elapsed time */
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)  
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN) 
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
#define dayOfWeek(_time_)  ((( _time_ / SECS_PER_DAY + 4)  % DAYS_PER_WEEK)+1) // 1 = Sunday
#define elapsedDays(_time_) ( _time_ / SECS_PER_DAY)  // this is number of days since Jan 1 1970
#define elapsedSecsToday(_time_)  (_time_ % SECS_PER_DAY)   // the number of seconds since last midnight 

// The following macros are used in calculating alarms and assume the clock is set to a date later than Jan 1 1971
// Always set the correct time before settting alarms
#define previousMidnight(_time_) (( _time_ / SECS_PER_DAY) * SECS_PER_DAY)  // time at the start of the given day
#define nextMidnight(_time_) ( previousMidnight(_time_)  + SECS_PER_DAY )   // time at the end of the given day 
#define elapsedSecsThisWeek(_time_)  (elapsedSecsToday(_time_) +  ((dayOfWeek(_time_)-1) * SECS_PER_DAY) )   // note that week starts on day 1
#define previousSunday(_time_)  (_time_ - elapsedSecsThisWeek(_time_))      // time at the start of the week for the given time
#define nextSunday(_time_) ( previousSunday(_time_)+SECS_PER_WEEK)          // time at the end of the week for the given time

/* Useful Macros for converting elapsed time to a time_t */
#define minutesToTime_t ((M)) ( (M) * SECS_PER_MIN)  
#define hoursToTime_t   ((H)) ( (H) * SECS_PER_HOUR)  
#define daysToTime_t    ((D)) ( (D) * SECS_PER_DAY) // fixed on Jul 22 2011
#define weeksToTime_t   ((W)) ( (W) * SECS_PER_WEEK) 


/*!
 * DS3231 high precision RTC library for Arduino
 * Based on https: https://github.com/Erriez/ErriezDS3231
 * Documentation : https://erriez.github.io/ErriezDS3231
 * MIT License
 * Copyright (c) 2018 Erriez
 */
#if defined (EZTIME_DS3231_ENABLE) || defined (ARDUINO_M5Stack_Core_ESP32) || defined (ARDUINO_D1_MINI32)
	#include <Wire.h>
	#include <stdint.h>

	#define EZTIME_DS3231_ENABLE

	const uint8_t adrDS3231 = 0x68;

	#define DS3231_REG_SECONDS      0x00    // Seconds register
	#define DS3231_REG_MINUTES      0x01    // Minutes register
	#define DS3231_REG_HOURS        0x02    // Hours register
	#define DS3231_REG_DAY_WEEK     0x03    // Day of the week register
	#define DS3231_REG_DAY_MONTH    0x04    // Day of the month register
	#define DS3231_REG_MONTH        0x05    // Month register
	#define DS3231_REG_YEAR         0x06    // Year register

	#define DS3231_REG_ALARM1_SEC   0x07    // Alarm 1 seconds register
	#define DS3231_REG_ALARM1_MIN   0x08    // Alarm 1 minutes register
	#define DS3231_REG_ALARM1_HOUR  0x09    // Alarm 1 hour register
	#define DS3231_REG_ALARM1_DD    0x0A    // Alarm 1 day/date register
	#define DS3231_REG_ALARM2_MIN   0x0B    // Alarm 2 seconds register
	#define DS3231_REG_ALARM2_HOUR  0x0C    // Alarm 2 hour register
	#define DS3231_REG_ALARM2_DD    0x0D    // Alarm 2 day/date register

	#define DS3231_REG_CONTROL      0x0E    // Control register
	#define DS3231_REG_STATUS       0x0F    // Status register
	#define DS3231_REG_AGING_OFFSET 0x10    // Aging offset register
	#define DS3231_REG_TEMP_MSB     0x11    // Temperature MSB register
	#define DS3231_REG_TEMP_LSB     0x12    // Temperature LSB register

	// DS3231 register bit defines
	#define DS3231_MONTH_CENTURY    7       // Century
	#define DS3231_HOUR_12H_24H     6       // 12 or 24 hour mode
	#define DS3231_HOUR_AM_PM       5       // AM/PM

	#define DS3231_CTRL_EOSC        7       // Enable oscillator
	#define DS3231_CTRL_BBSQW       6       // Battery-Backed Square-Wave Enable
	#define DS3231_CTRL_CONV        5       // Start temperature conversion
	#define DS3231_CTRL_RS2         4       // Square wave rate-select 2
	#define DS3231_CTRL_RS1         3       // Square wave rate-select 1
	#define DS3231_CTRL_INTCN       2       // Interrupt control
	#define DS3231_CTRL_A2IE        1       // Alarm 2 interrupt enable
	#define DS3231_CTRL_A1IE        0       // Alarm 1 interrupt enable

	#define DS3231_STAT_OSF         7       // Oscillator Stop Flag
	#define DS3231_STAT_EN32KHZ     3       // Enable 32kHz clock output
	#define DS3231_STAT_BSY         2       // Temperature conversion busy flag
	#define DS3231_STAT_A2F         1       // Alarm 2 status flag
	#define DS3231_STAT_A1F         0       // Alarm 1 status flag

	#define DS3231_A1M1             7       // Alarm 1 bit 7 seconds register
	#define DS3231_A1M2             7       // Alarm 1 bit 7 minutes register
	#define DS3231_A1M3             7       // Alarm 1 bit 7 hours register
	#define DS3231_A1M4             7       // Alarm 1 bit 7 day/date register
	#define DS3231_A2M2             7       // Alarm 2 bit 7 minutes register
	#define DS3231_A2M3             7       // Alarm 2 bit 7 hours register
	#define DS3231_A2M4             7       // Alarm 2 bit 7 day/date register
	#define DS3231_DYDT             6       // Alarm 2 bit 6

	typedef enum {
		Alarm1 = 1,
		Alarm2 = 2
	} AlarmId_t;

	typedef enum {
		A1_ES       = 0x0F, // Alarm once per second
		A1_MS		= 0x0E,	// Alarm when seconds match
		A1_MMSS     = 0x0C, // When minutes and seconds match (once per hour)
		A1_HHMMSS   = 0x08, // When hours, minutes and seconds match (once per day)
		A1_WDHHMMSS = 0x10, // When weekday, hours, minutes and seconds match (once per weekday/date)
		A1_DDHHMMSS = 0x00, // When date, hours, minutes and seconds match (once per weekday/date)
	} Alarm1_t;

	typedef enum {
		A2_EM     = 0x0E,	// Alarm once per minute (00 seconds of every minute)
		A2_MM     = 0x0C,	// Alarm when minutes match
		A2_HHMM   = 0x08,	// Alarm when hours and minutes match
		A2_WDHHMM = 0x10,	// Alarm when weekday, hours, and minutes match
		A2_DDHHMM = 0x00,	// Alarm when date, hours, and minutes match
	} Alarm2_t;

	typedef enum {
		SquareWaveDisable = (1 << DS3231_CTRL_INTCN),                           // SQW disable
		SquareWave1Hz     = ((0 << DS3231_CTRL_RS2) | (0 << DS3231_CTRL_RS1)),	// SQW 1Hz
		SquareWave1024Hz  = ((0 << DS3231_CTRL_RS2) | (1 << DS3231_CTRL_RS1)),	// SQW 1024Hz
		SquareWave4096Hz  = ((1 << DS3231_CTRL_RS2) | (0 << DS3231_CTRL_RS1)),	// SQW 4096Hz
		SquareWave8192Hz  = ((1 << DS3231_CTRL_RS2) | (1 << DS3231_CTRL_RS1)),	// SQW 8192Hz
	} SquareWave_t;


	class DS3231 {

		public:
			static bool begin(TwoWire &wirePort = Wire);
			static void enableOsc(bool enable = true);
			static timeStatus_t timeStatus(bool clearOSF = false);
		
			static void setTime(time_t t);
			static void setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t month, const uint16_t yr);
			static void setTime(tmElements_t &tm);
			static time_t now();
			static time_t getSetTime(uint64_t &micros);	//Returns the time when RTC time was set

			static void enableClockPin(bool enable = true);
			static void setSquareWave(SquareWave_t squareWave);

			static bool setAgingOffset(int8_t val);
			static int8_t getAgingOffset();
			static bool startTemperatureConversion();
			static int32_t getTemperature();

			static void setAlarm1(Alarm1_t alarmType, uint8_t dayDate, const uint8_t hr, const uint8_t min, const uint8_t sec);
			static void setAlarm2(Alarm2_t alarmType, uint8_t dayDate, const uint8_t hr, const uint8_t min);
			//static void enableInterrupt(AlarmId_t alarmId, bool enable);
			static void enableInterrupt(AlarmId_t intId);
			static void disableInterrupt(AlarmId_t intId);
			static bool getAlarmFlag(AlarmId_t alarmId);
			static void clearAlarmFlag(AlarmId_t alarmId);
		private:
			static uint8_t bcdToDec(uint8_t bcd);
			static uint8_t decToBcd(uint8_t dec);
			static uint8_t ds3231(uint8_t reg);
			static void ds3231(uint8_t reg, uint8_t value);
			static void ds3231rd(uint8_t reg, uint8_t len, void *data);
			static void ds3231wr(uint8_t reg, uint8_t len, void *data);
	};

	extern DS3231 RTC;

//#endif	// EZTIME_DS3231_ENABLE 

#elif defined  (EZTIME_RV3028_ENABLE) || defined (ARDUINO_Piranha)
	#include <Wire.h>

	#define EZTIME_RV3028_ENABLE

	const uint8_t adrRV3028 = 0x52;

	#define RV3028_REG_SECONDS      	0x00
	#define RV3028_REG_MINUTES      	0x01
	#define RV3028_REG_HOURS        	0x02
	#define RV3028_REG_WEEKDAY			0x03
	#define RV3028_REG_DATE         	0x04
	#define RV3028_REG_MONTHS        	0x05
	#define RV3028_REG_YEARS        	0x06
	#define RV3028_REG_MINUTES_ALM     	0x07
	#define RV3028_REG_HOURS_ALM       	0x08
	#define RV3028_REG_DATE_ALM        	0x09
	#define RV3028_REG_TIMERVAL_0		0x0A
	#define RV3028_REG_TIMERVAL_1		0x0B
	#define RV3028_REG_TIMERSTAT_0		0x0C
	#define RV3028_REG_TIMERSTAT_1		0x0D
	#define RV3028_REG_STATUS			0x0E
	#define RV3028_REG_CTRL1			0x0F
	#define RV3028_REG_CTRL2			0x10
	#define RV3028_REG_GPBITS			0x11
	#define RV3028_REG_INT_MASK			0x12
	#define RV3028_REG_EVENTCTRL		0x13
	#define RV3028_REG_COUNT_TS			0x14
	#define RV3028_REG_SECONDS_TS		0x15
	#define RV3028_REG_MINUTES_TS		0x16
	#define RV3028_REG_HOURS_TS			0x17
	#define RV3028_REG_DATE_TS			0x18
	#define RV3028_REG_MONTH_TS			0x19
	#define RV3028_REG_YEAR_TS			0x1A
	#define RV3028_REG_UNIX_TIME0		0x1B
	#define RV3028_REG_UNIX_TIME1		0x1C
	#define RV3028_REG_UNIX_TIME2		0x1D
	#define RV3028_REG_UNIX_TIME3		0x1E
	#define RV3028_REG_USER_RAM1		0x1F
	#define RV3028_REG_USER_RAM2		0x20
	#define RV3028_REG_PASSWORD0		0x21
	#define RV3028_REG_PASSWORD1		0x22
	#define RV3028_REG_PASSWORD2		0x23
	#define RV3028_REG_PASSWORD3		0x24
	#define RV3028_REG_EEPROM_ADDR		0x25
	#define RV3028_REG_EEPROM_DATA		0x26
	#define RV3028_REG_EEPROM_CMD		0x27
	#define RV3028_REG_ID				0x28
	#define RV3028_REG_EEPROM_CLKOUT	0x35
	#define RV3028_REG_EEOffset_8_1		0x36	//bits 8 to 1 of EEOffset. Bit 0 is bit 7 of register 0x37 
	#define RV3028_REG_EEPROM_BACKUP	0x37

	//BITS IN IMPORTANT REGISTERS
	//Bits in Status Register
	#define STATUS_EEBUSY	7
	#define STATUS_CLKF		6
	#define STATUS_BSF		5
	#define STATUS_UF		4
	#define STATUS_TF		3
	#define STATUS_AF		2
	#define STATUS_EVF		1
	#define STATUS_PORF		0

	//Bits in Control1 Register
	#define CTRL1_TRPT		7
	#define CTRL1_WADA		5//Bit 6 not implemented
	#define CTRL1_USEL		4
	#define CTRL1_EERD		3
	#define CTRL1_TE		2
	#define	CTRL1_TD1		1
	#define CTRL1_TD0		0

	//Bits in Control2 Register
	#define CTRL2_TSE		7
	#define CTRL2_CLKIE		6
	#define CTRL2_UIE		5
	#define CTRL2_TIE		4
	#define CTRL2_AIE		3
	#define CTRL2_EIE		2
	#define CTRL2_12_24		1
	#define CTRL2_RESET		0

	//Bits in Hours register
	#define HOURS_AM_PM			5

	//Bits in Alarm registers
	#define MINUTESALM_AE_M		7
	#define HOURSALM_AE_H		7
	#define DATE_AE_WD			7

	//Commands for EEPROM Command Register (0x27)
	#define EEPROMCMD_First				0x00
	#define EEPROMCMD_Update			0x11
	#define EEPROMCMD_Refresh			0x12
	#define EEPROMCMD_WriteSingle		0x21
	#define EEPROMCMD_ReadSingle		0x22

	//Bits in EEPROM Backup Register
	#define EEPROMBackup_TCE_BIT		5			//Trickle Charge Enable Bit
	#define EEPROMBackup_FEDE_BIT		4			//Fast Edge Detection Enable Bit (for Backup Switchover Mode)
	#define EEPROMBackup_BSM_SHIFT		2			//Backup Switchover Mode shift
	#define EEPROMBackup_TCR_SHIFT		0			//Trickle Charge Resistor shift

	#define EEPROMBackup_BSM_CLEAR		0b11110011	//Backup Switchover Mode clear
	#define EEPROMBackup_TCR_CLEAR		0b11111100	//Trickle Charge Resistor clear
	// #define	TCR_3K						0b00		//Trickle Charge Resistor 3kOhm
	// #define	TCR_5K						0b01		//Trickle Charge Resistor 5kOhm
	// #define	TCR_9K						0b10		//Trickle Charge Resistor 9kOhm
	// #define	TCR_15K						0b11		//Trickle Charge Resistor 15kOhm

	// Clock output register (0x35)
	#define EEPROMClkout_CLKOE_BIT		7			//1 = CLKOUT pin is enabled. – Default value on delivery 
	#define EEPROMClkout_CLKSY_BIT		6
	// Bits 5 and 4 not implemented
	#define EEPROMClkout_PORIE			3			//0 = No interrupt, or canceled, signal on INT pin at POR. – Default value on delivery
													//1 = An interrupt signal on INT pin at POR. Retained until the PORF flag is cleared to 0 (no automatic cancellation). 
	#define EEPROMClkout_FREQ_SHIFT		0			//frequency shift
	#define FD_CLKOUT_32k				0b000		//32.768 kHz – Default value on delivery 
	#define FD_CLKOUT_8192				0b001 		//8192 Hz 
	#define FD_CLKOUT_1024				0b010		//1024 Hz
	#define FD_CLKOUT_64				0b011		//64 Hz 
	#define FD_CLKOUT_32				0b100		//32 Hz
	#define FD_CLKOUT_1					0b101		//1 Hz 
	#define FD_CLKOUT_TIMER				0b110		//Predefined periodic countdown timer interrupt 
	#define FD_CLKOUT_LOW				0b111		//CLKOUT = LOW 


	#define IMT_MASK_CEIE				3			//Clock output when Event Interrupt bit. 
	#define IMT_MASK_CAIE				2			//Clock output when Alarm Interrupt bit.
	#define IMT_MASK_CTIE				1			//Clock output when Periodic Countdown Timer Interrupt bit.
	#define IMT_MASK_CUIE				0			//Clock output when Periodic Time Update Interrupt bit.

	typedef enum {
		Alarm1 = 1,
		Alarm2 = 2,
		Timer  = 3,
		TimeUpdate = 4,
	} InterruptId_t;

	typedef enum {
		A1_DDHHMMSS = 0, //When date, hours, minutes and seconds match (once per weekday/date)
		A1_WDHHMMSS = 1, //When weekday, hours, minutes and seconds match (once per weekday/date)
		A1_HHMMSS   = 4, //When hours, minutes and seconds match (once per day)
		A1_MMSS     = 6, //When minutes and seconds match (once per hour)
		A1_OFF    = 7, //Alarm disabled - Default value
		A1_ES     = 9, //Alarm once per second
	} Alarm1_t;

// Set the alarm mode in the following way:
// ALARM_DDHHMM 0: When minutes, hours and weekday/date match (once per weekday/date)
// ALARM_DDHH 1: When hours and weekday/date match (once per weekday/date)
// ALARM_DDMM 2: When minutes and weekday/date match (once per hour per weekday/date)
// ALARM_DD 3: When weekday/date match (once per weekday/date)
// ALARM_HHMM 4: When hours and minutes match (once per day)
// ALARM_HH  5: When hours match (once per day)
// ALARM_MM 6: When minutes match (once per hour)
// ALARM_OFF 7: All disabled � Default value	
	typedef enum {
		A2_DDHHMM = 0, //When date, hours and minutes match (once per date)
		A2_DDHH   = 1, //When date and hours match (once per month @hour)
		A2_DDMM   = 2, //When date and minutes match (once per month @minute)
		A2_DD     = 3, //When date match (once per month)
		A2_HHMM   = 4, //When hours and minutes match (once per day)
		A2_HH     = 5, //When hours match (once per day)
		A2_MM     = 6, //When minutes match (once per hour)
		A2_OFF    = 7, //Alarm disabled - Default value
		A2_EM     = 8, //Alarm every minute
		A2_ES     = 9, //Alarm every second
		A2_WDHHMM = 10, //When weekday, hours and minutes match (once per date)
		A2_WDHH   = 11, //When weekday and hours match (once per month @hour)
		A2_WDMM   = 12, //When weekday and minutes match (once per month @minute)
		A2_WD     = 13, //When weekday match (once per month)
	} Alarm2_t;

	typedef enum {
		EverySecond = 0,
		EveryMinute = 1,
	} TimeUpdate_t;

	typedef enum {
		SquareWaveDisable = 7,
		PeriodicCountDown = 6,
		SquareWave1Hz     = 5,
		SquareWave32Hz    = 4,
		SquareWave64Hz    = 3,
		SquareWave1024Hz  = 2,
		SquareWave8192Hz  = 1,
		SquareWave32768Hz = 0,
	} SquareWave_t;

	typedef enum {
		TCR_3K  = 0,
		TCR_5K  = 1,
		TCR_9K  = 2,
		TCR_15K = 3,
		TC_OFF  = 4,
	} TrickleCharge_t;

	typedef enum {
		BS_OFF     = 0,
		BS_DIRECT  = 1,
		BS_STANDBY = 2,
		BS_LEVEL   = 3,
	} SwitchOver_t;

	class RV3028 {

		public:
			static bool begin(TwoWire &wirePort = Wire);
			static timeStatus_t timeStatus(bool clearPORF = false);
			static void setTime(time_t t, bool setEpoch = false);
			static void setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t month, const uint16_t yr, bool setLocal = true);
			static void setTime(tmElements_t &tm, bool setLocal = true);
			static time_t now(bool getEpoch = false);
			static time_t getSetTime(uint64_t &micros);	//Returns the time when RTC time was set
			static void setUnix(time_t t);	//Set the UNIX Time (Real Time and UNIX Time are INDEPENDENT!)
			static time_t nowUnix();

			//DS3231 compatible alarm types
			static void setAlarm1(Alarm1_t alarmType, uint8_t dayDate, const uint8_t hour, const uint8_t min, const uint8_t sec, const bool enableClockOut = false);
			static void setAlarm2(Alarm2_t alarmType, uint8_t dayDate, const uint8_t hour, const uint8_t min, const bool enableClockOut = false);

			static bool setAlarm(time_t t);
			static bool setAlarm(tmElements_t &tm);
			static bool isMonthYearMatch();
			static void setAlarm(uint8_t mode, uint8_t dayDate, const uint8_t hour, const uint8_t min, bool dayNotDate = false, bool enableClockOut = false);
			static void setUpdate(TimeUpdate_t update = EverySecond, bool enableClockOut = false);
			static void setTimer(uint16_t timerValue, uint16_t timerFrequency = 1, bool timerRepeat = false, bool startTimer = true, bool setInterrupt = true, bool enableClockOutput = false);
			static void enableTimer();
			static void disableTimer();
			
			static void enableInterrupt(InterruptId_t intId);
			static void disableInterrupt(InterruptId_t intId);
			static bool readInterruptFlag(InterruptId_t intId);
			static void clearInterruptFlag(InterruptId_t intId);

			static void setSquareWave(SquareWave_t squareWave);
			static void enableClockOut(bool enable = true);
			static bool readClockFlag();
			static void clearClockFlag();

			static void setTrickleCharge(TrickleCharge_t trickCharge = TCR_15K); //Trickle Charge Resistor default 15k
			static bool setBackupSwitchover(SwitchOver_t switchOver = BS_LEVEL);

			static uint8_t reset();  //Returns ID register
			static uint8_t status(); //Returns the status byte
			static void clearInterrupts();

		// 	static bool setAgingOffset(int8_t val);
		// 	static int8_t getAgingOffset();

		private:
			static uint8_t bcdToDec(uint8_t bcd);
			static uint8_t decToBcd(uint8_t dec);
			static uint8_t rv3028(uint8_t reg);
			static bool rv3028(uint8_t reg, uint8_t value);
			static void rv3028rd(uint8_t reg, uint8_t len, void *data);
			static void rv3028wr(uint8_t reg, uint8_t len, void *data);

			static void setBit(uint8_t reg, uint8_t bitNum);
			static void clearBit(uint8_t reg, uint8_t bitNum);
			static bool readBit(uint8_t reg, uint8_t bitNum);

			static uint8_t readConfigEEPROM_RAMmirror(uint8_t eepromAddr);
			static bool writeConfigEEPROM_RAMmirror(uint8_t eepromAddr, uint8_t value);
			static bool waitforEEPROM();
	};

	extern RV3028 RTC;

#endif	// EZTIME_RV3028_ENABLE

} // extern "C++"
#endif // __cplusplus
#endif //_EZTIME_H_
