#include <Arduino.h>
#include <sys/param.h>
#include <ezTime.h>

#ifdef EZTIME_NETWORK_ENABLE
	#ifdef EZTIME_CACHE_NVS
		#include <Preferences.h>		// For timezone lookup cache
	#endif
	#ifdef EZTIME_CACHE_EEPROM
		#include <EEPROM.h>
	#endif	
	#if defined(ESP8266)
		#include <ESP8266WiFi.h>
		#include <WiFiUdp.h>
	#elif defined(ARDUINO_SAMD_MKR1000)
		#include <SPI.h>
		#include <WiFi101.h>
		#include <WiFiUdp.h>
	#elif defined(EZTIME_ETHERNET)
		#include <SPI.h>
		#include <Ethernet.h>
		#include <EthernetUdp.h>
	#elif defined(EZTIME_WIFIESP)
		#include <WifiEsp.h>
		#include <WifiEspUdp.h>
	#else
		#include <WiFi.h>
		#include <WiFiUdp.h>
	#endif
#endif

#if defined(EZTIME_MAX_DEBUGLEVEL_NONE)
	#define	err(args...) 		""
	#define	errln(args...) 		""
	#define	info(args...) 		""
	#define	infoln(args...) 	""
	#define	debug(args...) 		""
	#define	debugln(args...) 	""
#elif defined(EZTIME_MAX_DEBUGLEVEL_ERROR)
	#define	err(args...) 		if (_debug_level >= ERROR) _debug_device->print(args)
	#define	errln(args...) 		if (_debug_level >= ERROR) _debug_device->println(args)
	#define	info(args...) 		""
	#define	infoln(args...) 	""
	#define	debug(args...) 		""
	#define	debugln(args...) 	""
#elif defined(EZTIME_MAX_DEBUGLEVEL_INFO)
	#define	err(args...) 		if (_debug_level >= ERROR) _debug_device->print(args)
	#define	errln(args...) 		if (_debug_level >= ERROR) _debug_device->println(args)
	#define	info(args...) 		if (_debug_level >= INFO) _debug_device->print(args)
	#define	infoln(args...) 	if (_debug_level >= INFO) _debug_device->println(args)
	#define	debug(args...) 		""
	#define	debugln(args...) 	""
#else		// nothing specified compiles everything in.
	#define	err(args...) 		if (_debug_level >= ERROR) _debug_device->print(args)
	#define	errln(args...) 		if (_debug_level >= ERROR) _debug_device->println(args)
	#define	info(args...) 		if (_debug_level >= INFO) _debug_device->print(args)
	#define	infoln(args...) 	if (_debug_level >= INFO) _debug_device->println(args)
	#define	debug(args...) 		if (_debug_level >= DEBUG) _debug_device->print(args)
	#define	debugln(args...) 	if (_debug_level >= DEBUG) _debug_device->println(args)
#endif


const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0

// The private things go in an anonymous namespace
namespace {

	ezError_t _last_error = NO_ERROR;
	String _server_error = "";
	ezDebugLevel_t _debug_level = NONE;
	Print *_debug_device = (Print *)&Serial;
	ezEvent_t _events[MAX_EVENTS];
	time_t _last_sync_time = 0;
	time_t _last_read_t = 0;
	uint64_t _last_sync_micros = 0;
	uint32_t _last_read_us;
	uint32_t _micros_in_sec = 1000000;
	timeStatus_t _time_status;
	bool _initialised = false;
	#ifdef EZTIME_NETWORK_ENABLE
		uint16_t _ntp_interval = NTP_INTERVAL;
		String _ntp_server = NTP_SERVER;
	#endif
	#if defined (EZTIME_DS3231_ENABLE) || defined (EZTIME_RV3028_ENABLE) || defined (EZTIME_BM8563_ENABLE)
		#define HAS_RTC
		TwoWire *_i2cPort;
		timeStatus_t _rtc_status;
		uint8_t _alarm_mode;
		time_t _rtc_set_time;
		uint64_t _rtc_set_micros;
		uint8_t data[7];
	#endif

	void triggerError(const ezError_t err) {
		_last_error = err;
		if (_last_error) {
			err(F("ERROR: "));
			errln(ezt::errorString(err));
		}
	}

	String debugLevelString(const ezDebugLevel_t level) {
		switch (level) {
			case NONE: return 	F("NONE");
			case ERROR: return 	F("ERROR");
			case INFO: return 	F("INFO");
			default: return 	F("DEBUG");
		}
	}

	time_t nowUTC(uint32_t& us, const bool update_last_read = true) {
		uint64_t us_passed = micros() - _last_sync_micros;
		time_t t = _last_sync_time + us_passed / _micros_in_sec; //Not always 1M
		us = (uint32_t)(us_passed % _micros_in_sec);
		if (update_last_read) {
			_last_read_t = t;
			_last_read_us = us;
		}
		return t;
	}

	time_t nowUTC(const bool update_last_read = true) {
		uint32_t us;
		return nowUTC(us, update_last_read);
	}

}



namespace ezt {
	
	IRAM_ATTR void syncToPPS() {
		_last_sync_time++;
		_last_sync_micros = 0;
		_last_read_t = _last_sync_time + micros() / _micros_in_sec;
		_last_read_us = micros() % _micros_in_sec;
	}

	bool setPPSMicros(const uint32_t us /* = 1000000 */){
		if((MICROS_IN_SEC_MIN > us) || (us > MICROS_IN_SEC_MAX)) return false;
		_micros_in_sec = us;
		return true;
	}

	uint32_t getPPSMicros(){ return _micros_in_sec; }

	////////// Error handing

	String errorString(const ezError_t err /* = LAST_ERROR */) {
		switch (err) {
			case NO_ERROR: return				F("OK");
			case LAST_ERROR: return 			errorString(_last_error);
			case NO_NETWORK: return				F("No network");
			case TIMEOUT: return 				F("Timeout");
			case CONNECT_FAILED: return 		F("Connect Failed");
			case DATA_NOT_FOUND: return			F("Data not found");
			case LOCKED_TO_UTC: return			F("Locked to UTC");
			case NO_CACHE_SET: return			F("No cache set");
			case CACHE_TOO_SMALL: return		F("Cache too small");
			case TOO_MANY_EVENTS: return		F("Too many events");
			case INVALID_DATA: return			F("Invalid data received from NTP server");
			case SERVER_ERROR: return			_server_error; 
			default: return						F("Unkown error");
		}
	}

	ezError_t error(const bool reset /* = false */) {
		ezError_t tmp = _last_error;
		if (reset) _last_error = NO_ERROR;
		return tmp;
	}

	void setDebug(const ezDebugLevel_t level) {
		setDebug(level, *_debug_device);
	}

	void setDebug(const ezDebugLevel_t level, Print &device) { 
		_debug_level = level;
		_debug_device = &device;
		info(F("\r\nezTime debug level set to "));
		infoln(debugLevelString(level));
	}


	// The include below includes the dayStr, dayShortStr, monthStr and monthShortStr from the appropriate language file
	// in the /src/lang subdirectory.


	#ifdef EZTIME_LANGUAGE
		#define XSTR(x) #x
		#define STR(x) XSTR(x)
		#include STR(lang/EZTIME_LANGUAGE)
	#else
		#include "lang/EN"
	#endif

	//

	timeStatus_t timeStatus() { return _time_status; }

	void events() {
		if (!_initialised) {
			for (uint8_t n = 0; n < MAX_EVENTS; n++) _events[n] = { 0, NULL };
			#ifdef EZTIME_NETWORK_ENABLE
				if (_ntp_interval) updateNTP();	// Start the cycle of updateNTP running and then setting an event for its next run
			#endif
			_initialised = true;
		}
		// See if any events are due
		for (uint8_t n = 0; n < MAX_EVENTS; n++) {
			if (_events[n].function && nowUTC(false) >= _events[n].time) {
				debug(F("Running event (#")); debug(n + 1); debug(F(") set for ")); debugln(UTC.dateTime(_events[n].time));
				void (*tmp)() = _events[n].function;
				_events[n] = { 0, NULL };		// reset the event
				(tmp)();						// execute the function
			}
		}
		yield();
	}

	void deleteEvent(const uint8_t event_handle) { 
		if (event_handle && event_handle <= MAX_EVENTS) {
			debug(F("Deleted event (#")); debug(event_handle); debug(F("), set for ")); debugln(UTC.dateTime(_events[event_handle - 1].time));	
			_events[event_handle - 1] = { 0, NULL };
		}
	}

	void deleteEvent(void (*function)()) { 
		for (uint8_t n = 0; n< MAX_EVENTS; n++) {
			if (_events[n].function == function) {
				debug(F("Deleted event (#")); debug(n + 1); debug(F("), set for ")); debugln(UTC.dateTime(_events[n].time));
				_events[n] = { 0, NULL };
			}
		}
	}

	void breakTime(const time_t timeInput, tmElements_t &tm){
		// break the given time_t into time components
		// this is a more compact version of the C library localtime function
		// note that year is offset from 1970 !!!

		uint8_t year;
		uint8_t month, monthLength;
		uint32_t time;
		unsigned long days;

		time = (uint32_t)timeInput;
		tm.Second = time % 60;
		time /= 60; // now it is minutes
		tm.Minute = time % 60;
		time /= 60; // now it is hours
		tm.Hour = time % 24;
		time /= 24; // now it is days
		tm.Wday = ((time + 4) % 7) + 1;  // Sunday is day 1 

		year = 0;  
		days = 0;
		while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
			year++;
		}
		tm.Year = year; // year is offset from 1970 

		days -= LEAP_YEAR(year) ? 366 : 365;
		time  -= days; // now it is days in this year, starting at 0

