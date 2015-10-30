#if (PRODUCT_ID==6) || (PRODUCT_ID==8) || (PRODUCT_ID==10) // 6 = Photon, 8 = P1, 10 = Electron

#define ENABLED_SYSTEM_THREAD

// Handle cloudy stuff in another thread
SYSTEM_THREAD(ENABLED);

#else

// Start up in SEMI_AUTOMATIC mode to be able to display
// rainbows while trying to connect to wifi.
SYSTEM_MODE(SEMI_AUTOMATIC);

#endif

#define SERIAL_DEBUG


#define	ROWS	11
#define COLS	11


// NeoPixel init stuffs
#include "neopixel.h"
#define PIXEL_COUNT ROWS*COLS
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, D2, WS2811);


// Timers
#include "elapsedMillis.h"
elapsedMillis elapsedRainbow;


// DHT22 Sensor
// #define ENABLE_DHT
#define DHT_PIN		D2
#define DHT_TYPE	DHT22

#ifdef ENABLE_DHT
#include "PietteTech_DHT.h"
void dht_wrapper();
void doDHT();

PietteTech_DHT DHT(DHT_PIN, DHT_TYPE, dht_wrapper);

elapsedMillis elapsedDHT22;
bool dhtStarted = false;
double dhtFahrenheit = 0;
double dhtHumidity = 0;
double dhtDewPoint = 0;
bool dhtIsGood = false;
#endif


// Enable cloud publishing
#define ENABLE_PUB
#define DEVICE_NAME		"wc2"
#define PUB_EVENT		"statsd"
#define PUB_TTL			60
#define	PUB_SCOPE		PUBLIC
#define PUB_INTERVAL	60*1000

#ifdef ENABLE_PUB
elapsedMillis elapsedPub;
uint16_t intervalPub = PUB_INTERVAL;
void doPub();
#endif


#ifdef ENABLEPUB
#if PRODUCT_ID==10
PMIC PMIC;
FuelGauge fuel;

elapsedMillis elapsedFuel;
#define FUEL_INTERVAL   5*1000

double fuelSOC = 0;
double fuelVcell = 0;

void doFuel();
#endif
#endif


// Enable LED mirroring
#define LED_MIRROR


// The physical text of the word clock
static const String text = "\
IT.IS.HALF.\
TEN.QUARTER\
TWENTY.FIVE\
MINUTES.TO.\
.PAST.SEVEN\
.NINE.FIVE.\
.EIGHT.TEN.\
THREE.FOUR.\
.TWELVE.TWO\
ELEVEN.ONE.\
SIX.O'CLOCK\
";


// Get the location of the status pixel (the apostrophe)
static const uint8_t status_pixel = 5; //text.indexOf("'");
uint8_t status_color[3] = {0, 0, 0};
uint8_t rainbow_cycle = 0;


// Function prototypes in case they're needed
// void rainbow(uint8_t wait);
uint32_t Wheel(byte WheelPos);
void blackOut();
void doWord(String word);
void doWord(String word, bool skip);
uint8_t xyToPixel(uint8_t x, uint8_t y);
void ticktock();
void applyRainbow();
void randomColor();
void doEffectMode();
int functionHandler(String command);


// EEPROM
// Address 0 = 117 if values have been saved
// Address 1 = 0/1 for -/+ of time zone offset
// Address 2 = Time zone offset (positive integer)
// Address 3 = 12/24 for hour format <-- Not used here
// Address 4 = Effect mode
// Address 5 = Red
// Address 6 = Green
// Address 7 = Blue
// Address 8 = Rainbow delay

int8_t timeZone = 0;
bool time12Hour = false;
bool resetFlag = false;
elapsedMillis timerReset = 0;
uint8_t color[3] = {0, 0, 64};


// Effect modes
// 0 = no effect
// 1 = rainbow
uint8_t EFFECT_MODE = 0;
uint8_t currEffect = EFFECT_MODE;
uint16_t RAINBOW_DELAY = 50;
int8_t LAST_MINUTE = -1;


// LED mirroring
#ifdef LED_MIRROR
void ledChangeHandler(uint8_t r, uint8_t g, uint8_t b) {
    status_color[0] = r;
    status_color[1] = g;
    status_color[2] = b;
}
#endif