		days=0;
		month=0;
		monthLength=0;
		for (month=0; month<12; month++) {
			if (month==1) { // february
				if (LEAP_YEAR(year)) {
					monthLength=29;
				} else {
					monthLength=28;
				}
			} else {
				monthLength = monthDays[month];
			}

			if (time >= monthLength) {
				time -= monthLength;
			} else {
				break;
			}
		}
		tm.Month = month + 1;  // jan is month 1  
		tm.Day = time + 1;     // day of month
	}

	time_t makeTime(const uint8_t hour, const uint8_t minute, const uint8_t second, const uint8_t day, const uint8_t month, const uint16_t year) {
		tmElements_t tm;
		tm.Hour = hour;
		tm.Minute = minute;
		tm.Second = second;
		tm.Day = day;
		tm.Month = month;
		if (year > 68) {			// time_t cannot reach beyond 68 + 1970 anyway, so if bigger user means actual years
			tm.Year = year - 1970;
		} else {
			tm.Year = year;
		}
		return makeTime(tm);
	}

	time_t makeTime(tmElements_t &tm){
	// assemble time elements into time_t 
	// note year argument is offset from 1970 (see macros in time.h to convert to other formats)
	// previous version used full four digit year (or digits since 2000),i.e. 2009 was 2009 or 9
	
		int i;
		uint32_t seconds;

		// seconds from 1970 till 1 jan 00:00:00 of the given year
		seconds= tm.Year * SECS_PER_DAY * 365UL;

		for (i = 0; i < tm.Year; i++) {
			if (LEAP_YEAR(i)) {
			  seconds +=  SECS_PER_DAY;   // add extra days for leap years
			}
		}

		// add days for this year, months start from 1
		for (i = 1; i < tm.Month; i++) {
			if ( (i == 2) && LEAP_YEAR(tm.Year)) { 
			  seconds += SECS_PER_DAY * 29UL;
			} else {
			  seconds += SECS_PER_DAY * (uint32_t)monthDays[i-1];  //monthDay array starts from 0
			}
		}
	
		seconds+= (tm.Day-1) * SECS_PER_DAY;
		seconds+= tm.Hour * 3600UL;
		seconds+= tm.Minute * 60UL;
		seconds+= tm.Second;
		
		return (time_t)seconds; 
	}

	// makeOrdinalTime allows you to resolve "second thursday in September in 2018" into a number of seconds since 1970
	// (Very useful for the timezone calculations that ezTime does internally) 
	// If ordinal is 0 or 5 it is taken to mean "the last $wday in $month"
	time_t makeOrdinalTime(const uint8_t hour, const uint8_t minute, uint8_t const second, uint8_t ordinal, const uint8_t wday, const uint8_t month, uint16_t year) {
		if (year <= 68 ) year = 1970 + year;		// fix user intent
		uint8_t m = month;   
		uint8_t w = ordinal;
		if (w == 5) {	
			ordinal = 0;
			w = 0;
		}
		if (w == 0) {			// is this a "Last week" rule?
			if (++m > 12) {		// yes, for "Last", go to the next month
				m = 1;
				++year;
			}
			w = 1;               // and treat as first week of next month, subtract 7 days later
		}
		time_t t = makeTime(hour, minute, second, 1, m, year);
		// add offset from the first of the month to weekday, and offset for the given week
		t += ( (wday - UTC.weekday(t) + 7) % 7 + (w - 1) * 7 ) * SECS_PER_DAY;
		// back up a week if this is a "Last" rule
		if (ordinal == 0) t -= 7 * SECS_PER_DAY;
		return t;
	}

	String zeropad(const uint32_t number, const uint8_t length) {
		String out;
		out.reserve(length);
		out = String(number);
		while (out.length() < length) out = "0" + out;
		return out;
	}

	time_t compileTime(const String compile_date /* = __DATE__ */, const String compile_time /* = __TIME__ */) {
	
		uint8_t hrs = compile_time.substring(0,2).toInt();
		uint8_t min = compile_time.substring(3,5).toInt();
		uint8_t sec = compile_time.substring(6).toInt();
		uint8_t day = compile_date.substring(4,6).toInt();
		int16_t year = compile_date.substring(7).toInt();
		String iterate_month;
		for (uint8_t month = 1; month < 13; month++) {
			iterate_month = monthStr(month);
			if ( iterate_month.substring(0,3) == compile_date.substring(0,3) ) {
				return makeTime(hrs, min, sec, day, month, year);
			}
		}
		return 0;
	}

	bool secondChanged() {
		time_t t = nowUTC(false);
		if (_last_read_t != t) return true;
		return false;
	}

	bool minuteChanged() {
		time_t t = nowUTC(false);
		if (_last_read_t / 60 != t / 60) return true;
		return false;
	}


	#ifdef EZTIME_NETWORK_ENABLE

		void updateNTP() {
			deleteEvent(updateNTP);	// Delete any events pointing here, in case called manually
			time_t t;
			uint64_t measured_at;
			if (queryNTP(_ntp_server, t, measured_at)) {
				int32_t correction = (int32_t)( (uint64_t)(t - _last_sync_time) * _micros_in_sec ) - ( measured_at - _last_sync_micros );
				_last_sync_time = t;
				_last_sync_micros = measured_at;
				_last_read_us = ( micros() - measured_at) % _micros_in_sec;
				info(F("Received time: "));
				info(UTC.dateTime(t, F("l, d-M-y H:i:s.u T")));
				if (_time_status != timeNotSet) {
					info(F(" (internal clock was "));
					if (!correction) {
						infoln(F("spot on)"));
					} else {
						info(String(abs(correction)));
						if (correction > 0) {
							infoln(F(" us fast)"));
						} else {
							infoln(F(" us slow)"));
						}
					}
				} else {
					infoln("");
				}
				if (_ntp_interval) UTC.setEvent(updateNTP, t + _ntp_interval);
				_time_status = timeSet;
			} else {
			        if ( nowUTC(false) > _last_sync_time + _ntp_interval + NTP_STALE_AFTER ) {
			        	_time_status = timeNeedsSync;
			        }
				UTC.setEvent(updateNTP, nowUTC(false) + NTP_RETRY);
			}
		}

		// This is a nice self-contained NTP routine if you need one: feel free to use it.
		// It gives you the seconds since 1970 (unix epoch) and the micros() on your system when 
		// that happened (by deducting fractional seconds and estimated network latency).
		bool queryNTP(const String server, time_t &t, uint64_t &measured_at) {
			info(F("Querying "));
			info(server);
			info(F(" ... "));

			#ifndef EZTIME_ETHERNET
				if (WiFi.status() != WL_CONNECTED) { triggerError(NO_NETWORK); return false; }
				#ifndef EZTIME_WIFIESP
					WiFiUDP udp;
				#else
					WiFiEspUDP udp;
				#endif
			#else
				EthernetUDP udp;
			#endif
	
			// Send NTP packet
			byte buffer[NTP_PACKET_SIZE];
			memset(buffer, 0, NTP_PACKET_SIZE);
			buffer[0] = 0b11100011;		// LI, Version, Mode
			buffer[1] = 0;   			// Stratum, or type of clock
			buffer[2] = 9;				// Polling Interval (9 = 2^9 secs = ~9 mins, close to our 10 min default)
			buffer[3] = 0xEC;			// Peer Clock Precision
										// 8 bytes of zero for Root Delay & Root Dispersion
			buffer[12]  = 'X';			// "kiss code", see RFC5905
			buffer[13]  = 'E';			// (codes starting with 'X' are not interpreted)
			buffer[14]  = 'Z';
			buffer[15]  = 'T';	
	
			udp.flush();
			udp.begin(NTP_LOCAL_PORT);
			uint64_t started = micros();
			udp.beginPacket(server.c_str(), 123); //NTP requests are to port 123
			udp.write(buffer, NTP_PACKET_SIZE);
			udp.endPacket();

			// Wait for packet or return false with timed out
			while (!udp.parsePacket()) {
				delay (1);
				if (micros() - started > NTP_TIMEOUT) {
					udp.stop();	
					triggerError(TIMEOUT); 
					return false;
				}
			}
			udp.read(buffer, NTP_PACKET_SIZE);
			udp.stop();													// On AVR there's only very limited sockets, we want to free them when done.
	
			//print out received packet for debug
			int i;
			debug(F("Received data:"));
			for (i = 0; i < NTP_PACKET_SIZE; i++) {
				if ((i % 4) == 0) {
					debugln();
					debug(String(i) + ": ");
				}
				debug(buffer[i], HEX);
				debug(F(", "));
			}
			debugln();

			//prepare timestamps
			uint32_t highWord, lowWord;	
			highWord = ( buffer[16] << 8 | buffer[17] ) & 0x0000FFFF;
			lowWord = ( buffer[18] << 8 | buffer[19] ) & 0x0000FFFF;
			uint32_t reftsSec = highWord << 16 | lowWord;				// reference timestamp seconds

			highWord = ( buffer[32] << 8 | buffer[33] ) & 0x0000FFFF;
			lowWord = ( buffer[34] << 8 | buffer[35] ) & 0x0000FFFF;
			uint32_t rcvtsSec = highWord << 16 | lowWord;				// receive timestamp seconds

			highWord = ( buffer[40] << 8 | buffer[41] ) & 0x0000FFFF;
			lowWord = ( buffer[42] << 8 | buffer[43] ) & 0x0000FFFF;
			uint32_t secsSince1900 = highWord << 16 | lowWord;			// transmit timestamp seconds

			highWord = ( buffer[44] << 8 | buffer[45] ) & 0x0000FFFF;
			lowWord = ( buffer[46] << 8 | buffer[47] ) & 0x0000FFFF;
			uint32_t fraction = highWord << 16 | lowWord;				// transmit timestamp fractions	

			//check if received data makes sense
			//buffer[1] = stratum - should be 1..15 for valid reply
			//also checking that all timestamps are non-zero and receive timestamp seconds are <= transmit timestamp seconds
			if ((buffer[1] < 1) or (buffer[1] > 15) or (reftsSec == 0) or (rcvtsSec == 0) or (rcvtsSec > secsSince1900)) {
				// we got invalid packet
				triggerError(INVALID_DATA); 
				return false;
			}

			// Set the t and measured_at variables that were passed by reference
			uint64_t done = micros();
			info(F("success (round trip ")); info((uint32_t)(done - started)); infoln(F(" us)"));
			t = secsSince1900 - 2208988800UL;					// Subtract 70 years to get seconds since 1970
			uint32_t us = fraction / 4294UL;					// Turn 32 bit fraction into ms by dividing by 2^32 / 1000000 
			measured_at = done - ((done - started) / 2) - us;	// Assume symmetric network latency and return when we think the whole second was.
				
			return true;
		}

		void setInterval(const uint16_t seconds /* = 0 */) { 
			deleteEvent(updateNTP);
			_ntp_interval = seconds;
			if (seconds) UTC.setEvent(updateNTP, nowUTC(false) + _ntp_interval);
		}

		void setServer(const String ntp_server /* = NTP_SERVER */) { _ntp_server = ntp_server; }

		bool waitForSync(const uint16_t timeout /* = 0 */) {

			unsigned long start = millis();
		
			#if !defined(EZTIME_ETHERNET)
				if (WiFi.status() != WL_CONNECTED) {
					info(F("Waiting for WiFi ... "));
					while (WiFi.status() != WL_CONNECTED) {
						if ( timeout && (millis() - start) / 1000 > timeout ) { triggerError(TIMEOUT); return false;};
						events();
						delay(25);
					}
					infoln(F("connected"));
				}
			#endif

			if (_time_status != timeSet) {
				infoln(F("Waiting for time sync"));
				while (_time_status != timeSet) {
					if ( timeout && (millis() - start) / 1000 > timeout ) { triggerError(TIMEOUT); return false;};
					delay(250);
					events();
				}
				infoln(F("Time is in sync"));
			}
			return true;
		}
		
		time_t lastNtpUpdateTime() { return _last_sync_time; }
	
	#endif // EZTIME_NETWORK_ENABLE

}


//
// Timezone class
//

Timezone::Timezone(const bool locked_to_UTC /* = false */) {
	_locked_to_UTC = locked_to_UTC;
	_posix = "UTC";
	#ifdef EZTIME_NETWORK_ENABLE
		#ifdef EZTIME_CACHE_EEPROM
			_cache_month = 0;
			_eeprom_address = -1;
		#endif
		#ifdef EZTIME_CACHE_NVS
			_cache_month = 0;
			_nvs_name = "";
			_nvs_key = "";
		#endif
		_olson = "";
	#endif
}

bool Timezone::setPosix(const String posix) {
	if (_locked_to_UTC) { triggerError(LOCKED_TO_UTC); return false; }
	_posix = posix;
	#ifdef EZTIME_NETWORK_ENABLE
		_olson = "";
	#endif
	return true;
}

time_t Timezone::now() { return tzTime(); }

time_t Timezone::tzTime(time_t t /* = TIME_NOW */, ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	if (_locked_to_UTC) return nowUTC();	// just saving some time and memory
	String tzname;
	bool is_dst;
	int16_t offset;
	return tzTime(t, local_or_utc, tzname, is_dst, offset);
}

time_t Timezone::tzTime(time_t t, ezLocalOrUTC_t local_or_utc, String &tzname, bool &is_dst, int16_t &offset) {

	if (t == TIME_NOW) {
		t = nowUTC(); 
		local_or_utc = UTC_TIME;
	} else if (t == LAST_READ) {
		t = _last_read_t;
		local_or_utc = UTC_TIME;
	}
	
	int8_t offset_hr = 0;
	uint8_t offset_min = 0;
	int8_t dst_shift_hr = 1;
	uint8_t dst_shift_min = 0;
	
	uint8_t start_month = 0, start_week = 0, start_dow = 0, start_time_hr = 2, start_time_min = 0;
	uint8_t end_month = 0, end_week = 0, end_dow = 0, end_time_hr = 2, end_time_min = 0;
	
	enum posix_state_e {STD_NAME, OFFSET_HR, OFFSET_MIN, DST_NAME, DST_SHIFT_HR, DST_SHIFT_MIN, START_MONTH, START_WEEK, START_DOW, START_TIME_HR, START_TIME_MIN, END_MONTH, END_WEEK, END_DOW, END_TIME_HR, END_TIME_MIN};
	posix_state_e state = STD_NAME; 

	bool ignore_nums = false;
	char c = 1; // Dummy value to get while(newchar) started
	uint8_t strpos = 0;
	uint8_t stdname_end = _posix.length() - 1;
	uint8_t dstname_begin = _posix.length();
	uint8_t dstname_end = _posix.length();

	while (strpos < _posix.length()) {
		c = (char)_posix[strpos];

		// Do not replace the code below with switch statement: evaluation of state that 
		// changes while this runs. (Only works because this state can only go forward.)

		if (c && state == STD_NAME) {
			if (c == '<') ignore_nums = true;
			if (c == '>') ignore_nums = false;
			if (!ignore_nums && (isDigit(c) || c == '-'  || c == '+')) {
				state = OFFSET_HR;
				stdname_end = strpos - 1;
			}
		}
		if (c && state == OFFSET_HR) {
			if (c == '+') {
				// Ignore the plus
			} else if (c == ':') {
				state = OFFSET_MIN;
				c = 0;
			} else if (c != '-' && !isDigit(c)) {
				state = DST_NAME;
				dstname_begin = strpos;
			} else {
				if (!offset_hr) offset_hr = atoi(_posix.c_str() + strpos);
			}
		}			
		if (c && state == OFFSET_MIN) {
			if (!isDigit(c)) {
				state = DST_NAME;
				dstname_begin = strpos;
				ignore_nums = false;
			} else {
				if (!offset_min) offset_min = atoi(_posix.c_str() + strpos);
			}
		}				
		if (c && state == DST_NAME) {
			if (c == '<') ignore_nums = true;
			if (c == '>') ignore_nums = false;
			if (c == ',') {
				state = START_MONTH;
				c = 0;
				dstname_end = strpos - 1;
			} else if (!ignore_nums && (c == '-' || isDigit(c))) {
				state = DST_SHIFT_HR;
				dstname_end = strpos - 1;
			}
		}		
		if (c && state == DST_SHIFT_HR) {
			if (c == ':') {
				state = DST_SHIFT_MIN;
				c = 0;
			} else if (c == ',') {
				state = START_MONTH;
				c = 0;
			} else if (dst_shift_hr == 1) dst_shift_hr = atoi(_posix.c_str() + strpos);
		}			
		if (c && state == DST_SHIFT_MIN) {
			if (c == ',') {
				state = START_MONTH;
				c = 0;
			} else if (!dst_shift_min) dst_shift_min = atoi(_posix.c_str() + strpos);
		}			
		if (c && state == START_MONTH) {
			if (c == '.') {
				state = START_WEEK;
				c = 0;
			} else if (c != 'M' && !start_month) start_month = atoi(_posix.c_str() + strpos);	
		}			
		if (c && state == START_WEEK) {
			if (c == '.') {
				state = START_DOW;
				c = 0;
			} else start_week = c - '0';
		}		
		if (c && state == START_DOW) {
			if (c == '/') {
				state = START_TIME_HR;
				c = 0;
			} else if (c == ',') {
				state = END_MONTH;
				c = 0;
			} else start_dow = c - '0';				
		}
		if (c && state == START_TIME_HR) {
			if (c == ':') {
				state = START_TIME_MIN;
				c = 0;
			} else if (c == ',') {
				state = END_MONTH;
				c = 0;
			} else if (start_time_hr == 2) start_time_hr = atoi(_posix.c_str() + strpos);
		}		
		if (c && state == START_TIME_MIN) {
			if (c == ',') {
				state = END_MONTH;
				c = 0;
			} else if (!start_time_min) start_time_min = atoi(_posix.c_str() + strpos);
		}		
		if (c && state == END_MONTH) {
			if (c == '.') {
				state = END_WEEK;
				c = 0;
			} else if (c != 'M') if (!end_month) end_month = atoi(_posix.c_str() + strpos);
		}			
		if (c && state == END_WEEK) {
			if (c == '.') {
				state = END_DOW;
				c = 0;
			} else end_week = c - '0';
		}		
		if (c && state == END_DOW) {
			if (c == '/') {
				state = END_TIME_HR;
				c = 0;			
			} else end_dow = c - '0';
		}
		if (c && state == END_TIME_HR) {
			if (c == ':') {
				state = END_TIME_MIN;
				c = 0;
			}  else if (end_time_hr == 2) end_time_hr = atoi(_posix.c_str() + strpos);
		}		
		if (c && state == END_TIME_MIN) {
			if (!end_time_min) end_time_min = atoi(_posix.c_str() + strpos);
		}
		strpos++;
	}	
	
	int16_t std_offset = (offset_hr < 0) ? offset_hr * 60 - offset_min : offset_hr * 60 + offset_min;
	
	tzname = _posix.substring(0, stdname_end + 1);	// Overwritten with dstname later if needed
	if (!start_month) {
		if (tzname == "UTC" && std_offset) tzname = "???";
		is_dst = false;
		offset = std_offset;
	} else {
		int16_t dst_offset = std_offset - dst_shift_hr * 60 - dst_shift_min;
		// to find the year
		tmElements_t tm;
		ezt::breakTime(t, tm);	
		
		// in local time
		time_t dst_start = ezt::makeOrdinalTime(start_time_hr, start_time_min, 0, start_week, start_dow + 1, start_month, tm.Year + 1970);
		time_t dst_end = ezt::makeOrdinalTime(end_time_hr, end_time_min, 0, end_week, end_dow + 1, end_month, tm.Year + 1970);
		
		if (local_or_utc == UTC_TIME) {
			dst_start += std_offset * 60LL;
			dst_end += dst_offset * 60LL;
		}
		
		if (dst_end > dst_start) {
			is_dst = (t >= dst_start && t < dst_end);		// northern hemisphere
		} else {
			is_dst = !(t >= dst_end && t < dst_start);		// southern hemisphere
		}

		if (is_dst) {
			offset = dst_offset;
			tzname = _posix.substring(dstname_begin, dstname_end + 1);
		} else {
			offset = std_offset;
		}
	}

	if (local_or_utc == LOCAL_TIME) {
		return t + offset * 60LL;
	} else {
		return t - offset * 60LL;
	}
}

String Timezone::getPosix() { return _posix; }

#ifdef EZTIME_NETWORK_ENABLE

	bool Timezone::setLocation(const String location /* = "GeoIP" */) {
	
		info(F("Timezone lookup for: "));
		info(location);
		info(F(" ... "));
		if (_locked_to_UTC) { triggerError(LOCKED_TO_UTC); return false; }
		
		#ifndef EZTIME_ETHERNET
			if (WiFi.status() != WL_CONNECTED) { triggerError(NO_NETWORK); return false; }
			#ifndef EZTIME_WIFIESP
				WiFiUDP udp;
			#else
				WiFiEspUDP udp;
			#endif
		#else
			EthernetUDP udp;
		#endif
		
		udp.flush();
		udp.begin(TIMEZONED_LOCAL_PORT);
		uint64_t started = micros();
		udp.beginPacket(TIMEZONED_REMOTE_HOST, TIMEZONED_REMOTE_PORT);
		udp.write((const uint8_t*)location.c_str(), location.length());
		udp.endPacket();
		
		// Wait for packet or return false with timed out
		while (!udp.parsePacket()) {
			delay (1);
			if (micros() - started > TIMEZONED_TIMEOUT) {
				udp.stop();	
				triggerError(TIMEOUT);
				return false;
			}
		}
		// Stick result in String recv 
		String recv;
		recv.reserve(60);
		while (udp.available()) recv += (char)udp.read();
		udp.stop();
		info(F("(round trip "));
		info((uint32_t)(micros() - started));
		info(F(" us)  "));
		if (recv.substring(0,6) == "ERROR ") {
			_server_error = recv.substring(6);
			error (SERVER_ERROR);
			return false;
		}
		if (recv.substring(0,3) == "OK ") {
			_olson = recv.substring(3, recv.indexOf(" ", 4));
			_posix = recv.substring(recv.indexOf(" ", 4) + 1);
			infoln(F("success."));
			info(F("  Olson: ")); infoln(_olson);
			info(F("  Posix: ")); infoln(_posix);
			#if defined(EZTIME_CACHE_EEPROM) || defined(EZTIME_CACHE_NVS)
				String tzinfo = _olson + " " + _posix;
				writeCache(tzinfo);		// caution, byref to save memory, tzinfo mangled afterwards
			#endif
			return true;
		}
		error (DATA_NOT_FOUND);
		return false;
	}
	
	
	String Timezone::getOlson() {
		return _olson;
	}

	String Timezone::getOlsen() {
		return _olson;
	}	


	#if defined(EZTIME_CACHE_EEPROM) || defined(EZTIME_CACHE_NVS)
	
		#if defined(ESP32) || defined(ESP8266)
			#define eepromBegin()	EEPROM.begin(4096)
			#define eepromEnd()		EEPROM.end()
			#define eepromLength()	(4096)
		#else
			#define eepromBegin()	""
			#define eepromEnd()		""
			#define eepromLength()	EEPROM.length()
		#endif
		
		#ifdef EZTIME_CACHE_EEPROM
			bool Timezone::setCache(const int16_t address) {
				eepromBegin();
				if (address + EEPROM_CACHE_LEN > eepromLength()) { triggerError(CACHE_TOO_SMALL); return false; }
				_eeprom_address = address;
				eepromEnd();
				return setCache();
			}
		#endif
	
		#ifdef EZTIME_CACHE_NVS
			bool Timezone::setCache(const String name, const String key) {
				_nvs_name = name;
				_nvs_key = key;
				return setCache();
			}
		#endif

		bool Timezone::setCache() {
			String olson, posix;
			uint8_t months_since_jan_2018;
			if (readCache(olson, posix, months_since_jan_2018)) {
				setPosix(posix);
				_olson = olson;
				_cache_month = months_since_jan_2018;
				if ( (year() - 2018) * 12 + month(LAST_READ) - months_since_jan_2018 > MAX_CACHE_AGE_MONTHS) {
					infoln(F("Cache stale, getting fresh"));
					setLocation(olson);
				}
				return true;
			}
			return false;
		}
		
		void Timezone::clearCache(const bool delete_section /* = false */) {
		
			#ifdef EZTIME_CACHE_EEPROM
				eepromBegin();
				if (_eeprom_address < 0) { triggerError(NO_CACHE_SET); return; }
				for (int16_t n = _eeprom_address; n < _eeprom_address + EEPROM_CACHE_LEN; n++) EEPROM.write(n, 0);
				eepromEnd();
			#endif

			#ifdef EZTIME_CACHE_NVS
				if (_nvs_name == "" || _nvs_key == "") { triggerError(NO_CACHE_SET); return; }
				Preferences prefs;
				prefs.begin(_nvs_name.c_str(), false);
				if (delete_section) {
					prefs.clear();
				} else {
					prefs.remove(_nvs_key.c_str());
				}
				prefs.end();
			#endif
		}

		bool Timezone::writeCache(String &str) {
			uint8_t months_since_jan_2018 = 0;
			if (year() >= 2018) months_since_jan_2018 = (year(LAST_READ) - 2018) * 12 + month(LAST_READ) - 1;

			#ifdef EZTIME_CACHE_EEPROM
				if (_eeprom_address < 0) return false;

				info(F("Caching timezone data  "));
				if (str.length() > MAX_CACHE_PAYLOAD) { triggerError(CACHE_TOO_SMALL); return false; }
				
				uint16_t last_byte = _eeprom_address + EEPROM_CACHE_LEN - 1;	
				uint16_t addr = _eeprom_address;
				
				eepromBegin();
				
				// First byte is cache age, in months since 2018
				EEPROM.write(addr++, months_since_jan_2018);
				
				// Second byte is length of payload
				EEPROM.write(addr++, str.length());
				
				// Followed by payload, compressed. Every 4 bytes to three by encoding only 6 bits, ASCII all-caps
				str.toUpperCase();
				uint8_t store = 0;
				for (uint8_t n = 0; n < str.length(); n++) {
					unsigned char c = str.charAt(n) - 32;
					if ( c > 63) c = 0;
					switch (n % 4) {
						case 0:
							store = c << 2;					//all of 1st
							break;
						case 1:
							store |= c >> 4;				//high two of 2nd
							EEPROM.write(addr++, store);	 
							store = c << 4;					//low four of 2nd
							break;
						case 2:
							store |= c >> 2;				//high four of 3rd
							EEPROM.write(addr++, store);
							store = c << 6;					//low two of third
							break;
						case 3:
							store |= c;						//all of 4th
							EEPROM.write(addr++, store);
							store = 0;
					}
				}
				if (store) EEPROM.write(addr++, store);
				
				// Fill rest of cache (except last byte) with zeroes
				for (; addr < last_byte; addr++) EEPROM.write(addr, 0);

				// Add all bytes in cache % 256 and add 42, that is the checksum written to last byte.
				// The 42 is because then checksum of all zeroes then isn't zero.
				uint8_t checksum = 0;
				for (uint16_t n = _eeprom_address; n < last_byte; n++) checksum += EEPROM.read(n);
				checksum += 42;
				EEPROM.write(last_byte, checksum);
				eepromEnd();
				infoln();
				return true;
			#endif
			
			#ifdef EZTIME_CACHE_NVS
				if (_nvs_name == "" || _nvs_key == "") return false;
				infoln(F("Caching timezone data"));
				Preferences prefs;
				prefs.begin(_nvs_name.c_str(), false);
				String tmp = String(months_since_jan_2018) + " " + str;
				prefs.putString(_nvs_key.c_str(), tmp);
				prefs.end();
				return true;
			#endif
		}
	

		bool Timezone::readCache(String &olson, String &posix, uint8_t &months_since_jan_2018) {

			#ifdef EZTIME_CACHE_EEPROM
				if (_eeprom_address < 0) { triggerError(NO_CACHE_SET); return false; }
				eepromBegin();
				uint16_t last_byte = _eeprom_address + EEPROM_CACHE_LEN - 1;			
				
				for (uint16_t n = _eeprom_address; n <= last_byte; n++) {
					debug(n);
					debug(F(" "));
					debugln(EEPROM.read(n), HEX);
				}
				
				// return false if checksum incorrect
				uint8_t checksum = 0;
				for (uint16_t n = _eeprom_address; n < last_byte; n++) checksum += EEPROM.read(n);
				checksum += 42;				
				if (checksum != EEPROM.read(last_byte)) { eepromEnd(); return false; }
				debugln(F("Checksum OK"));
				
				// Return false if length impossible
				uint8_t len = EEPROM.read(_eeprom_address + 1);
				debug("Length: "); debugln(len);
				if (len > MAX_CACHE_PAYLOAD) { eepromEnd(); return false; }
				
				// OK, we're gonna decompress
				olson.reserve(len + 3);		// Everything goes in olson first. Decompression might overshoot 3 
				months_since_jan_2018 = EEPROM.read(_eeprom_address);
				
				for (uint8_t n = 0; n < EEPROM_CACHE_LEN - 3; n++) {
					uint16_t addr = n + _eeprom_address + 2;
					uint8_t c = EEPROM.read(addr);
					uint8_t p = EEPROM.read(addr - 1);	// previous byte
					switch (n % 3) {
						case 0:
							olson += (char)( ((c & 0b11111100) >> 2) + 32 );
							break;
						case 1:
							olson += (char)( ((p & 0b00000011) << 4) + ((c & 0b11110000) >> 4) + 32 );
							break;
						case 2:
							olson += (char)( ((p & 0b00001111) << 2) + ((c & 0b11000000) >> 6) + 32 );
							olson += (char)( (c & 0b00111111) + 32 );
					}
					if (olson.length() >= len) break;
				}
				
				uint8_t first_space = olson.indexOf(' ');
				posix = olson.substring(first_space + 1, len);
				olson = olson.substring(0, first_space);
				
				// Restore case of olson (best effort)
				String olson_lowercase = olson;
				olson_lowercase.toLowerCase();
				for (uint8_t n = 1; n < olson.length(); n++) {
					unsigned char p = olson.charAt(n - 1);	// previous character
					if (p != '_' && p != '/' && p != '-') {
						olson.setCharAt(n, olson_lowercase[n]);
					}
				}
				info(F("Cache read. Olson: ")); info(olson); info (F("  Posix: ")); infoln(posix);
				eepromEnd();
				return true;
			#endif						
			
			#ifdef EZTIME_CACHE_NVS
				if (_nvs_name == "" || _nvs_key == "") { triggerError(NO_CACHE_SET); return false; }
				
				Preferences prefs;
				prefs.begin(_nvs_name.c_str(), true);
				String read_string = prefs.getString(_nvs_key.c_str());
				read_string.trim();
				prefs.end();
				if (read_string == "") return false;
				
				uint8_t first_space = read_string.indexOf(' ');
				uint8_t second_space = read_string.indexOf(' ', first_space + 1);
				if (first_space && second_space) {
					months_since_jan_2018 = read_string.toInt();
					posix = read_string.substring(second_space + 1);
					olson = read_string.substring(first_space + 1, second_space);
					info(F("Cache read. Olson: ")); info(olson); info (F("  Posix: ")); infoln(posix);
					return true;
				}
				return false;
			#endif
		}
		
	#endif	// defined(EZTIME_CACHE_EEPROM) || defined(EZTIME_CACHE_NVS)


#endif // EZTIME_NETWORK_ENABLE


void Timezone::setDefault() {
	defaultTZ = this;
	debug(F("Default timezone set to ")); debug(_olson); debug(F("  "));debugln(_posix);
}

bool Timezone::isDST(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	String tzname;
	bool is_dst;
	int16_t offset;
	t = tzTime(t, local_or_utc, tzname, is_dst, offset);
	return is_dst;
}

String Timezone::getTimezoneName(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	String tzname;
	bool is_dst;
	int16_t offset;
	t = tzTime(t, local_or_utc, tzname, is_dst, offset);
	return tzname;
}

int16_t Timezone::getOffset(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	String tzname;
	bool is_dst;
	int16_t offset;
	t = tzTime(t, local_or_utc, tzname, is_dst, offset);
	return offset;
}

uint8_t Timezone::setEvent(void (*function)(), const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr) {
	time_t t = ezt::makeTime(hr, min, sec, day, mnth, yr);
	return setEvent(function, t);
}

uint8_t Timezone::setEvent(void (*function)(), time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	for (uint8_t n = 0; n < MAX_EVENTS; n++) {
		if (!_events[n].function) {
			_events[n].function = function;
			_events[n].time = t;
			debug(F("Set event (#")); debug(n + 1); debug(F(") to trigger on: ")); debugln(UTC.dateTime(t));
			return n + 1;
		}
	}
	triggerError(TOO_MANY_EVENTS);
	return 0;
}

void Timezone::setTime(const time_t t, const uint32_t us /* = 0 */) {
	int16_t offset;
	offset = getOffset(t);
	_last_sync_time = t + offset * 60;
	_last_sync_micros = micros() - us;
	_time_status = timeSet;
}

void Timezone::setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr) {
	tmElements_t tm;
	// year can be given as full four digit year or two digts (2010 or 10 for 2010);  
	// it is converted to years since 1970
	if( yr > 99) {
		yr = yr - 1970;
	} else {
		yr += 30; 
	}
	tm.Year = yr;
	tm.Month = mnth;
	tm.Day = day;
	tm.Hour = hr;
	tm.Minute = min;
	tm.Second = sec;
	setTime(ezt::makeTime(tm));
}

String Timezone::dateTime(const String format /* = DEFAULT_TIMEFORMAT */) {
	return dateTime(TIME_NOW, format);
}

String Timezone::dateTime(const time_t t, const String format /* = DEFAULT_TIMEFORMAT */) {
	return dateTime(t, LOCAL_TIME, format);
}