void setup() {
#ifdef LED_MIRROR
    RGB.onChange(ledChangeHandler);
#endif

#ifdef SERIAL_DEBUG
    Serial.begin(9600);
#endif


	// Set all timers
    elapsedRainbow = 0;
#ifdef ENABLE_DHT
    elapsedDHT = 0;
#endif
#ifdef ENABLE_PUB
	elapsedPub = 0;
#endif


	// Initialize NeoPixels
    strip.begin();
    strip.show();


    // Declare any cloud variables we want to expose
#ifdef ENABLE_DHT
    Particle.variable("Fahrenheit", &dhtFahrenheit, DOUBLE);
    Particle.variable("Humidity", &dhtHumidity, DOUBLE);
    Particle.variable("DewPoint", &dhtDewPoint, DOUBLE);
#endif


	// Declare our cloud function handler
	Particle.function("function", functionHandler);


    // Connect to the cloud
#ifndef ENABLED_SYSTEM_THREAD
    Particle.connect();
#endif


    // We have to turn on all the pixels for the rainbow to work
    for(uint8_t i=0; i<ROWS*COLS; i++)
    	strip.setPixelColor(i, strip.Color(0, 0, 1));

	// Wait for the cloud connection to happen
    while(!Particle.connected()) {
        // And do a little rainbow dance while we wait
        elapsedRainbow = RAINBOW_DELAY; // A little trick to beat the rainbow timer
        applyRainbow();
        strip.setPixelColor(status_pixel, status_color[0], status_color[1], status_color[2]);
        strip.show();
        delay(10);

#ifndef ENABLED_SYSTEM_THREAD
        Particle.process();
#endif
    }


    // Do a "wipe" to clear away the rainbow
    for(uint8_t i=0; i<PIXEL_COUNT; i++) {
        strip.setPixelColor(i, strip.Color(i, i, i));

        if(i>0)
            strip.setPixelColor(i-1, strip.Color(0, 0, 0));

        strip.show();
        delay(5);
    }


	// See if this EEPROM has saved data
    if(EEPROM.read(0)==117) {
        // Set the time zone
        if(EEPROM.read(1)==0)
            timeZone = EEPROM.read(2)*-1;
        else
            timeZone = EEPROM.read(2);

        EFFECT_MODE = EEPROM.read(4);

        color[0] = EEPROM.read(5);
        color[1] = EEPROM.read(6);
        color[2] = EEPROM.read(7);

        RAINBOW_DELAY = EEPROM.read(8);

    // If data has not been saved, "initialize" the EEPROM
    } else {
        // Initialize
        EEPROM.write(0, 117);
        // Time zone +/-
        EEPROM.write(1, 0);
        // Time zone
        EEPROM.write(2, 0);
        // Effect mode
        EEPROM.write(4, 0);
        // Red
        EEPROM.write(5, 0);
        // Green
        EEPROM.write(6, 255);
        // Blue
        EEPROM.write(7, 128);
        // Rainbow delay
        EEPROM.write(8, RAINBOW_DELAY);
    }


    // Set the timezone
    Time.zone(timeZone);


    // Blank slate
    blackOut();
    strip.show();
}


void loop() {
    ticktock();
    doEffectMode();

#ifdef LED_MIRROR
    strip.setPixelColor(status_pixel, status_color[0], status_color[1], status_color[2]);
#endif

#ifdef ENABLE_DHT
	doDHT();
#endif

#ifdef ENABLE_PUB
	doPub();
#endif

	// Handle remote reset
    if(timerReset>=500) {
        if(resetFlag) {
            System.reset();
            resetFlag = false;
        }

        timerReset = 0;
    }

    strip.show();

#ifndef ENABLED_SYSTEM_THREAD
	Particle.process();
#endif
	delay(10);
}