String Timezone::dateTime(time_t t, const ezLocalOrUTC_t local_or_utc, const String format /* = DEFAULT_TIMEFORMAT */) {

	String tzname;
	bool is_dst;
	int16_t offset;

	if (t == TIME_NOW || t == LAST_READ || local_or_utc == UTC_TIME) {
		// in these cases we actually want tzTime to translate the time for us
		// back in to this timezone's time as well as grab the timezone info
		// from the stored POSIX data
		t = tzTime(t, UTC_TIME, tzname, is_dst, offset);
	} else {
		// when receiving a local time we don't want to translate the timestamp
		// but rather use tzTime to just parse the info about the timezone from
		// the stored POSIX data
		tzTime(t, LOCAL_TIME, tzname, is_dst, offset);
	}

	String tmpstr;
	uint8_t tmpint8;
	String out = "";

	tmElements_t tm;
	ezt::breakTime(t, tm);

	int8_t hour12 = tm.Hour % 12;
	if (hour12 == 0) hour12 = 12;
	
	int32_t o;

	bool escape_char = false;
	
	for (uint8_t n = 0; n < format.length(); n++) {
	
		char c = format.charAt(n);
		
		if (escape_char) {
			out += String(c);
			escape_char = false;
		} else {
		
			switch (c) {
		
				case '\\':	// Escape character, ignore this one, and let next through as literal character
				case '~':	// Same but easier without all the double escaping
					escape_char = true;
					break;
				case 'd':	// Day of the month, 2 digits with leading zeros
					out += ezt::zeropad(tm.Day, 2);
					break;
				case 'D':	// A textual representation of a day, usually two or three letters
					out += ezt::dayShortStr(tm.Wday);
					break;
				case 'j':	// Day of the month without leading zeros
					out += String(tm.Day);
					break;
				case 'l':	// (lowercase L) A full textual representation of the day of the week
					out += ezt::dayStr(tm.Wday);
					break;
				case 'N':	// ISO-8601 numeric representation of the day of the week. ( 1 = Monday, 7 = Sunday )
					tmpint8 = tm.Wday - 1;
					if (tmpint8 == 0) tmpint8 = 7;
					out += String(tmpint8);
					break;
				case 'S':	// English ordinal suffix for the day of the month, 2 characters (st, nd, rd, th)
					switch (tm.Day) {
						case 1:
						case 21:
						case 31:
							out += F("st"); break;
						case 2:
						case 22:
							out += F("nd"); break;
						case 3:
						case 23:
							out += F("rd"); break;
						default:
							out += F("th"); break;
					}
					break;
				case 'w':	// Numeric representation of the day of the week ( 0 = Sunday )
					out += String(tm.Wday);
					break;
				case 'F':	// A full textual representation of a month, such as January or March
					out += ezt::monthStr(tm.Month);
					break;
				case 'm':	// Numeric representation of a month, with leading zeros
					out += ezt::zeropad(tm.Month, 2);
					break;
				case 'M':	// A short textual representation of a month, usually three letters
					out += ezt::monthShortStr(tm.Month);
					break;
				case 'n':	// Numeric representation of a month, without leading zeros
					out += String(tm.Month);
					break;
				case 't':	// Number of days in the given month
					out += String(monthDays[tm.Month - 1]);
					break;
				case 'Y':	// A full numeric representation of a year, 4 digits
					out += String(tm.Year + 1970);
					break;
				case 'y':	// A two digit representation of a year
					out += ezt::zeropad((tm.Year + 1970) % 100, 2);
					break;
				case 'a':	// am or pm
					out += (tm.Hour < 12) ? F("am") : F("pm");
					break;
				case 'A':	// AM or PM
					out += (tm.Hour < 12) ? F("AM") : F("PM");
					break;
				case 'g':	// 12-hour format of an hour without leading zeros
					out += String(hour12);
					break;
				case 'G':	// 24-hour format of an hour without leading zeros
					out += String(tm.Hour);
					break;
				case 'h':	// 12-hour format of an hour with leading zeros
					out += ezt::zeropad(hour12, 2);
					break;
				case 'H':	// 24-hour format of an hour with leading zeros
					out += ezt::zeropad(tm.Hour, 2);
					break;
				case 'i':	// Minutes with leading zeros
					out += ezt::zeropad(tm.Minute, 2);
					break;
				case 's':	// Seconds with leading zeros
					out += ezt::zeropad(tm.Second, 2);
					break;
				case 'T':	// abbreviation for timezone
					out += tzname;	
					break;
				case 'u':	// microseconds as six digits
					out += ezt::zeropad(_last_read_us, 6);				
					break;
				case 'v':	// milliseconds as three digits
					out += ezt::zeropad(_last_read_us / 1000, 3);				
					break;
				#ifdef EZTIME_NETWORK_ENABLE
					case 'e':	// Timezone identifier (Olson)
						out += getOlson();
						break;
				#endif
				case 'O':	// Difference to Greenwich time (GMT) in hours and minutes written together (+0200)
				case 'P':	// Difference to Greenwich time (GMT) in hours and minutes written with colon (+02:00)
					o = offset;
					out += (o < 0) ? "+" : "-";		// reversed from our offset
					if (o < 0) o = 0 - o;
					out += ezt::zeropad(o / 60, 2);
					out += (c == 'P') ? ":" : "";
					out += ezt::zeropad(o % 60, 2);
					break;	
				case 'Z':	//Timezone offset in seconds. West of UTC is negative, east of UTC is positive.
					out += String(0 - offset * 60);
					break;
				case 'z':
					out += String(dayOfYear(t)); // The day of the year (starting from 0)
					break;
				case 'W':
					out += ezt::zeropad(weekISO(t), 2); // ISO-8601 week number of year, weeks starting on Monday
					break;
				case 'X':
					out += String(yearISO(t)); // ISO-8601 year-week notation year, see https://en.wikipedia.org/wiki/ISO_week_date
					break;
				case 'B':
					out += militaryTZ(t);
					break;
				default:
					out += String(c);

			}
		}
	}
	
	return out;
}

String Timezone::militaryTZ(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	int16_t o = getOffset(t);
	if (o % 60) return "?"; // If it's not a whole hour from UTC, it's not a timezone with a military letter code
	o = o / 60;
	if (o > 0) return String((char)('M' + o));
	if (o < 0 && o >= -9) return String((char)('A' - o - 1));	// Minus a negative number == plus 1
	if (o < -9) return String((char)('A' - o));				// Crazy, they're skipping 'J'
	return "Z";
}


uint8_t Timezone::hour(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	return t / 3600 % 24;
}

uint8_t Timezone::minute(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	return t / 60 % 60;
}

uint8_t Timezone::second(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	return t % 60;
}

uint16_t Timezone::ms(time_t t /*= TIME_NOW */) {
	// Note that here passing anything but TIME_NOW or LAST_READ is pointless
	if (t == TIME_NOW) { nowUTC(); return _last_read_us / 1000; }
	if (t == LAST_READ) return _last_read_us / 1000;
	return 0;
}

uint32_t Timezone::us(time_t t /*= TIME_NOW */) {
	// Note that here passing anything but TIME_NOW or LAST_READ is pointless
	if (t == TIME_NOW) { nowUTC(); return _last_read_us; }
	if (t == LAST_READ) return _last_read_us;
	return 0;
}

uint8_t Timezone::day(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	tmElements_t tm;
	ezt::breakTime(t, tm);
	return tm.Day;
}

uint8_t Timezone::weekday(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	tmElements_t tm;
	ezt::breakTime(t, tm);
	return tm.Wday;
}

uint8_t Timezone::month(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	tmElements_t tm;
	ezt::breakTime(t, tm);
	return tm.Month;
}

uint16_t Timezone::year(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	tmElements_t tm;
	ezt::breakTime(t, tm);
	return tm.Year + 1970;
}

uint16_t Timezone::dayOfYear(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	time_t jan_1st = ezt::makeTime(0, 0, 0, 1, 1, year(t));
	return (t - jan_1st) / SECS_PER_DAY;
}

uint8_t Timezone::hourFormat12(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	uint8_t h = t / 3600 % 12;
	if (h) return h;
	return 12;
}

bool Timezone::isAM(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	return (t / 3600 % 24 < 12);
}

bool Timezone::isPM(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	return (t / 3600 % 24 >= 12);
}


// Now this is where this gets a little obscure. The ISO year can be different from the
// actual (Gregorian) year. That is: you can be in january and still be in week 53 of past
// year, _and_ you can be in december and be in week one of the next. The ISO 8601 
// definition for week 01 is the week with the Gregorian year's first Thursday in it.  
// See https://en.wikipedia.org/wiki/ISO_week_date
//
#define startISOyear(year...) ezt::makeOrdinalTime(0, 0, 0, FIRST, THURSDAY, JANUARY, year) - 3UL * SECS_PER_DAY;
uint8_t Timezone::weekISO(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	int16_t yr = year(t);
	time_t this_year = startISOyear(yr);
	time_t prev_year = startISOyear(yr - 1);
	time_t next_year = startISOyear(yr + 1);
	if (t < this_year) this_year = prev_year;
	if (t >= next_year) this_year = next_year;
	return (t - this_year) / ( SECS_PER_DAY * 7UL) + 1;
}

uint16_t Timezone::yearISO(time_t t /*= TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) {
	t = tzTime(t, local_or_utc);
	int16_t yr = year(t);
	time_t this_year = startISOyear(yr);
	time_t next_year = startISOyear(yr + 1);
	if (t < this_year) return yr - 1;
	if (t >= next_year) return yr + 1;
	return yr;
}


Timezone UTC;
Timezone *defaultTZ = &UTC;

namespace ezt {
	// All bounce-throughs to defaultTZ
	String dateTime(const String format /* = DEFAULT_TIMEFORMAT */) { return (defaultTZ->dateTime(format)); }
	String dateTime(time_t t, const String format /* = DEFAULT_TIMEFORMAT */) { return (defaultTZ->dateTime(t, format)); }
	String dateTime(time_t t, const ezLocalOrUTC_t local_or_utc, const String format /* = DEFAULT_TIMEFORMAT */) { return (defaultTZ->dateTime(t, local_or_utc, format)); }
	uint8_t day(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->day(t, local_or_utc)); } 
	uint16_t dayOfYear(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->dayOfYear(t, local_or_utc)); }
	int16_t getOffset(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->getOffset(t, local_or_utc)); }
	String getTimezoneName(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->getTimezoneName(t, local_or_utc)); }
	uint8_t hour(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->hour(t, local_or_utc)); }
	uint8_t hourFormat12(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->hourFormat12(t, local_or_utc)); }
	bool isAM(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->isAM(t, local_or_utc)); }
	bool isDST(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->isDST(t, local_or_utc)); }
	bool isPM(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->isPM(t, local_or_utc)); }
	String militaryTZ(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->militaryTZ(t, local_or_utc)); }
	uint8_t minute(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->minute(t, local_or_utc)); }
	uint8_t month(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->month(t, local_or_utc)); } 
	uint16_t ms(time_t t /* = TIME_NOW */) { return (defaultTZ->ms(t)); }
	time_t now() { return  (defaultTZ->now()); }
	uint8_t second(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->second(t, local_or_utc)); } 
	uint8_t setEvent(void (*function)(), const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr) { return (defaultTZ->setEvent(function,hr, min, sec, day, mnth, yr)); }
	uint8_t setEvent(void (*function)(), time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->setEvent(function, t, local_or_utc)); }
	void setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t month, const uint16_t yr) { defaultTZ->setTime(hr, min, sec, day, month, yr); }
	void setTime(time_t t) { defaultTZ->setTime(t); }
	uint8_t weekISO(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->weekISO(t, local_or_utc)); }
	uint8_t weekday(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->weekday(t, local_or_utc)); }
	uint16_t year(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->year(t, local_or_utc)); } 
	uint16_t yearISO(time_t t /* = TIME_NOW */, const ezLocalOrUTC_t local_or_utc /* = LOCAL_TIME */) { return (defaultTZ->yearISO(t, local_or_utc)); }
}

#if defined (EZTIME_DS3231_ENABLE) || defined (ARDUINO_M5Stack_Core_ESP32) || defined (ARDUINO_D1_MINI32)
#include <Wire.h>

	// Initialize and detect DS3231 RTC
	bool DS3231::begin(TwoWire &wirePort /* = Wire */) {
		_i2cPort = &wirePort;
		ds3231rd(DS3231_REG_CONTROL, 2, &data);
		// Check zero bits in Status register
		if (data[1] & 0x70) {	//DS3231_REG_STATUS
			_rtc_status = timeNotSet;
			return false;
		}
		// Clear EOSC bit to enable osc when running on V-BAT
		data[0] &= ~(1 << DS3231_CTRL_EOSC);
		return ds3231(DS3231_REG_CONTROL, data[0]);
	}

	// Read RTC OSF (Oscillator Stop Flag) from status register
	// and optionally clears the OSF bit
	timeStatus_t DS3231::timeStatus(bool clearOSF /* = false */) {
		data[0] = ds3231(DS3231_REG_STATUS);
		if (data[0] & (1 << DS3231_STAT_OSF)){
			_rtc_status = timeNotSet;
		} else {
			_rtc_status = timeSet;
		}
		if (clearOSF) {	// clear OSF if it's set and the caller wants to clear it
			ds3231(DS3231_REG_STATUS, (data[0] & ~(1 << DS3231_STAT_OSF)));
		}
		return _rtc_status; // RTC oscillator was stopped: The date/time data is invalid.
	}

	// Enable or disable oscillator when running on V-BAT
	// enable
	//  true: Enable RTC clock when running on V-BAT
	// false: Stop RTC clock when running on V-BAT. OSF bit will be set
	// in status register which can be read on next power-on
	bool DS3231::enableOsc(bool enable) {
		data[0] = ds3231(DS3231_REG_CONTROL);
		if (enable) {
			// Clear EOSC bit to enable
			data[0] &= ~(1 << DS3231_CTRL_EOSC);
		} else {
			// Set EOSC bit to disable
			data[0] |= (1 << DS3231_CTRL_EOSC);
			_rtc_status = timeNotSet;
		}
		return ds3231(DS3231_REG_CONTROL, data[0]);
	}

	// Set the RTC to the given time_t value and clear the
	// OSF in the Control/Status register.
	bool DS3231::setTime(time_t t, bool syncCalendar /* = true */) {
		tmElements_t tm;
		ezt::breakTime(t, tm);
		return setTime(tm);
	}

	// year can be given as full four digit year or two digts (2010 or 10 for 2010);  
	// it is converted to years since 1970
	bool DS3231::setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr) {
		tmElements_t tm;
		if( yr > 99) {
			yr = yr - 1970;
		} else {
			yr += 30; 
		}
		tm.Year = yr;
		tm.Month = mnth;
		tm.Day = day;
		tm.Hour = hr;
		tm.Minute = min;
		tm.Second = sec;
		ezt::breakTime(ezt::makeTime(tm), tm);
		return setTime(tm);
	}

	// Set the RTC time from a tmElements_t structure and clear the
	// oscillator stop flag (OSF) in the Control/Status register
	bool DS3231::setTime(tmElements_t &tm) {
		data[0] = (decToBcd(tm.Second));
		data[1] = (decToBcd(tm.Minute));
		data[2] = (decToBcd(tm.Hour));
		data[3] = tm.Wday - 1;
		// if tm.Sunday = 1 then RTC.Sunday is 7
		if(data[3] == 0) data[3] = 7;
		data[4] = (decToBcd(tm.Day));
		data[5] = (decToBcd(tm.Month));
		data[6] = (decToBcd(tmYearToY2k(tm.Year)));
		// Get the nearest micros when tm.Second is writen to RTC
		// Next second mark should be 500us later
		_rtc_set_micros = micros();
		// Write BCD encoded data to RTC registers
		ds3231wr (DS3231_REG_SECONDS, 7, data);
		enableOsc(true);
		_rtc_set_time = ezt::makeTime(tm);
		_rtc_status = timeSet;
		// Clear oscillator halt flag
		return (timeStatus(true) == timeSet ? true : false);
	}

	// Return time and micros RTC time was set 
	time_t DS3231::getSetTime(uint64_t &micros) {
		micros = _rtc_set_micros;
		return _rtc_set_time;
	}

	// Read the current time from the RTC
	// and return it as a time_t value. 
	// Returns false if an invalid date/time format was read from the RTC.
	time_t DS3231::now() {
		tmElements_t tm;
		ds3231rd(DS3231_REG_SECONDS, 7, &data);
		tm.Second = bcdToDec(data[0]);
		tm.Minute = bcdToDec(data[1]);
		tm.Hour   = bcdToDec(data[2] & 0x3f);    // assumes 24hr clock
		tm.Wday   = 1 + data[3];
		if(tm.Wday == 8) tm.Wday = 1;
		tm.Day    = bcdToDec(data[4]);
		tm.Month  = bcdToDec(data[5] & 0x1f);	// do not use the Century bit
		tm.Year   = y2kYearToTm(bcdToDec(data[6])); // +30
		if ((tm.Second > 59) || 
			(tm.Minute > 59) || (tm.Hour > 23)  ||
			(tm.Wday < 1)    || (tm.Wday > 7)   ||        
			(tm.Day < 1)     || (tm.Day > 31)   ||
			(tm.Month < 1)   || (tm.Month > 12) ||
			(tm.Year < 30)   || (tm.Year > 129)) {
			// Invalid date/time read from RTC: Clear date time
			memset(&tm, 0x00, sizeof(tmElements_t));
			return 0;
		}
		return( ezt::makeTime(tm) );
	}

	// Alarm 1 types:
	//			A1_ES
	//			A1_MS
	//			A1_MMSS
	//			A1_HHMMSS
	//			A1_WDHHMMSS	(of the week)
	//			A1_DDHHMMSS	(day of the month)
	// Unused matches can be set to zero. The alarm interrupt must be enabled
	// after setting the alarm, followed by clearing the alarm interrupt flag
	void DS3231::setAlarm1(Alarm1_t alarmType,
						uint8_t dayDate, uint8_t hr, uint8_t min, uint8_t sec) {
		data[0] = decToBcd(sec);
		data[1] = decToBcd(min);
		data[2] = decToBcd(hr);
		data[3] = decToBcd(dayDate);
		if (alarmType & 0x01) { data[0] |= (1 << DS3231_A1M1); }
		if (alarmType & 0x02) { data[1] |= (1 << DS3231_A1M2); }
		if (alarmType & 0x04) { data[2] |= (1 << DS3231_A1M3); }
		if (alarmType & 0x08) { data[3] |= (1 << DS3231_A1M4); }
		if (alarmType & 0x10) { data[3] |= (1 << DS3231_DYDT); }
		ds3231wr (DS3231_REG_ALARM1_SEC, 4, data);
		clearAlarmFlag(Alarm1);
	}

	// Alarm 2 types:
	//			A2_EM
	//			A2_MM
	//			A2_HHMM
	//			A2_WDHHMM
	//			A2_DDHHMM
	// Unused matches can be set to zero. The alarm interrupt must be enabled
	// after setting the alarm, followed by clearing the alarm interrupt flag
	void DS3231::setAlarm2(Alarm2_t alarmType, uint8_t dayDate, uint8_t hr, uint8_t min) {
		data[0] = decToBcd(min);
		data[1] = decToBcd(hr);
		data[2] = decToBcd(dayDate);
		if (alarmType & 0x02) { data[0] |= (1 << DS3231_A1M2); }
		if (alarmType & 0x04) { data[1] |= (1 << DS3231_A1M3); }
		if (alarmType & 0x08) { data[2] |= (1 << DS3231_A1M4); }
		if (alarmType & 0x10) { data[2] |= (1 << DS3231_DYDT); }
		ds3231wr (DS3231_REG_ALARM2_MIN, 3, data);
		clearAlarmFlag(Alarm2);
	}

	// Enable Alarm 1 or 2 interrupt.
	// Enabling the alarm interrupt will disable the Square Wave output on the INT/SQW pin.
	// The INT pin remains high until an alarm match occurs.
	// Alarm1 or Alarm2 enum.
	void DS3231::enableInterrupt(AlarmId_t alarmId) {
		uint8_t controlReg;
		clearAlarmFlag(alarmId);
		controlReg = ds3231(DS3231_REG_CONTROL);
		// Disable square wave out and enable INT
		controlReg |= (1 << DS3231_CTRL_INTCN);
		controlReg |= (1 << (alarmId - 1));
		ds3231(DS3231_REG_CONTROL, controlReg);
	}

	// Disable Alarm 1 or 2 interrupt.
	void DS3231::disableInterrupt(AlarmId_t alarmId) {
		uint8_t controlReg;
		clearAlarmFlag(alarmId);
		controlReg = ds3231(DS3231_REG_CONTROL);
		// Disable square wave out and enable INT
		controlReg |= (1 << DS3231_CTRL_INTCN);
		controlReg &= ~(1 << (alarmId - 1));
		ds3231(DS3231_REG_CONTROL, controlReg);
	}

	// Get Alarm 1 or 2 flag.
	// Call this function to retrieve the alarm status flag. This function can be used in
	// polling as well as with interrupts enabled.
	// The INT pin changes to low when an Alarm 1 or Alarm 2 match occurs and the interrupt is
	// enabled. The pin remains low until both alarm flags are cleared by the application.
	// Alarm1 or Alarm2 enum
	bool DS3231::getAlarmFlag(AlarmId_t alarmId) {
		if (ds3231(DS3231_REG_STATUS) & (1 << (alarmId - 1))) {
			return true;
		} else {
			return false;
		}
	}

	// This function should be called when the alarm flag has been handled in polling and
	// interrupt mode. The INT pin changes to high when both alarm flags are cleared and
	// alarm interrupts are enabled.
	// Alarm1 or Alarm2 enum.
	void DS3231::clearAlarmFlag(AlarmId_t alarmId) {
		uint8_t statusReg;
		statusReg = ds3231(DS3231_REG_STATUS);
		statusReg &= ~(1 << (alarmId - 1));
		ds3231(DS3231_REG_STATUS, statusReg);
	}


	// Enable or disable 32kHz output clock pin.
	void DS3231::enableClockPin(bool enable) {
		uint8_t statusReg;
		statusReg = ds3231(DS3231_REG_STATUS);
		if (enable) {
			statusReg |= (1 << DS3231_STAT_EN32KHZ);
		} else {
			statusReg &= ~(1 << DS3231_STAT_EN32KHZ);
		}
		ds3231(DS3231_REG_STATUS, statusReg);
	}
	
	// Configure SQW (Square Wave) output pin.
	// This will disable or initialize the SQW clock pin. 
	// The alarm interrupt INT pin will be disabled.
	// squareWave configuration:
	//   Disable:	SquareWaveDisable
	//       1Hz:   SquareWave1Hz
	//    1024Hz:   SquareWave1024Hz
	//    4096Hz:   SquareWave4096Hz
	//    8192Hz:   SquareWave8192Hz
	void DS3231::setSquareWave(SquareWave_t squareWave) {
		uint8_t controlReg;
		controlReg = ds3231(DS3231_REG_CONTROL);
		controlReg &= ~((1 << DS3231_CTRL_BBSQW) |
						(1 << DS3231_CTRL_INTCN) |
						(1 << DS3231_CTRL_RS2) |
						(1 << DS3231_CTRL_RS1));
		controlReg |= squareWave;
		ds3231(DS3231_REG_CONTROL, controlReg);
	}

	// The aging offset register capacitance value is added or subtracted from the 
	// capacitance value that the device calculates for each temperature compensation.
	// Aging offset value -127..127, 0.1ppm per LSB (Factory default value: 0)
	// Negative values increases the RTC oscillator frequency
	bool DS3231::setAgingOffset(int8_t val) {
		uint8_t regVal;
		// Convert 8-bit signed value to register value
		if (val < 0) {
			// Calculate two's complement for negative value
			regVal = ~(-val) + 1;
		} else {
			regVal = (uint8_t)val;
		}
		ds3231(DS3231_REG_AGING_OFFSET, regVal);
		// A temperature conversion is required to apply the aging offset change
		return startTemperatureConversion();
	}

	// The aging offset register capacitance value is added or subtracted from the capacitance
	// value that the device calculates for each temperature compensation
	int8_t DS3231::getAgingOffset() {
		uint8_t regVal;
		regVal = ds3231(DS3231_REG_AGING_OFFSET);
		// Convert to 8-bit signed value
		if (regVal & 0x80) {
			// Calculate two's complement for negative aging register value
			return regVal | ~((1 << 8) - 1);
		} else {
			// Positive aging register value
			return regVal;
		}
	}

	// Conversion is needed only to read temperature within 64 seconds,
	// or after changing the aging offset register
	bool DS3231::startTemperatureConversion() {
		uint8_t controlReg;
		// Check if temperature busy flag is set
		if (ds3231(DS3231_REG_STATUS) & (1 << DS3231_STAT_BSY)) {
			return 0;
		}
		// Start temperature conversion
		controlReg = ds3231(DS3231_REG_CONTROL) | (1 << DS3231_CTRL_CONV);
		ds3231(DS3231_REG_CONTROL, controlReg);
		return 1;
	}

	// temperature in 1/10 degrees Celsius
	int32_t DS3231::getTemperature() {
		ds3231rd(DS3231_REG_TEMP_MSB, 2, &data);
		int32_t temperature = data[0] << 8;	//MSB
		temperature |= data[1];	//LSB
		temperature >>= 6;
		// Calculate two's complement when negative
		if (temperature & 0x200) {
			temperature |= 0xFC00; // keep negative by setting bits
		}
		temperature *= 25;	// value is in .25C increments
		return temperature;	// Return computed temperature
	}

	// BCD to decimal conversion.
	uint8_t DS3231::bcdToDec(uint8_t bcd) {
		return (uint8_t)(10 * ((bcd & 0xF0) >> 4) + (bcd & 0x0F));
	}

	// Decimal to BCD conversion.
	uint8_t DS3231::decToBcd(uint8_t dec) {
		return (uint8_t)(((dec / 10) << 4) | (dec % 10));
	}

	// Read register
	uint8_t DS3231::ds3231(uint8_t reg) {
		_i2cPort->beginTransmission((uint8_t)adrDS3231);
		_i2cPort->write(reg);
		_i2cPort->endTransmission();
		_i2cPort->requestFrom((uint8_t)adrDS3231, uint8_t(1));
		return _i2cPort->read();
	}

	// Write to RTC register
	bool DS3231::ds3231(uint8_t reg, uint8_t value) {
		_i2cPort->beginTransmission((uint8_t)adrDS3231);
		_i2cPort->write(reg);
		_i2cPort->write(value);
		return (_i2cPort->endTransmission() == 0);
	}

	// Read buffer from RTC
	bool DS3231::ds3231rd(uint8_t reg, uint8_t len, void *data) {
		_i2cPort->beginTransmission((uint8_t)adrDS3231);
		_i2cPort->write(reg);
		_i2cPort->endTransmission(false);
		_i2cPort->requestFrom((uint8_t)adrDS3231, len);
		for (uint8_t i = 0; i < len; i++) ((uint8_t *)data)[i] = (uint8_t)_i2cPort->read();
		return (_i2cPort->endTransmission() == 0);
	}

	// Write buffer to RTC
	bool DS3231::ds3231wr (uint8_t reg, uint8_t len, void *data) {
		_i2cPort->beginTransmission((uint8_t)adrDS3231);
		_i2cPort->write(reg);
		for (uint8_t i = 0; i < len; i++) _i2cPort->write(((uint8_t *)data)[i]);
		return (_i2cPort->endTransmission() == 0);
	}

DS3231 RTC;