// Function handler for cloud function calls
int functionHandler(String command) {
    command.trim();
    command.toUpperCase();

    // Get time zone offset
    if(command.equals("GETTIMEZONE")) {
        return timeZone;


    // Set time zone offset
    } else if(command.substring(0, 12)=="SETTIMEZONE,") {
        timeZone = command.substring(12).toInt();
        Time.zone(timeZone);

        if(timeZone>-1) {
            EEPROM.write(1, 1);
            EEPROM.write(2, timeZone);
        } else {
            EEPROM.write(1, 0);
            EEPROM.write(2, timeZone * -1);
        }

        LAST_MINUTE = -1; // Set this to force the clock to update

        return timeZone;


    // Lazy way to reboot
    } else if(command.equals("REBOOT")) {
        resetFlag = true;
        return 1;


    // Set red
    } else if(command.substring(0, 7)=="SETRED,") {
        color[0] = command.substring(7).toInt();
        EEPROM.write(5, color[0]);
        EFFECT_MODE = 0;
        LAST_MINUTE = -1; // Set this to force the clock to update

        return color[0];


    // Set green
    } else if(command.substring(0, 9)=="SETGREEN,") {
        color[1] = command.substring(9).toInt();
        EEPROM.write(6, color[1]);
        EFFECT_MODE = 0;
        LAST_MINUTE = -1; // Set this to force the clock to update

        return color[1];


    // Set blue
    } else if(command.substring(0, 8)=="SETBLUE,") {
        color[2] = command.substring(8).toInt();
        EEPROM.write(7, color[2]);
        EFFECT_MODE = 0;
        LAST_MINUTE = -1; // Set this to force the clock to update

        return color[2];


    // Set RGB
    } else if(command.substring(0, 7)=="SETRGB,") {
        color[0] = command.substring(7, 10).toInt();
        color[1] = command.substring(11, 14).toInt();
        color[2] = command.substring(15, 18).toInt();

        EEPROM.write(5, color[0]);
        EEPROM.write(6, color[1]);
        EEPROM.write(7, color[2]);

        EFFECT_MODE = 0;
        LAST_MINUTE = -1; // Set this to force the clock to update

        return 1;


    // Random color
    } else if(command.equals("RANDOMCOLOR")) {
        randomColor();

        EEPROM.write(5, color[0]);
        EEPROM.write(6, color[1]);
        EEPROM.write(7, color[2]);

        EFFECT_MODE = 0;
        LAST_MINUTE = -1; // Set this to force the clock to update

        return 1;


    // Set effect mode
    } else if(command.substring(0, 10)=="SETEFFECT,") {
        EFFECT_MODE = command.substring(10).toInt();
        EEPROM.write(4, EFFECT_MODE);
        LAST_MINUTE = -1; // Set this to force the clock to update

        return EFFECT_MODE;


    // Get effect mode
    } else if(command.equals("GETEFFECT")) {
        return EFFECT_MODE;


    // Set rainbow effect delay
    } else if(command.substring(0, 16)=="SETRAINBOWDELAY,") {
        RAINBOW_DELAY = command.substring(16).toInt();
        EEPROM.write(8, RAINBOW_DELAY);
        return RAINBOW_DELAY;

    // Get rainbow effect delay
    } else if(command.equals("GETRAINBOWDELAY")) {
        return RAINBOW_DELAY;
    }


    return -1;
}


// Handle the different effect modes
void doEffectMode() {
    switch(EFFECT_MODE) {
        case 1: // Rainbow
            applyRainbow();
            break;
    }
}


// Turn off all pixels
void blackOut() {
    // Black it out
    for(uint8_t x=0; x<PIXEL_COUNT; x++)
        strip.setPixelColor(x, strip.Color(0, 0, 0));
}


// Generate a random color
void randomColor() {
    color[0] = random(32, 255);
    color[1] = random(32, 255);
    color[1] = random(32, 255);
}


// Display the rainbow
void rainbow(uint8_t wait) {
    uint16_t i, j;

    for(j=0; j<256; j++) {
        for(i=0; i<strip.numPixels(); i++) {
            strip.setPixelColor(i, Wheel((i+j) & 255));
        }

        strip.show();
        delay(wait);
    }
}


// Handle display of the time
void ticktock() {
	if(Time.minute()==LAST_MINUTE)
		return;

	// Round the minute to the nearest 5-minute interval
	uint8_t minute = 5 * round((Time.minute()+Time.second()/60)/5);
	LAST_MINUTE = Time.minute();

	blackOut();

	// Default words
	doWord("IT");
	doWord("IS");
	doWord("O'CLOCK");


	// Determine TO / PAST
	if(minute>30 && minute!=0)
		doWord("TO");
	else if(minute<=30 && minute!=0)
		doWord("PAST");


	// Setting this flag will show "MINUTES"
	bool showMinutes = true;


	// Determine which word to light up for the minutes
	switch(minute) {
		case 0:
			showMinutes = false;
			break;
		case 5:
		case 55:
			doWord("FIVE");
			break;
		case 10:
		case 50:
			doWord("TEN");
			break;
		case 15:
		case 45:
			doWord("QUARTER");
			showMinutes = false;
			break;
		case 20:
		case 40:
			doWord("TWENTY");
			break;
		case 25:
		case 35:
			doWord("TWENTY");
			doWord("FIVE");
			break;
		case 30:
			doWord("HALF");
			showMinutes = false;
			break;
	}

	// Show "MINUTES"
	if(showMinutes)
		doWord("MINUTES");


	// The hour
	uint8_t hour = Time.hour();

	// Convert to 12-hour format
	if(hour>12)
		hour -= 12;

	// Do we need to increment the hour for "TO"
	if(minute>30)
		hour++;

	// Fix for 13 o'clock
	if(hour==13)
		hour = 1;

	// Fix for 0 o'clock
	if(hour==0)
		hour = 12;


    // Display the hour
	switch(hour) {
		case 1:
			doWord("ONE");
			break;
		case 2:
			doWord("TWO");
			break;
		case 3:
			doWord("THREE");
			break;
		case 4:
			doWord("FOUR");
			break;
		case 5:
			doWord("FIVE", true);
			break;
		case 6:
			doWord("SIX");
			break;
		case 7:
			doWord("SEVEN");
			break;
		case 8:
			doWord("EIGHT");
			break;
		case 9:
			doWord("NINE");
			break;
		case 10:
			doWord("TEN", true);
			break;
		case 11:
			doWord("ELEVEN");
			break;
		case 12:
			doWord("TWELVE");
	}


    // Light it up!
	strip.show();
}


// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
    if(WheelPos < 85) {
        return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
    } else if(WheelPos < 170) {
        WheelPos -= 85;
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    } else {
        WheelPos -= 170;
        return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }
}


// Underloaded(?) version of doWord()
void doWord(String word) {
    doWord(word, false);
}


// Take a word, find it in the string, and turn it on (and optionally "skip" the first word)
void doWord(String word, bool skip) {
	word = word.toUpperCase();

    uint8_t x, y;

	if(!skip) {
		y = text.indexOf(word)/COLS;
		x = text.indexOf(word)-y*COLS;
	} else {
		y = text.lastIndexOf(word)/COLS;
		x = text.lastIndexOf(word)-y*COLS;
	}

	for(uint8_t i=x; i<x+word.length(); i++) {
	    uint8_t this_pixel = xyToPixel(i, y);

	    if(this_pixel!=status_pixel)
		    strip.setPixelColor(this_pixel, strip.Color(color[0], color[1], color[2]));
	}
}


// Convert X, Y coordinates to a pixel on the NeoPixel string
uint8_t xyToPixel(uint8_t x, uint8_t y) {
	int pixel;

	// If y is even, go left-to-right
	if(y%2==0) {
		pixel = y * ROWS + x + 1;

	// If y is odd, go right-to-left
	} else {
		pixel = y * ROWS + COLS - x;
	}

	pixel = (ROWS*COLS)-pixel;

	return pixel;
}


// All the pretty colors!  Now with more non-blocking!
void applyRainbow() {
	if(elapsedRainbow<RAINBOW_DELAY)
		return;

	if(rainbow_cycle>255)
		rainbow_cycle = 0;

    for(uint8_t i=0; i<strip.numPixels(); i++) {
        if(strip.getPixelColor(i)>0 && i!=status_pixel)
            strip.setPixelColor(i, Wheel((i+rainbow_cycle) & 255));
    }

    rainbow_cycle++;
    elapsedRainbow = 0;
}


#ifdef ENABLE_DHT
void doDHT() {
    if(elapsedDHT>2000) {
        if(!dhtStarted) {
            DHT.acquire();
            dhtStarted = true;
        }

        if(!DHT.acquiring()) {
            int dhtResult = DHT.getStatus();

            if(dhtResult==DHTLIB_OK) {
                dhtIsGood = true;

                dhtHumidity = DHT.getHumidity();
                dhtFahrenheit = DHT.getFahrenheit();
                dhtDewPoint = DHT.getDewPoint();
            } else
                dhtIsGood = false;

            dhtStarted = false;
        }

        elapsedDHT = 0;
    }
}


void dht_wrapper() {
    DHT.isrCallback();
}
#endif


#ifdef ENABLE_PUB
void doPub() {
    if(elapsedPub>intervalPub) {
#ifdef ENABLE_DHT
        // Skip this publish if the DHT22 reading is exactly 0
	    if((uint8_t)dhtHumidity==0) {
	        elapsedPub = intervalPub - 5000;
	        return;

	    // Skip this publish if the DHT22 reading is bad
	    } else if(dhtIsGood==false) {
	        elapsedPub = intervalPub - 5000;
	        return;
	    }
#endif

        String pub = "";

#ifdef DEVICE_NAME
		pub += String(DEVICE_NAME)+";";
#endif

#ifdef ENABLE_DHT
        pub += "h:"+String((float)dhtHumidity, 2)+"|g,f:"+String((float)dhtFahrenheit, 2)+"|g";
#endif

#if PRODUCT_ID!=10
        if(pub.length()>String(DEVICE_NAME).length()+1)
            pub += ",";

        pub += "r:"+String(WiFi.RSSI())+"|g";
#endif

#if PRODUCT_ID==10
        if(pub.length()>String(DEVICE_NAME).length()+1)
            pub += ",";

        pub += "vc:"+String(fuelVcell, 2)+"|g,soc:"+String(fuelSOC, 2)+"|g";
#endif

#ifdef SERIAL_DEBUG
    Serial.println(pub);
#endif

        // Only publish if we actually have something to publish
        if(pub.length()>String(DEVICE_NAME).length()+1)
            Particle.publish(PUB_EVENT, pub, PUB_TTL, PUB_SCOPE);

        elapsedPub = 0;
    }
}
#endif


#if PRODUCT_ID==10
void doFuel() {
    if(elapsedFuel>FUEL_INTERVAL) {
        fuelSOC = fuel.getSoC();
        fuelVcell = fuel.getVCell();

        fuel_timer = 0;
    }
}
#endif