#elif defined (EZTIME_RV3028_ENABLE) || defined (ARDUINO_WESP32)	//K46v1
#include <Wire.h>

	// Initialize and detect RV-3028-C7 RTC
	bool RV3028::begin(TrickleCharge_t trickCharge /* = TCR_15K */, SwitchOver_t switchOver /* = BS_LEVEL */, TwoWire &wirePort /* = Wire */) {
		_i2cPort = &wirePort;
		_alarm_mode = A2_DDHHMM;
		uint8_t setting = rv3028(RV3028_REG_CTRL2);
		// Reads RESET bit in CTRL2 register
		if (setting & (1 << CTRL2_RESET)){
			_rtc_status = timeNotSet;
			return false;	//RTC not detected
		}
		if (setting & (1 << CTRL2_12_24)){
			setting &= ~(1 << CTRL2_12_24);   //Clear 12/24 bit
			rv3028(RV3028_REG_CTRL2, setting);
			delay(1);
		}
		setBit(RV3028_REG_CTRL1, CTRL1_WADA); //Alarm on Date
		setTrickleCharge(trickCharge);
		setBackupSwitchover(switchOver);
		return true;
	}

	// Read RTC PORF (Power On Reset Flag) and optionally clears it
	timeStatus_t RV3028::timeStatus(bool clearPORFandBSF /* = false */) {
		uint8_t statusReg = rv3028(RV3028_REG_STATUS);
		if (statusReg & (1 << STATUS_PORF)){
			// RTC was POR: The date/time data is invalid.
			_rtc_status = timeNotSet;
			//Serial.println("PORF = 1");
		} else {
			if(statusReg & (1 << STATUS_BSF)){
				_rtc_status = timeSet;
				//Serial.println("PORF = 0 BSF = 1");
			} else {
				_rtc_status = timeNotSet;
				//Serial.println("PORF = 0 BSF = 0");
			}
		}
		if (clearPORFandBSF){
			statusReg &= ~(1 << STATUS_PORF);
			statusReg &= ~(1 << STATUS_BSF);
			rv3028(RV3028_REG_STATUS, statusReg);
		}
		return _rtc_status;
	}

	// Set the RTC to the given time_t value and clear the
	// PORF in the Status register.
	bool RV3028::setTime(time_t t, bool syncCalendar /* = true */) {
		if (t < 946684800) {
			t = 946684800; // 2000-01-01 00:00:00
		}
		bool success = false;
		data[0] = t;
		data[1] = t >> 8;
		data[2] = t >> 16;
		data[3] = t >> 24;
		success = rv3028wr(RV3028_REG_UNIX_TIME0, 4, data);
		if (syncCalendar) {
			tmElements_t tm;
			ezt::breakTime(t, tm);
			success = setTime(tm, false);
		}
		return success;
	}

	// year in 2000-2099 range can be given as full four digit year or two digts (2022 or 22 for 2022);  
	bool RV3028::setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr, bool syncEpoch /* = true */) {
		bool success = false;
		tmElements_t tm;
		if(yr <= 99) {
			// it is converted to years since 1970
			yr += 30; 
		} else if ((yr < 2000) || (yr > 2099)) {
			return false;
		} else {
			yr -= 1970;
		}
		tm.Year = yr;
		tm.Month = mnth;
		tm.Day = day;
		tm.Hour = hr;
		tm.Minute = min;
		tm.Second = sec;
		if ((tm.Second > 59) || 
			(tm.Minute > 59) || (tm.Hour > 23)  ||
			(tm.Wday < 1)    || (tm.Wday > 7)   ||        
			(tm.Day < 1)     || (tm.Day > 31)   ||
			(tm.Month < 1)   || (tm.Month > 12) ||
			(tm.Year < 30)   || (tm.Year > 129)) {
			return success;
		}
		time_t t = ezt::makeTime(tm);
		if(syncEpoch){
			success = setTime(t);
		} else {
			ezt::breakTime(t, tm);
			success = setTime(tm, false);
		}
		return success;
	}

	// Set the RTC time from a tmElements_t structure and clear the
	// oscillator stop flag (OSF) in the Control/Status register
	bool RV3028::setTime(tmElements_t &tm, bool syncEpoch /* = true */) {
		bool success = false;
		if ((tm.Second > 59) || 
			(tm.Minute > 59) || (tm.Hour > 23)  ||
			(tm.Wday < 1)    || (tm.Wday > 7)   ||        
			(tm.Day < 1)     || (tm.Day > 31)   ||
			(tm.Month < 1)   || (tm.Month > 12) ||
			(tm.Year < 30)   || (tm.Year > 129))
			return success;
		data[0] = (decToBcd(tm.Second));
		data[1] = (decToBcd(tm.Minute));
		data[2] = (decToBcd(tm.Hour));
		// tm.Sunday = 1 is RTC.Sunday = 0
		data[3] = (decToBcd(tm.Wday - 1));
		data[4] = (decToBcd(tm.Day));
		data[5] = (decToBcd(tm.Month));
		data[6] = (decToBcd(tmYearToY2k(tm.Year)));

		_rtc_set_time = ezt::makeTime(tm);
		// Get the nearest micros when tm.Second is writen to RTC
		_rtc_set_micros = micros();
		// Write BCD encoded data to RTC registers
		success = rv3028wr(RV3028_REG_SECONDS, 7, data);
		// set Unix epoch ONLY if required
		if (syncEpoch) success = setTime(_rtc_set_time, false);
		_rtc_status = timeSet;
		// Clear PORF
		success &= (timeStatus(true) == timeSet ? true : false);
		return success;
	}

	// Return time and micros RTC time was set 
	time_t RV3028::getSetTime(uint64_t &micros) {
		micros = _rtc_set_micros;
		return _rtc_set_time;
	}

	// Read the current time from the RTC
	// and return it as a time_t value. 
	// Returns false if an invalid date/time format was read from the RTC.
	time_t RV3028::now(bool getCalendar /* = false */) {
		if (getCalendar) {
			tmElements_t tm;
			rv3028rd(RV3028_REG_SECONDS, 7, &data);
			tm.Second = bcdToDec(data[0]);
			tm.Minute = bcdToDec(data[1]);
			tm.Hour   = bcdToDec(data[2] & 0x3f);    // assumes 24hr clock
			// RTC.Sunday = 0 is tm.Sunday = 1
			tm.Wday   = data[3] + 1;
			tm.Day    = bcdToDec(data[4]);
			tm.Month  = bcdToDec(data[5] & 0x1f);	// do not use the Century bit
			tm.Year   = y2kYearToTm(bcdToDec(data[6])); // +30
			if ((tm.Second > 59) || 
				(tm.Minute > 59) || (tm.Hour > 23)  ||
				(tm.Wday < 1)    || (tm.Wday > 7)   ||        
				(tm.Day < 1)     || (tm.Day > 31)   ||
				(tm.Month < 1)   || (tm.Month > 12) ||
				(tm.Year < 30)   || (tm.Year > 129))
				return 0;
			return (ezt::makeTime(tm));
		} else {
			rv3028rd(RV3028_REG_UNIX_TIME0, 4, data);
			return (time_t)((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[1] << 8) | data[0];
		}
		return 0;
	}

	bool RV3028::setAlarm(time_t t) {
		tmElements_t tm;
		ezt::breakTime(t, tm);
		return setAlarm(tm);
	}

	bool RV3028::setAlarm(tmElements_t &tm) {
		disableInterrupt(Alarm2);
		clearInterruptFlag(Alarm2);
		rv3028rd(RV3028_REG_MONTHS, 2, &data);	//read month and year only
		//Get alarm's month and year offset from the current ones
		uint8_t _yearMonthOffset = tm.Year - y2kYearToTm(bcdToDec(data[1])); // +30
		if (_yearMonthOffset > 7) return false;	//AlarmYear is out off range
		_yearMonthOffset = (_yearMonthOffset << 4) + (tm.Month - bcdToDec(data[0] & 0x1f));	// do not use the Century bit
		rv3028(RV3028_REG_GPBITS, _yearMonthOffset);

		setBit(RV3028_REG_CTRL1, CTRL1_WADA);
		data[0] = (decToBcd(tm.Minute));
		data[1] = (decToBcd(tm.Hour));
		data[2] = (decToBcd(tm.Day));
		rv3028wr(RV3028_REG_MINUTES_ALM, 3, data);
		enableInterrupt(Alarm2);
		return true;
	}

	//call once in alarm ISR
	bool RV3028::isMonthYearMatch() {
		uint8_t alarmYearMonth = rv3028(RV3028_REG_GPBITS);
		uint8_t alarmYear = alarmYearMonth >> 4;
		uint8_t alarmMonth = alarmYearMonth & 0x0F;
		
		if(alarmMonth == bcdToDec(rv3028(RV3028_REG_MONTHS))) {
			if(alarmYear > 0) {
				alarmYear--;
				//update alarmYearMonth in GP Reg
				alarmYearMonth = (alarmYear << 4) + alarmMonth;
				rv3028(RV3028_REG_GPBITS, alarmYearMonth);
				return false;
			} else {
				//alarmYear == 0 && alarmMonth == calendarMonth
				return true;
			}
		} else {
			return false;
		}
	}

	void RV3028::setAlarm1(Alarm1_t alarmType, uint8_t dayDate, uint8_t hour, uint8_t min, uint8_t sec, bool enableClockOut /* false */) {
		rv3028rd(RV3028_REG_SECONDS, 7, &data);
		tmElements_t tm;
		tm.Year   = y2kYearToTm(bcdToDec(data[6])); // +30
		tm.Month  = bcdToDec(data[5]);
		tm.Day    = bcdToDec(data[4]);
		tm.Wday   = bcdToDec(data[3]);
		tm.Hour   = bcdToDec(data[2]);
		tm.Minute = bcdToDec(data[1]);
		tm.Second = bcdToDec(data[0]);
		// Get alarm date not weekday
		uint8_t alarmDate = dayDate;
		if (alarmType == A1_WDHHMMSS){
			int8_t daysTillAlarm = dayDate - tm.Wday;
			if(daysTillAlarm < 0) daysTillAlarm += 7;
			tm.Day += daysTillAlarm;
			alarmDate = tm.Day;
		}

		if(alarmDate < tm.Day) tm.Month++;	//Alarm is next month
		else if(alarmDate == tm.Day){		//Alarm can be next month
			if(hour < tm.Hour) tm.Month++;
			else if (hour == tm.Hour){
				if(min < tm.Minute) tm.Month++;
				else if (min == tm.Minute){
					if(sec <= tm.Second) tm.Month++;
				}
			}
		}
		uint8_t clockBit;
		InterruptId_t intId;
		_alarm_mode = (uint8_t)alarmType;
		uint8_t dhm = _alarm_mode;
		if (_alarm_mode >= A2_WDHHMM){
			clearBit(RV3028_REG_CTRL1, CTRL1_WADA);
			dhm -= 10;
			intId = Alarm2;
			clockBit = IMT_MASK_CAIE;
		} else if (_alarm_mode == A2_ES){
			setUpdate(EverySecond);
			intId = TimeUpdate;
			clockBit = IMT_MASK_CUIE;
		} else {
			setBit(RV3028_REG_CTRL1, CTRL1_WADA);
			intId = Alarm2;
			clockBit = IMT_MASK_CAIE;
		}
		disableInterrupt(intId);
		clearInterruptFlag(intId);
		data[0] = decToBcd(min);
		data[1] = decToBcd(hour);
		data[2] = decToBcd(dayDate);
		if (dhm & 0x01) data[0] |= 1 << MINUTESALM_AE_M;
		if (dhm & 0x02) data[1] |= 1 << HOURSALM_AE_H;
		if (dhm & 0x04) data[2] |= 1 << DATE_AE_WD;
		rv3028wr(RV3028_REG_MINUTES_ALM, 3, data);
		enableInterrupt(intId);
		if (enableClockOut)
			setBit(RV3028_REG_INT_MASK, clockBit);
		else
			clearBit(RV3028_REG_INT_MASK, clockBit);
	}


	void RV3028::setAlarm2(Alarm2_t alarmType, uint8_t dayDate, uint8_t hour, uint8_t min, bool enableClockOut /* false */) {
		InterruptId_t intId;
		uint8_t clockBit;
		_alarm_mode = (uint8_t)alarmType;
		uint8_t dhm = _alarm_mode;
		if (_alarm_mode >= A2_WDHHMM){
			clearBit(RV3028_REG_CTRL1, CTRL1_WADA);
			dhm -= 10;
			intId = Alarm2;
			clockBit = IMT_MASK_CAIE;
		} else if (_alarm_mode == A2_ES){
			setUpdate(EverySecond);
			intId = TimeUpdate;
			clockBit = IMT_MASK_CUIE;
		} else if (_alarm_mode == A2_EM){
			setUpdate(EveryMinute);
			intId = TimeUpdate;
			clockBit = IMT_MASK_CUIE;
		} else {
			setBit(RV3028_REG_CTRL1, CTRL1_WADA);
			intId = Alarm2;
			clockBit = IMT_MASK_CAIE;
		}
		disableInterrupt(intId);
		clearInterruptFlag(intId);
		data[0] = decToBcd(min);
		data[1] = decToBcd(hour);
		data[2] = decToBcd(dayDate);
		if (dhm & 0x01) data[0] |= 1 << MINUTESALM_AE_M;
		if (dhm & 0x02) data[1] |= 1 << HOURSALM_AE_H;
		if (dhm & 0x04) data[2] |= 1 << DATE_AE_WD;
		rv3028wr(RV3028_REG_MINUTES_ALM, 3, data);
		enableInterrupt(intId);
		if (enableClockOut)
			setBit(RV3028_REG_INT_MASK, clockBit);
		else
			clearBit(RV3028_REG_INT_MASK, clockBit);
	}

	/*********************************
	Set the alarm mode in the following way:
	0: Match weekday/date, hours, minutes (once per weekday/date)
	1: Match weekday/date, hours (once per weekday/date)
	2: Match weekday/date, minutes (once per hour per weekday/date)
	3: Match weekday/date (once per weekday/date)
	4: Match hours, minutes (once per day)
	5: Match hours (once per day)
	6: Match minutes (once per hour)
	7: All disabled � Default value
	If you want to set a weekday alarm (dayNotDate = true), set 'dayDate' from 0 (Sunday) to 6 (Saturday)
	********************************/
	void RV3028::setAlarm(uint8_t mode, uint8_t dayDate, uint8_t hour, uint8_t min, bool dayNotDate /* false */, bool enableClockOut /* false */) {
		disableInterrupt(Alarm2);
		clearInterruptFlag(Alarm2);
		if (dayNotDate)
			clearBit(RV3028_REG_CTRL1, CTRL1_WADA);
		else
			setBit(RV3028_REG_CTRL1, CTRL1_WADA);
		data[0] = decToBcd(min);
		data[1] = decToBcd(hour);
		data[2] = decToBcd(dayDate);
		if (mode > 0b111) mode = 0b111;
		if (mode & 0b001) data[0] |= 1 << MINUTESALM_AE_M;
		if (mode & 0b010) data[1] |= 1 << HOURSALM_AE_H;
		if (mode & 0b100) data[2] |= 1 << DATE_AE_WD;
		rv3028wr(RV3028_REG_MINUTES_ALM, 3, data);
		enableInterrupt(Alarm2);
		if (enableClockOut)
			setBit(RV3028_REG_INT_MASK, IMT_MASK_CAIE);
		else
			clearBit(RV3028_REG_INT_MASK, IMT_MASK_CAIE);
	}

	void RV3028::setUpdate(TimeUpdate_t update /* EverySecond */, bool enableClockOut /* false*/){
		disableInterrupt(TimeUpdate);
		clearInterruptFlag(TimeUpdate);
		if (update == EverySecond)
			clearBit(RV3028_REG_CTRL1, CTRL1_USEL);
		else 
			setBit(RV3028_REG_CTRL1, CTRL1_USEL);
		setBit(RV3028_REG_CTRL2, CTRL2_UIE);
		if (enableClockOut)
			setBit(RV3028_REG_INT_MASK, IMT_MASK_CUIE);
		else
			clearBit(RV3028_REG_INT_MASK, IMT_MASK_CUIE);
	}

	void RV3028::setTimer(uint16_t timerValue, uint16_t timerFrequency /* 1 */, bool timerRepeat /* false */, bool startTimer /* true */, bool setInterrupt /* true */, bool enableClockOut /* false */){
		disableTimer();
		disableInterrupt(Timer);
		clearInterruptFlag(Timer);
		rv3028(RV3028_REG_TIMERVAL_0, timerValue & 0xff);
		rv3028(RV3028_REG_TIMERVAL_1, timerValue >> 8);
		uint8_t ctrl1 = rv3028(RV3028_REG_CTRL1);
		if (timerRepeat)
			ctrl1 |=   1 << CTRL1_TRPT;
		else
			ctrl1 &= ~(1 << CTRL1_TRPT);
		switch (timerFrequency){
			case 4096:		// 4096Hz (default)		// up to 122us error on first time
				ctrl1 &= ~3; // Clear both the bits
				break;
			case 64:		// 64Hz					// up to 7.813ms error on first time
				ctrl1 &= ~3; // Clear both the bits
				ctrl1 |= 1;
				break;
			case 1:			// 1Hz					// up to 7.813ms error on first time
				ctrl1 &= ~3; // Clear both the bits
				ctrl1 |= 2;
				break;
			case 60000:		// 1/60Hz				// up to 7.813ms error on first time
				ctrl1 |= 3; // Set both bits
				break;
		}
		if (setInterrupt){
			enableInterrupt(Timer);
		}
		if (startTimer){
			ctrl1 |= (1 << CTRL1_TE);
		}
		rv3028(RV3028_REG_CTRL1, ctrl1);
		if (enableClockOut)
			setBit(RV3028_REG_INT_MASK, IMT_MASK_CTIE);
		else
			clearBit(RV3028_REG_INT_MASK, IMT_MASK_CTIE);
	}

	void RV3028::enableTimer(){
		setBit(RV3028_REG_CTRL1, CTRL1_TE);
	}

	void RV3028::disableTimer(){
		clearBit(RV3028_REG_CTRL1, CTRL1_TE);
	}

	void RV3028::setSquareWave(SquareWave_t squareWave){
		uint8_t EEPROMClkout = readConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_CLKOUT);
		EEPROMClkout |= (uint8_t)squareWave << EEPROMClkout_FREQ_SHIFT;
		writeConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_CLKOUT, EEPROMClkout);
		if(squareWave == PeriodicCountDown)
			setBit(RV3028_REG_CTRL2, CTRL2_CLKIE);
	}

	void RV3028::enableClockOut(bool enable){
		uint8_t EEPROMClkout = readConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_CLKOUT);
		if (enable){
			EEPROMClkout |= 1 << EEPROMClkout_CLKOE_BIT;
		} else {
			clearBit(RV3028_REG_CTRL2, CTRL2_CLKIE);
			EEPROMClkout &= ~(1 << EEPROMClkout_CLKOE_BIT);
		}
		writeConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_CLKOUT, EEPROMClkout);
	}

	bool RV3028::readClockFlag(){
		return readBit(RV3028_REG_STATUS, STATUS_CLKF);
	}

	void RV3028::clearClockFlag(){
		clearBit(RV3028_REG_STATUS, STATUS_CLKF);
	}

	void RV3028::enableInterrupt(InterruptId_t intId){
		switch(intId){
			case Alarm1:
				setBit(RV3028_REG_CTRL2, CTRL2_TIE);
				break;
			case Alarm2:
				setBit(RV3028_REG_CTRL2, CTRL2_AIE);
				break;
			case Timer:
				setBit(RV3028_REG_CTRL2, CTRL2_TIE);
				break;
			case TimeUpdate:
				setBit(RV3028_REG_CTRL2, CTRL2_UIE);
				break;
		}
	}

	//Only disables the interrupt (not the alarm flag)
	void RV3028::disableInterrupt(InterruptId_t intId){
		switch(intId){
			case Alarm1:
				clearBit(RV3028_REG_CTRL2, CTRL2_TIE);
				break;
			case Alarm2:
				clearBit(RV3028_REG_CTRL2, CTRL2_AIE);
				break;
			case Timer:
				clearBit(RV3028_REG_CTRL2, CTRL2_TIE);
				break;
			case TimeUpdate:
				clearBit(RV3028_REG_CTRL2, CTRL2_UIE);
				break;
		}
	}

	bool RV3028::readInterruptFlag(InterruptId_t intId){
		switch(intId){
			case Alarm1:
				return readBit(RV3028_REG_STATUS, STATUS_TF);
			case Alarm2:
				return readBit(RV3028_REG_STATUS, STATUS_AF);
			case Timer:
				return readBit(RV3028_REG_STATUS, STATUS_TF);
			case TimeUpdate:
				return readBit(RV3028_REG_STATUS, STATUS_UF);
		}
		return true;
	}

	void RV3028::clearInterruptFlag(InterruptId_t intId){
		switch(intId){
			case Alarm1:
				clearBit(RV3028_REG_STATUS, STATUS_TF);
				break;
			case Alarm2:
				clearBit(RV3028_REG_STATUS, STATUS_AF);
				break;
			case Timer:
				clearBit(RV3028_REG_STATUS, STATUS_TF);
				break;
			case TimeUpdate:
				clearBit(RV3028_REG_STATUS, STATUS_UF);
				break;
		}
	}

	void RV3028::setTrickleCharge(TrickleCharge_t trickCharge){
		uint8_t EEPROMBackup = readConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_BACKUP);
		EEPROMBackup &= EEPROMBackup_TCR_CLEAR; //TCR returned to default 3K
		if(trickCharge == TC_OFF)
			EEPROMBackup &= ~(1 << EEPROMBackup_TCE_BIT);
		else
			EEPROMBackup |=   1 << EEPROMBackup_TCE_BIT;
		EEPROMBackup |= (uint8_t)trickCharge << EEPROMBackup_TCR_SHIFT;
		writeConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_BACKUP, EEPROMBackup);
	}

	bool RV3028::setBackupSwitchover(SwitchOver_t switchOver){
		bool success = true;
		uint8_t EEPROMBackup = readConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_BACKUP);
		if (EEPROMBackup == 0xFF)
			success = false;
		EEPROMBackup |= 1 << EEPROMBackup_FEDE_BIT;
		EEPROMBackup &= EEPROMBackup_BSM_CLEAR;
		EEPROMBackup |= (uint8_t)switchOver << EEPROMBackup_BSM_SHIFT;
		if (!writeConfigEEPROM_RAMmirror(RV3028_REG_EEPROM_BACKUP, EEPROMBackup)) 
			success = false;
		return success;
	}

	//Returns HW Id register
	uint8_t RV3028::reset(void){
		setBit(RV3028_REG_CTRL2, CTRL2_RESET);
		return rv3028(RV3028_REG_ID);
	}

	//Returns the status byte
	uint8_t RV3028::status(void){
		return(rv3028(RV3028_REG_STATUS));
	}

	//Read the status register to clear the current interrupt flags
	void RV3028::clearInterrupts(void){
		rv3028(RV3028_REG_STATUS, 0);
	}

	//Set the EEOffset register
	bool RV3028::setAgingOffset(int8_t val) {
		int regVal = (int) val;
		regVal = regVal <= 0 ? -regVal : (512 - regVal);
		regVal >>= 1;
		regVal = MIN(regVal, UINT8_MAX);  // not needed because it's guaranted to be within the range
		return rv3028(RV3028_REG_EEOffset_8_1, (uint8_t) regVal);
	}

	int8_t RV3028::getAgingOffset() {
		uint8_t regVal = rv3028(RV3028_REG_EEOffset_8_1);
		int regFull = ((int) regVal) << 1;
		if (regFull <= 128) {
			return -((int8_t) regFull);
		}
		regFull = MIN(512 - MIN(regFull, UINT8_MAX), INT8_MAX);
		return (int8_t) regFull;
	}

	uint8_t RV3028::bcdToDec(uint8_t bcd) {
		return (uint8_t)(10 * ((bcd & 0xF0) >> 4) + (bcd & 0x0F));
	}

	uint8_t RV3028::decToBcd(uint8_t dec) {
		return (uint8_t)(((dec / 10) << 4) | (dec % 10));
	}

	// Read register
	uint8_t RV3028::rv3028(uint8_t reg) {
		_i2cPort->beginTransmission((uint8_t)adrRV3028);
		_i2cPort->write(reg);
		_i2cPort->endTransmission();
		_i2cPort->requestFrom((uint8_t)adrRV3028, uint8_t(1));
		return _i2cPort->read();
	}

	// Write to RTC register
	bool RV3028::rv3028(uint8_t reg, uint8_t value) {
		_i2cPort->beginTransmission((uint8_t)adrRV3028);
		_i2cPort->write(reg);
		_i2cPort->write(value);
		return (_i2cPort->endTransmission() == 0);
	}

	// Read data from RTC
	bool RV3028::rv3028rd(uint8_t reg, uint8_t len, void *data) {
		_i2cPort->beginTransmission((uint8_t)adrRV3028);
		_i2cPort->write(reg);
		_i2cPort->endTransmission(false);
		_i2cPort->requestFrom((uint8_t)adrRV3028, len);
		for (uint8_t i = 0; i < len; i++) ((uint8_t *)data)[i] = (uint8_t)_i2cPort->read();
		return (_i2cPort->endTransmission() == 0);
	}

	// Write data to RTC
	bool RV3028::rv3028wr(uint8_t reg, uint8_t len, void *data) {
		_i2cPort->beginTransmission((uint8_t)adrRV3028);
		_i2cPort->write(reg);
		for (uint8_t i = 0; i < len; i++) _i2cPort->write(((uint8_t *)data)[i]);
		return (_i2cPort->endTransmission() == 0);
	}

	bool RV3028::writeConfigEEPROM_RAMmirror(uint8_t eepromaddr, uint8_t val){
		bool success = waitforEEPROM();

		//Disable auto refresh by writing 1 to EERD control bit in CTRL1 register
		uint8_t ctrl1 = rv3028(RV3028_REG_CTRL1);
		ctrl1 |= 1 << CTRL1_EERD;
		if (!rv3028(RV3028_REG_CTRL1, ctrl1)) success = false;
		//Write Configuration RAM Register
		rv3028(eepromaddr, val);
		//Update EEPROM (All Configuration RAM -> EEPROM)
		rv3028(RV3028_REG_EEPROM_CMD, EEPROMCMD_First);
		rv3028(RV3028_REG_EEPROM_CMD, EEPROMCMD_Update);
		if (!waitforEEPROM()) success = false;
		//Reenable auto refresh by writing 0 to EERD control bit in CTRL1 register
		ctrl1 = rv3028(RV3028_REG_CTRL1);
		if (ctrl1 == 0x00)success = false;
		ctrl1 &= ~(1 << CTRL1_EERD);
		rv3028(RV3028_REG_CTRL1, ctrl1);
		if (!waitforEEPROM()) success = false;

		return success;
	}

	uint8_t RV3028::readConfigEEPROM_RAMmirror(uint8_t eepromaddr){
		bool success = waitforEEPROM();

		//Disable auto refresh by writing 1 to EERD control bit in CTRL1 register
		uint8_t ctrl1 = rv3028(RV3028_REG_CTRL1);
		ctrl1 |= 1 << CTRL1_EERD;
		if (!rv3028(RV3028_REG_CTRL1, ctrl1)) success = false;
		//Read EEPROM Register
		rv3028(RV3028_REG_EEPROM_ADDR, eepromaddr);
		rv3028(RV3028_REG_EEPROM_CMD, EEPROMCMD_First);
		rv3028(RV3028_REG_EEPROM_CMD, EEPROMCMD_ReadSingle);
		if (!waitforEEPROM()) success = false;
		uint8_t eepromdata = rv3028(RV3028_REG_EEPROM_DATA);
		if (!waitforEEPROM()) success = false;
		//Reenable auto refresh by writing 0 to EERD control bit in CTRL1 register
		ctrl1 = rv3028(RV3028_REG_CTRL1);
		if (ctrl1 == 0x00)success = false;
		ctrl1 &= ~(1 << CTRL1_EERD);
		rv3028(RV3028_REG_CTRL1, ctrl1);

		if (!success) return 0xFF;
		return eepromdata;
	}

	//True if success, false if timeout occured
	bool RV3028::waitforEEPROM(){
		unsigned long timeout = millis() + 500;
		while ((rv3028(RV3028_REG_STATUS) & 1 << STATUS_EEBUSY) && millis() < timeout);

		return millis() < timeout;
	}

	void RV3028::setBit(uint8_t reg, uint8_t bitNum){
		uint8_t value = rv3028(reg);
		value |= (1 << bitNum);
		rv3028(reg, value);
	}

	void RV3028::clearBit(uint8_t reg, uint8_t bitNum){
		uint8_t value = rv3028(reg);
		value &= ~(1 << bitNum);
		rv3028(reg, value);
	}

	bool RV3028::readBit(uint8_t reg, uint8_t bitNum){
		uint8_t value = rv3028(reg);
		value &= (1 << bitNum);
		return value;
	}

RV3028 RTC;

#elif defined (EZTIME_BM8563_ENABLE) || defined (ARDUINO_FROG_ESP32)	//K46v4
#include <Wire.h>

	// Initialize and detect BM8563 RTC
	bool BM8563::begin(TwoWire &wirePort /* = Wire */) {
		_i2cPort = &wirePort;
		_i2cPort->beginTransmission(adrBM8563);
		return (_i2cPort->endTransmission() ==  0);
	}

	// Read RTC VolageLow Flag and optionally clears it
	timeStatus_t BM8563::timeStatus(bool clearVL /* = false */) {
		uint8_t vldReg = bm8563rd(BM8563_REG_SEC);
		vldReg & BM8563_VOLT_LOW_MASK)? _rtc_status = timeNotSet : _rtc_status = timeSet;
		if (clearМД){
			vldReg &= ~(BM8563_VOLT_LOW_MASK);
			bm8563(BM8563_REG_SEC, vldReg);
		}
		return _rtc_status;
	}

	// Set the RTC to the given time_t value and clear the
	// OSF in the Control/Status register.
	// Set the RTC to the given time_t value and clear the
	// PORF in the Status register.
	bool BM8563::setTime(time_t t, bool syncCalendar /* = true */) {
		tmElements_t tm;
		if (t < 946684800) {
			t = 946684800; // 2000-01-01 00:00:00
		}
		ezt::breakTime(t, tm);
		return setTime(tm);
	}

	// year in 2000-2099 range can be given as full four digit year or two digts (2022 or 22 for 2022);
	bool BM8563::setTime(const uint8_t hr, const uint8_t min, const uint8_t sec, const uint8_t day, const uint8_t mnth, uint16_t yr, bool syncEpoch /* = true */) {
		tmElements_t tm;
		if( yr > 99) {
			yr = yr - 1970;
		} else {
			yr += 30; 
		}
		// if(yr <= 99) {
		// 	// it is converted to years since 1970
		// 	yr += 30; 
		// } else if ((yr < 2000) || (yr > 2099)) {
		// 	return false;
		// } else {
		// 	yr -= 1970;
		// }
		tm.Year = yr;
		tm.Month = mnth;
		tm.Day = day;
		tm.Hour = hr;
		tm.Minute = min;
		tm.Second = sec;
		if ((tm.Second > 59) || 
			(tm.Minute > 59) || (tm.Hour > 23)  ||        
			(tm.Day < 1)     || (tm.Day > 31)   ||
			(tm.Month < 1)   || (tm.Month > 12) ||
			(tm.Year < 30)   || (tm.Year > 129)) {
			return false;
		}
		ezt::breakTime(ezt::makeTime(tm), tm);
		return setTime(tm);
	}

	// Set the RTC time from a tmElements_t structure and clear the
	// oscillator stop flag (OSF) in the Control/Status register
	bool BM8563::setTime(tmElements_t &tm) {
		data[0] = (decToBcd(tm.Second)) & (~BM8563_VOL_LOW_MASK);
		data[1] = (decToBcd(tm.Minute));
		data[2] = (decToBcd(tm.Hour));
		data[3] = (decToBcd(tm.Day));
		data[4] = tm.Wday - 1;
		data[5] = (decToBcd(tm.Month));
		data[6] = (decToBcd(tm.Year % 100));
		if ((2000 % year) == 2000) {
			data[5] &= (~BM8563_CENTURY_MASK);
		} else {
			data[5] |= BM8563_CENTURY_MASK;
		}
		// Get the nearest micros when tm.Second is writen to RTC
		// Next second mark should be 500us later
		_rtc_set_micros = micros();
		// Write BCD encoded data to RTC registers
		bm8563wr(BM8563_REG_SEC, 7, data);
		enableOsc(true);
		_rtc_set_time = ezt::makeTime(tm);
		_rtc_status = timeSet;
		// Clear oscillator halt flag
		return (timeStatus(true) == timeSet ? true : false);
	}

	// Return time and micros RTC time was set 
	time_t BM8563::getSetTime(uint64_t &micros) {
		micros = _rtc_set_micros;
		return _rtc_set_time;
	}

	// Read the current time from the RTC
	// and return it as a time_t value. 
	// Returns false if an invalid date/time format was read from the RTC.
	time_t BM8563::now(bool getCalendar /* = false */) {
		tmElements_t tm;
		uint16_t year;
		uint8_t cetury = 0;
		bm8563rd(BM8563_REG_SEC, 7, data);
		_voltageLow = (data[0] & BM8563_VOL_LOW_MASK);
		tm.Second = bcdToDec(data[0] & (~BM8563_VOL_LOW_MASK));
		tm.Minute = bcdToDec(data[1] & BM8563_MIN_MASK);
		tm.Hour   = bcdToDec(data[2] & BM8563_HOUR_MASK);
		tm.Day    = bcdToDec(data[3] & BM8563_DAY_MASK);
		tm.Wday   = bcdToDec(data[4] & BM8563_WEEKDAY_MASK);
		cetury    = data[5] & BM8563_CENTURY_MASK;
		tm.Month  = bcdToDec(data[5] & BM8563_MONTH_MASK);
		// tm.Year = _bcd_to_dec(data[6]);
		// //cetury :  0 = 1900 , 1 = 2000
		// tm.Year = cetury ?  1900 + tm.Year : 2000 + tm.Year;
		tm.Year   = y2kYearToTm(bcdToDec(data[6])); // +30
		if ((tm.Second > 59) || 
			(tm.Minute > 59) || (tm.Hour > 23)  ||
			(tm.Wday < 1)    || (tm.Wday > 7)   ||        
			(tm.Day < 1)     || (tm.Day > 31)   ||
			(tm.Month < 1)   || (tm.Month > 12) ||
			(tm.Year < 30)   || (tm.Year > 129))
			return 0;
		return (ezt::makeTime(tm));
	}

	void BM8563::setAlarm2(Alarm2_t alarmType, uint8_t dayDate, uint8_t hour, uint8_t min, bool enableClockOut /* false */)
	{
		data[0] = decToBcd(constrain(min, 0, 59));
		data[1] = decToBcd(constrain(hour, 0, 23));
		data[2] = decToBcd(constrain(dayDate, 1, 31));
		data[3] = decToBcd(constrain(dayDate, 0, 6));
		switch (alarmType){
			case A2_DDHHMM: //When date, hours and minutes match (once per date)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_DDHH:   //When date and hours match (once per month @hour)
				data[0] !=  BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_DDMM:   //When date and minutes match (once per month @minute)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] !=  BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_DD:     //When date match (once per month)
				data[0] !=  BM8563_ALARM_ENABLE;
				data[1] !=  BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_HHMM:   //When hours and minutes match (once per day)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_HH:     //When hours match (once per day)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_MM:     //When minutes match (once per hour)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] !=  BM8563_ALARM_ENABLE;
				break;
			case A2_OFF:    //Alarm disabled - Default value
				data[0] != BM8563_ALARM_ENABLE;
				data[1] != BM8563_ALARM_ENABLE;
				data[2] != BM8563_ALARM_ENABLE;
				data[3] != BM8563_ALARM_ENABLE;
				break;
			case A2_EM:     //Alarm every minute
				bm8563rd(BM8563_REG_STAT2, 1, data);
				data[0] |= 1 << STATUS2_TI_TP;
				bm8563wr(BM8563_REG_STAT2, 1, data);
				data[0] = 1;
				bm8563wr(BM8563_REG_TIMER2, 1, data)
				bm8563rd(BM8563_REG_TIMER1, 1, data);
				data[0] |= 0x83;	//TE | 1/60Hz
				bm8563wr(BM8563_REG_TIMER1, 1, data);
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] &= ~BM8563_ALARM_ENABLE;
				break;
			case A2_ES:     //Alarm every second
				bm8563rd(BM8563_REG_STAT2, 1, data);
				data[0] |= 1 << STATUS2_TI_TP;
				bm8563wr(BM8563_REG_STAT2, 1, data);
				data[0] = 1;
				bm8563wr(BM8563_REG_TIMER2, 1, data)
				bm8563rd(BM8563_REG_TIMER1, 1, data);
				data[0] |= 0x82;	//TE | 1Hz
				bm8563wr(BM8563_REG_TIMER1, 1, data);
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] &= ~BM8563_ALARM_ENABLE;
				data[3] &= ~BM8563_ALARM_ENABLE;
				break;
			case A2_WDHHMM: //When weekday, hours and minutes match (once per week @hour&min)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] !=  BM8563_ALARM_ENABLE;
				data[3] &= ~BM8563_ALARM_ENABLE;
				break;
			case A2_WDHH:   //When weekday and hours match (once per week @hour)
				data[0] !=  BM8563_ALARM_ENABLE;
				data[1] &= ~BM8563_ALARM_ENABLE;
				data[2] !=  BM8563_ALARM_ENABLE;
				data[3] &= ~BM8563_ALARM_ENABLE;
				break;
			case A2_WDMM:   //When weekday and minutes match (once per week @minute)
				data[0] &= ~BM8563_ALARM_ENABLE;
				data[1] !=  BM8563_ALARM_ENABLE;
				data[2] !=  BM8563_ALARM_ENABLE;
				data[3] &= ~BM8563_ALARM_ENABLE;
				break;
			case A2_WD:     //When weekday match (once per week)
				data[0] !=  BM8563_ALARM_ENABLE;
				data[1] !=  BM8563_ALARM_ENABLE;
				data[2] !=  BM8563_ALARM_ENABLE;
				data[3] &= ~BM8563_ALARM_ENABLE;
				break;
		}
		bm8563wr(BM8563_REG_ALRM_MIN, 4, data);
	}

	void BM8563::setTimer(uint8_t timerValue, uint8_t timerFrequency /* 1 */, bool setInterrupt /* true */, bool enableClockOut /* false */){
		// disableTimer();
		// disableInterrupt(Timer);
		// clearInterruptFlag(Timer);
		bm8563rd(BM8563_REG_STAT2, 1, data);
		if (setInterrupt) {
			data[0] |= 1 << STATUS2_TI_TP;
		} else {
			data[0] &= ~(1 << STATUS2_TI_TP);
		}
		bm8563wr(BM8563_REG_STAT2, 1, data);
		bm8563rd(BM8563_REG_TIMER1, 1, data);
		data[0] |= (timerFrequency & BM8563_TIMER_TD10);
		bm8563wr(BM8563_REG_TIMER1, 1, data);
		data[0] = timerValue;
		bm8563wr(BM8563_REG_TIMER2, 1, data);
	}

	void BM8563::enableTimer(){
		bm8563rd(BM8563_REG_STAT2, 1, data);
		data[0] &= ~BM8563_TIMER_TF;
		data[0] |= (BM8563_ALARM_AF | BM8563_TIMER_TIE);
		bm8563wr(BM8563_REG_STAT2, 1, data);
		bm8563rd(BM8563_REG_TIMER1, 1, data);
		data[0] |= BM8563_TIMER_TE;
		bm8563wr(BM8563_REG_TIMER1, 1, data);
	}

	void BM8563::disableTimer(){
		bm8563(BM8563_REG_STAT2);
		data[0] &= ~BM8563_TIMER_TF;
		data[0] |= BM8563_ALARM_AF;
		bm8563(BM8563_REG_STAT2, data);
	}

	void BM8563::setSquareWave(SquareWave_t squareWave, bool enableClockOut /* false */){
		if ((uint8_t)squareWave > SquareWave1H) return false;
		data[0] = (uint8_t)squareWave;
		if(enableClockOut)
			m8563(BM8563_REG_SQW, data[0] | BM8563_CLK_ENABLE);
		bm8563(BM8563_REG_SQW, data);
	}

	void BM8563::enableClockOut(bool enable){
		if(enable)
			m8563(BM8563_REG_SQW, bm8563(BM8563_REG_SQW) | BM8563_CLK_ENABLE);
		else
			bm8563(BM8563_REG_SQW, bm8563(BM8563_REG_SQW) & ~BM8563_CLK_ENABLE);
	}

	void BM8563::enableInterrupt(InterruptId_t intId){
		switch(intId){
			case Alarm1:
			case Alarm2:
				setBit(BM8563_REG_STAT2, STATUS2_AIE);
				break;
			case Timer:
				setBit(BM8563_REG_STAT2, STATUS2_TIE);
				break;
		}
	}

	//Only disables the interrupt (not the alarm flag)
	void BM8563::disableInterrupt(InterruptId_t intId){
		switch(intId){
			case Alarm1:
			case Alarm2:
				clearBit(BM8563_REG_STAT2, STATUS2_AIE);
				break;
			case Timer:
				clearBit(BM8563_REG_STAT2, STATUS2_TIE);
				break;
		}
	}

	bool BM8563::readInterruptFlag(InterruptId_t intId){
		switch(intId){
			case Alarm1:
			case Alarm2:
				return readBit(BM8563_REG_STAT2, STATUS2_AF);
			case Timer:
				return readBit(BM8563_REG_STAT2, STATUS2_TF);
		}
		return true;
	}

	void BM8563::clearInterruptFlag(InterruptId_t intId){
		switch(intId){
			case Alarm1:
			case Alarm2:
				clearBit(BM8563_REG_STAT2, STATUS2_AF);
				break;
			case Timer:
				clearBit(BM8563_REG_STAT2, STATUS2_TF);
				break;
		}
	}

	//Read the status register to clear the current interrupt flags
	void BM8563::clearInterrupts(void){
		bm8563(BM8563_REG_STAT2, bm8563(BM8563_REG_STAT2) & 0xF3);
	}

	//Returns the status byte
	uint8_t BM8563::status(void){
		return bm8563(BM8563_REG_STAT2);
	}

	uint8_t BM8563::bcdToDec(uint8_t bcd) {
		return (uint8_t)(10 * ((bcd & 0xF0) >> 4) + (bcd & 0x0F));
	}

	uint8_t BM8563::decToBcd(uint8_t dec) {
		return (uint8_t)(((dec / 10) << 4) | (dec % 10));
	}

	// Read register
	uint8_t BM8563::bm8563(uint8_t reg) {
		_i2cPort->beginTransmission((uint8_t)adrBM8563);
		_i2cPort->write(reg);
		_i2cPort->endTransmission();
		_i2cPort->requestFrom((uint8_t)adrBM8563, uint8_t(1));
		return _i2cPort->read();
	}

	// Write to RTC register
	bool BM8563::bm8563(uint8_t reg, uint8_t value) {
		_i2cPort->beginTransmission((uint8_t)adrBM8563);
		_i2cPort->write(reg);
		_i2cPort->write(value);
		return (_i2cPort->endTransmission() == 0);
	}

	// Read data from RTC
	bool BM8563::bm8563rd(uint8_t reg, uint8_t len, void *data) {
		_i2cPort->beginTransmission((uint8_t)adrBM8563);
		_i2cPort->write(reg);
		_i2cPort->endTransmission(false);
		_i2cPort->requestFrom((uint8_t)adrBM8563, len);
		for (uint8_t i = 0; i < len; i++) ((uint8_t *)data)[i] = (uint8_t)_i2cPort->read();
		return (_i2cPort->endTransmission() == 0);
	}

	// Write data to RTC
	bool BM8563::bm8563wr(uint8_t reg, uint8_t len, void *data) {
		_i2cPort->beginTransmission((uint8_t)adrBM8563);
		_i2cPort->write(reg);
		for (uint8_t i = 0; i < len; i++) _i2cPort->write(((uint8_t *)data)[i]);
		return (_i2cPort->endTransmission() == 0);
	}

	void BM8563::setBit(uint8_t reg, uint8_t bitNum){
		uint8_t value = bm8563(reg);
		value |= (1 << bitNum);
		bm8563(reg, value);
	}

	void BM8563::clearBit(uint8_t reg, uint8_t bitNum){
		uint8_t value = bm8563(reg);
		value &= ~(1 << bitNum);
		bm8563(reg, value);
	}

	bool BM8563::readBit(uint8_t reg, uint8_t bitNum){
		uint8_t value = bm8563(reg);
		value &= (1 << bitNum);
		return value;
	}

BM8563 RTC;

#endif	// EZTIME_BM8563_ENABLE
