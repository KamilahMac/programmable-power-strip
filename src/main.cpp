#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "env.h" 
#include <stdio.h>
#include <string.h>
#include <TM1637Display.h>
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <time.h>


float out1_daily = 0, out2_daily = 0, out3_daily = 0;
unsigned long lastMillis = 0;

WiFiClient wifiClient;
HTTPClient http;
String energyURL = "http://192.168.100.65:8000/energy_data";
String settingsURL = "http://192.168.100.65:8000/user_outlet_input";

//String energyURL = "http://10.220.78.122:8000/energy_data";
//String settingsURL = "http://10.220.78.122:8000/user_outlet_input";

//String energyURL = "http://10.22.36.22:8000/energy_data";
//String settingsURL = "http://10.22.36.22:8000/user_outlet_input";


#define RX 16
#define TX 17

#define INC_BUTTON_PIN 35      
#define DEC_BUTTON_PIN 4       
#define ENTER_BUTTON_PIN 15    
#define MODE_BUTTON_PIN 34     

#define CLK1 26
#define DIO1 27
#define CLK2 32
#define DIO2 33
#define CLK3 14
#define DIO3 13

#define PWM_FREQ 5000
#define PWM_RES 8

#define SD_CS 5

HardwareSerial mySerial(2);

TM1637Display display1 = TM1637Display(CLK1, DIO1);
TM1637Display display2 = TM1637Display(CLK2, DIO2);
TM1637Display display3 = TM1637Display(CLK3, DIO3);

const int PIN_RED = 25;   
const int PIN_GREEN = 22;
const int PIN_BLUE = 21;

// Segment patterns for OUT1, OUT2, OUT3
const uint8_t out1[] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,        // O
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,                // U
  SEG_D | SEG_E | SEG_F | SEG_G,                        // T
  SEG_E | SEG_F                                         // 1
};

const uint8_t out2[] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,        // O
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,                // U
  SEG_D | SEG_E | SEG_F | SEG_G,                        // T
  SEG_A | SEG_B | SEG_D | SEG_E | SEG_G                 // 2
};

const uint8_t out3[] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,        // O
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,                // U
  SEG_D | SEG_E | SEG_F | SEG_G,                        // T
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_G                 // 3
};

// datafromwebsite flag:
// 0 = Receiving data FROM ATmega (Default mode)
// 1 = Sending data TO ATmega (Toggle & Current Setting modes)
int datafromwebsite = 0;

bool website_cmd_ready = false;
bool newAtmegaData = false;

String atmegaBuffer = "";
unsigned long lastSerialReceive = 0;

volatile bool outlet_cmd_pending = false;
volatile bool current_limit_pending = false;
bool manual_state_sync_pending = false;
unsigned long lastManualSyncTime = 0;

unsigned long lastWebSendTime = 0;
const unsigned long WEB_SEND_INTERVAL = 2000; // ms

bool outlet_state_changed = false;
bool current_limit_changed = false;

unsigned long lastDataSendTime = 0;
unsigned long dataSendInterval = 1000;

// Outlet status variables for Toggle Mode
volatile int outlet1_enable = 0;
volatile int outlet2_enable = 0;
volatile int outlet3_enable = 0;

// Current limit variables for each outlet
float outlet1_set_current_limit = 7.5;
float outlet2_set_current_limit = 7.5;
float outlet3_set_current_limit = 7.5;

float cur_lmt_out1_calc = 0.5;
float cur_lmt_out2_calc = 0.5;
float cur_lmt_out3_calc = 0.5;

volatile int inc_btn_cnt_olt_seq = 0;
volatile int rt_current_limit_level = 0;

unsigned long lastDebounceTimeInc = 0;
unsigned long lastDebounceTimeDec = 0;
unsigned long lastDebounceTimeEnter = 0;
unsigned long lastDebounceTimeMode = 0;
unsigned long debounceDelay = 300;

volatile bool increaseButtonPressed = false;
volatile bool decreaseButtonPressed = false;
volatile bool EnterButtonPressed = false;
volatile bool ModeButtonPressed = false;

volatile int mode_select_btn_count = 0;
volatile int present_Mode_option = 0;

volatile unsigned long lastIncPress = 0;
volatile unsigned long lastDecPress = 0;
volatile unsigned long lastEnterPress = 0;
volatile unsigned long lastModePress = 0;

int default_mode_flag = 0;
int current_set_mode_flag = 0;
int toggle_mode_flag = 0;

int current_setting_active = 0;

int irms_value_cur_sen1 = 0, irms_value_cur_sen2 = 0, irms_value_cur_sen3 = 0;
float irms_float_cur_sen1 = 0, irms_float_cur_sen2 = 0, irms_float_cur_sen3 = 0;

float out1_cur_val = 0.0;
float out2_cur_val = 0.0;
float out3_cur_val = 0.0;

float out1_engy_val = 0.0;
float out2_engy_val = 0.0;
float out3_engy_val = 0.0;

float out1_relay_trip_time = 0.0;
float out2_relay_trip_time = 0.0;
float out3_relay_trip_time = 0.0;


float out1_set_cur_limit = 0;
float out2_set_cur_limit = 0;
float out3_set_cur_limit = 0;

// --- SD Card interval accumulator (add these globals) ---
float outlet1_interval_energy_Wh = 0.0;
float outlet2_interval_energy_Wh = 0.0;
float outlet3_interval_energy_Wh = 0.0;

bool interval_log_just_fired = false;
bool sdCardAvailable = false;        
bool startup_state_received = false;  

unsigned long lastSDLogTime = 0;
const unsigned long SD_LOG_INTERVAL_MS = 300000UL; // 5 minutes

unsigned long lastEnergyUpdateTime = 0;
unsigned long lastMidnightCheck    = 0;
unsigned long simulatedDayMillis   = 86400000UL; // 24h in ms

// Function declarations
void IRAM_ATTR handleIncreaseButton();
void IRAM_ATTR handleDecreaseButton();
void IRAM_ATTR handleEnterButton();
void IRAM_ATTR handleModeButton();
void handle_button_presses();
void display_current_limit(float value, TM1637Display &display);
void current_setting_seq_w_7seg();
void save_current_limit();
void reset_outlet_current_limit(int outlet);
void outlet_seq_w_7seg();
void handle_outlet_enable();
void handle_outlet_disable();
void mode_select_option(int mode_select_btn_count);
void displayIRMS1(float value1);
void displayIRMS2(float value2);
void displayIRMS3(float value3);
void Default_mode();
void Toggle_mode();
void Current_Setting_mode();
void setColor(int R, int G, int B);
void MODE_updateRGB();
void receiveDataFromATmega();
void sendDataToATmega();
void sendCurrentLimits();
void sendOutletCommands(int outletIndex, int enableState);
void updateEnergyCalculations();
void parseCurrentValues(String data);
void parseEnergyValues(String data);
void parseRelayTripTime(String data);
void parseStartupState(String data);   
void updateDisplays();
void postCurrentStateToServer(); 
void send_website_data();
void logTripTimes(int outletNum, float tripTime);
void logIntervalEnergy();
void restoreDailyTotals();             
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);

//===================================================================
//    VOID SET UP
void setup() {

    Serial.begin(9600);

    mySerial.begin(9600, SERIAL_8N1, RX, TX);

     pinMode(INC_BUTTON_PIN, INPUT);
    pinMode(DEC_BUTTON_PIN, INPUT_PULLUP);
    pinMode(ENTER_BUTTON_PIN, INPUT_PULLUP);
    pinMode(MODE_BUTTON_PIN, INPUT);           

    attachInterrupt(digitalPinToInterrupt(INC_BUTTON_PIN), handleIncreaseButton, FALLING);
    attachInterrupt(digitalPinToInterrupt(DEC_BUTTON_PIN), handleDecreaseButton, FALLING);
    attachInterrupt(digitalPinToInterrupt(ENTER_BUTTON_PIN), handleEnterButton, FALLING);
    attachInterrupt(digitalPinToInterrupt(MODE_BUTTON_PIN), handleModeButton, FALLING);

ledcSetup(0, PWM_FREQ, PWM_RES);
ledcSetup(1, PWM_FREQ, PWM_RES);
ledcSetup(2, PWM_FREQ, PWM_RES);

  ledcAttachPin(PIN_RED, 0);  // Attach to channel 0
ledcAttachPin(PIN_GREEN, 1); // Attach to channel 1
ledcAttachPin(PIN_BLUE, 2);  // Attach to channel 2

 display1.clear();
    display1.setBrightness(2);
    display2.clear();
    display2.setBrightness(2);
    display3.clear();
    display3.setBrightness(2);

    present_Mode_option = 0;
    datafromwebsite = 0;
    mode_select_option(present_Mode_option);

     WiFi.begin(SSID,PASSWORD);


     Serial.print("Connecting to WiFi");
unsigned long wifiStart = millis();
while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 15000) {  // 15 second timeout
        Serial.println("WiFi timeout — continuing without WiFi");
        break;  // keep going even without WiFi
    }
    delay(500);
    Serial.print(".");
}
if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
} else {
    Serial.println("No WiFi — SD logging still active");
}

    Serial.print("WiFi connected. IP address is: ");
    Serial.println(WiFi.localIP());

   
if (WiFi.status() == WL_CONNECTED) {
   // configTime(0, 0, "pool.ntp.org");
    configTime(-5*3600, 0, "pool.ntp.org");
    Serial.print("Waiting for NTP time sync");
    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    if (retries < 10) {
        Serial.println(" done!");
        Serial.printf("Time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println(" NTP sync failed — midnight reset won't work");
    }
}

    // Initialize SD card
       // SD card LAST — its return can no longer block anything above
    if (!SD.begin(SD_CS)) {
        Serial.println("Card Mount Failed — continuing without SD");
        // no return — system keeps running 
        sdCardAvailable = false;
    } else {
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            Serial.println("No SD card attached");
             sdCardAvailable = false;
        } else {
            Serial.println("SD card OK");
            sdCardAvailable = true; 
          
File tripFile = SD.open("/trip_log.csv");
if (!tripFile) {
    writeFile(SD, "/trip_log.csv", "date,time,outlet,trip_curr_val\r\n");
}else {
    String firstLine = tripFile.readStringUntil('\n');
    tripFile.close();
    if (firstLine.startsWith("timestamp_ms")|| firstLine.indexOf("trip_time_s") != -1) {
        Serial.println("Old trip_log format detected — recreating file");
        writeFile(SD, "/trip_log.csv", "date,time,outlet,trip_curr_val\r\n");
    }
}
//tripFile.close();

File engyFile = SD.open("/energy_log.csv");
if (!engyFile) {
    writeFile(SD, "/energy_log.csv",
        "date,time,out1_interval_Wh,out2_interval_Wh,out3_interval_Wh,"
        "out1_cumulative_Wh,out2_cumulative_Wh,out3_cumulative_Wh\r\n");
    }else {
    // File exists — read first line and check if it's the old format
    String firstLine = engyFile.readStringUntil('\n');
    engyFile.close();
    if (firstLine.startsWith("timestamp_ms")) {
        // Old header detected — wipe and rewrite
        Serial.println("Old energy_log format detected — recreating file");
        writeFile(SD, "/energy_log.csv",
            "date,time,out1_interval_Wh,out2_interval_Wh,out3_interval_Wh,"
            "out1_cumulative_Wh,out2_cumulative_Wh,out3_cumulative_Wh\r\n");
    }
    }
//engyFile.close();
           
 }
       
    }
  // After SD card init succeeds
restoreDailyTotals();  //reload daily totals from last log row
    
delay(500);
  
// Signal ATmega that ESP32 is ready to receive startup state
mySerial.println("ESP_READY");
Serial.println("Sent ESP_READY to ATmega");

increaseButtonPressed = false;
decreaseButtonPressed = false;
EnterButtonPressed    = false;
ModeButtonPressed     = false;
lastDebounceTimeInc   = millis();
lastDebounceTimeDec   = millis();
lastDebounceTimeEnter = millis();
lastDebounceTimeMode  = millis();
}

//===================================================================
//    VOID LOOP
void loop() {

   handle_button_presses();
    MODE_updateRGB();
    receiveDataFromATmega();
    updateEnergyCalculations();

    if (present_Mode_option == 0) {
        
        
        if (newAtmegaData) {
            Default_mode();
            newAtmegaData = false;
        }
    }

    if (website_cmd_ready) {
        
        if (current_limit_pending) {
            sendCurrentLimits();
            current_limit_pending = false;
        }
        if (present_Mode_option == 1) {
            Serial.println("Website outlet states applied in Toggle mode");
        } else if (present_Mode_option == 2) {
            display1.clear(); display2.clear(); display3.clear();
            if (inc_btn_cnt_olt_seq == 0) {
                rt_current_limit_level = (int)(outlet1_set_current_limit / 0.5 + 0.5);
                display_current_limit(outlet1_set_current_limit, display1);
            } else if (inc_btn_cnt_olt_seq == 1) {
                rt_current_limit_level = (int)(outlet2_set_current_limit / 0.5 + 0.5);
                display_current_limit(outlet2_set_current_limit, display2);
            } else if (inc_btn_cnt_olt_seq == 2) {
                rt_current_limit_level = (int)(outlet3_set_current_limit / 0.5 + 0.5);
                display_current_limit(outlet3_set_current_limit, display3);
            }
        }
        website_cmd_ready = false;
    }
    
    if (outlet_cmd_pending) {
         sendOutletCommands(0, outlet1_enable);
    sendOutletCommands(1, outlet2_enable);
    sendOutletCommands(2, outlet3_enable);
        outlet_cmd_pending = false;
    }
    
    if (manual_state_sync_pending && (millis() - lastManualSyncTime > 500)) {
        if (millis() - lastWebSendTime > 200) {
            lastManualSyncTime = millis();
            postCurrentStateToServer();
            manual_state_sync_pending = false;
        }
    }

    unsigned long now = millis();

    if (now - lastSDLogTime >= SD_LOG_INTERVAL_MS) {
        lastSDLogTime = now;
        logIntervalEnergy();
    }

    if (now - lastWebSendTime >= WEB_SEND_INTERVAL) {
        lastWebSendTime = now;
        send_website_data();
    }
}
// =====================================================================
// SEND WEBSITE DATA FUNCTIONS

void send_website_data() {

    // Auto-reconnect WiFi if it drops
static unsigned long lastWifiCheck = 0;
if (millis() - lastWifiCheck > 10000) {  // check every 10 seconds
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost — reconnecting...");
        WiFi.reconnect();
    }
}
// Guard — don't attempt HTTP if still not connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected — skipping web send");
        return;
    }
    // ---- Send energy data ----
    StaticJsonDocument<384> energyDoc;
    JsonArray rtEng = energyDoc.createNestedArray("interval_eng_Wh");
    rtEng.add(outlet1_interval_energy_Wh);
    rtEng.add(outlet2_interval_energy_Wh);
    rtEng.add(outlet3_interval_energy_Wh);

    JsonArray dailyEng = energyDoc.createNestedArray("daily_eng_consum");
    dailyEng.add(out1_daily);   // real accumulators
    dailyEng.add(out2_daily);
    dailyEng.add(out3_daily);

    JsonArray onOff = energyDoc.createNestedArray("outlet_on_off");
    onOff.add(outlet1_enable);
    onOff.add(outlet2_enable);
    onOff.add(outlet3_enable);

    JsonArray limits = energyDoc.createNestedArray("outlet_current_limit");
    limits.add(outlet1_set_current_limit);
    limits.add(outlet2_set_current_limit);
    limits.add(outlet3_set_current_limit);

      //flag tells website a 5-min interval just logged ──
    energyDoc["interval_log_fired"] = interval_log_just_fired;
    interval_log_just_fired = false;  // clear immediately after adding to JSON
    String jsonEnergy;
    serializeJson(energyDoc, jsonEnergy);  // moved to after all fields added

    http.begin(wifiClient, energyURL);        
    http.addHeader("Content-Type", "application/json");
    int postCode = http.POST(jsonEnergy);
    if (postCode > 0) {
        Serial.print("Energy POST response: ");
        Serial.println(http.getString());
    } else {
        Serial.print("Error sending energy data: ");
        Serial.println(postCode);
    }
    http.end();

    // ---- Get user settings ----
    http.begin(wifiClient, settingsURL);        
    int getCode = http.GET();
    if (getCode == 200) {
        String resp = http.getString();
        Serial.println("Received user settings:");
        Serial.println(resp);

        StaticJsonDocument<256> settingsDoc;
        DeserializationError err = deserializeJson(settingsDoc, resp);
        if (!err) {
            JsonArray currentLimits = settingsDoc["outlet_current_limit"];
            JsonArray onOff         = settingsDoc["outlet_on_off"];
            
            if (currentLimits.size() == 3 && onOff.size() == 3) {
               float newL1 = currentLimits[0], newL2 = currentLimits[1], newL3 = currentLimits[2];
    int newO1 = onOff[0], newO2 = onOff[1], newO3 = onOff[2];

    bool limitsChanged = (newL1 != outlet1_set_current_limit ||
                          newL2 != outlet2_set_current_limit ||
                          newL3 != outlet3_set_current_limit);
    bool statesChanged = (newO1 != outlet1_enable ||
                          newO2 != outlet2_enable ||
                          newO3 != outlet3_enable);

    outlet1_set_current_limit = newL1;
    outlet2_set_current_limit = newL2;
    outlet3_set_current_limit = newL3;
    outlet1_enable = newO1;
    outlet2_enable = newO2;
    outlet3_enable = newO3;

if (limitsChanged) {
    current_limit_pending = true;
    website_cmd_ready     = true;
}
if (statesChanged) {
    outlet_cmd_pending = true;
    website_cmd_ready  = true;
}
}
        } else {
            Serial.print("JSON parse error: ");
            Serial.println(err.c_str());
        }
    } else {
        Serial.print("GET user settings failed: ");
        Serial.println(getCode);
    }
    http.end();
}


void updateEnergyCalculations() {
    
        // Accumulate Wh from power (kW) × time (h)
         if (newAtmegaData) {
        outlet1_interval_energy_Wh += out1_engy_val / 1000.0;
        outlet2_interval_energy_Wh += out2_engy_val / 1000.0;
        outlet3_interval_energy_Wh += out3_engy_val / 1000.0;
    }

    

        struct tm timeinfo;
if (getLocalTime(&timeinfo)) {
    static int lastResetDay = -1;
    int today = timeinfo.tm_yday;  // day of year 0-365

    if (lastResetDay == -1) lastResetDay = today;  // init on first run

   
        if (today != lastResetDay && timeinfo.tm_hour == 0 && timeinfo.tm_min == 0) {
       
        out1_daily = 0.0;
        out2_daily = 0.0;
        out3_daily = 0.0;
        lastResetDay = today;
        Serial.println("=== Midnight Reset (NTP) ===");
    }
}
}

// Returns "YYYY-MM-DD HH:MM:SS" from NTP, or "millis:XXXXXXX" as fallback
String getTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return String(buf);
    }
    return "millis:" + String(millis());  // fallback if NTP not synced
}

void logIntervalEnergy() {

    out1_daily += outlet1_interval_energy_Wh;
    out2_daily += outlet2_interval_energy_Wh;
    out3_daily += outlet3_interval_energy_Wh;

    String ts = getTimestamp();  // "YYYY-MM-DD HH:MM:SS"
    String engyLog =
          ts.substring(0, 10)                      + "," +      // date
          ts.substring(11)                         + "," +   // time
        String(outlet1_interval_energy_Wh, 7)     + "," +
        String(outlet2_interval_energy_Wh, 7)     + "," +
        String(outlet3_interval_energy_Wh, 7)     + "," +
        String(out1_daily, 7)                     + "," +   // cumulative
        String(out2_daily, 7)                     + "," +
        String(out3_daily, 7)                     + "\r\n";


    appendFile(SD, "/energy_log.csv", engyLog.c_str());
    Serial.println("Energy logged.");

    interval_log_just_fired = true;  // website will see this flag

    // Reset interval buckets AFTER logging
    outlet1_interval_energy_Wh = 0.0;
    outlet2_interval_energy_Wh = 0.0;
    outlet3_interval_energy_Wh = 0.0;
}

// In logIntervalEnergy() — already saves to CSV, so daily is recoverable
// On boot, read the last row of energy_log.csv to restore out1_daily etc.

void restoreDailyTotals() {
    if (!sdCardAvailable) return;

    File f = SD.open("/energy_log.csv");
    if (!f) return;

    String lastLine = "";
    String line = "";
    while (f.available()) {
        line = f.readStringUntil('\n');
        if (line.length() > 10) lastLine = line;  
    }
    f.close();

    if (lastLine.length() == 0) return;

    // Parse: timestamp,out1_int,out2_int,out3_int,out1_cum,out2_cum,out3_cum
    int c1 = lastLine.indexOf(',');
    int c2 = lastLine.indexOf(',', c1+1);
    int c3 = lastLine.indexOf(',', c2+1);
    int c4 = lastLine.indexOf(',', c3+1);
    int c5 = lastLine.indexOf(',', c4+1);
    int c6 = lastLine.indexOf(',', c5+1);
    int c7 = lastLine.indexOf(',', c6+1);
    if (c7 == -1) return;

    out1_daily = lastLine.substring(c5+1, c6).toFloat();
    out2_daily = lastLine.substring(c6+1, c7).toFloat();
    out3_daily = lastLine.substring(c7+1).toFloat();

    Serial.println("Daily totals restored from SD:");
    Serial.println("out1: " + String(out1_daily,8) +
                   " out2: " + String(out2_daily,8) +
                   " out3: " + String(out3_daily,8));
}

    void postCurrentStateToServer() {
    if (WiFi.status() != WL_CONNECTED) return;

    StaticJsonDocument<256> doc;
    JsonArray onOff = doc.createNestedArray("outlet_on_off");
    onOff.add(outlet1_enable == 1);
    onOff.add(outlet2_enable == 1);
    onOff.add(outlet3_enable == 1);

    JsonArray limits = doc.createNestedArray("outlet_current_limit");
    limits.add(outlet1_set_current_limit);
    limits.add(outlet2_set_current_limit);
    limits.add(outlet3_set_current_limit);

    String body;
    serializeJson(doc, body);

    //http.begin(wifiClient, "http://10.220.78.122:8000/esp_state");

   //http.begin(wifiClient, "http://10.22.36.22:8000/esp_state");

    http.begin(wifiClient, "http://192.168.100.65:8000/esp_state");
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code > 0)
    Serial.println("State synced to server");
else
    Serial.println("State sync failed");
    http.end();
}

// =====================================================================
// DATA DIRECTION CONTROL
void updateDataFromWebsiteFlag() {
 
    if (present_Mode_option == 0) {
        if (datafromwebsite != 0) {
            datafromwebsite = 0;
            Serial.println("datafromwebsite = 0 (Receiving from ATmega)");
        }
    }
    else if (present_Mode_option == 1 || present_Mode_option == 2) {
        if (datafromwebsite != 1) {
            datafromwebsite = 1;
            Serial.println("datafromwebsite = 1 (Sending to ATmega)");
        }
    }
}

// =====================================================================
// INTERRUPT SERVICE ROUTINES (ISRs)

void IRAM_ATTR handleIncreaseButton() {
    if (digitalRead(INC_BUTTON_PIN) == LOW) {   // confirm pin is actually low
        increaseButtonPressed = true;   
    } 
}

void IRAM_ATTR handleDecreaseButton() {
   
        decreaseButtonPressed = true;
     }

void IRAM_ATTR handleEnterButton() {
  
        EnterButtonPressed = true;
      }

void IRAM_ATTR handleModeButton() {
    if (digitalRead(MODE_BUTTON_PIN) == LOW) {  // confirm pin is actually low
        ModeButtonPressed = true;
       }
    }
// =====================================================================
// BUTTON HANDLING
void handle_button_presses() {
    unsigned long currentMillis = millis();
    // --- Mode button: cycles through 3 modes (0=Default, 1=Toggle, 2=Current Setting) ---
    if (ModeButtonPressed) {
        ModeButtonPressed = false;  // 
        if (currentMillis - lastDebounceTimeMode > debounceDelay) {
            lastDebounceTimeMode = currentMillis;
            // Auto-save active outlet's current limit before leaving Current Setting Mode
            if (present_Mode_option == 2) {
                save_current_limit();
                 //sendCurrentLimits();
                 current_limit_pending = true;
            }
            int newMode = (present_Mode_option + 1) % 3;  // 3 modes only

            if (present_Mode_option == 0 && newMode != 0) {
                display1.clear();
                display2.clear();
                display3.clear();
            }

            present_Mode_option = newMode;
            mode_select_option(present_Mode_option);
            //updateDataFromWebsiteFlag();
            lastDebounceTimeMode = currentMillis;
            ModeButtonPressed = false;
        }
    }

    // --- Increase button ---
    // Toggle Mode    : Enable selected outlet
    // Current Setting: Increase current limit
     if (increaseButtonPressed) {
    increaseButtonPressed = false;          // clear FIRST always
    if (currentMillis - lastDebounceTimeInc > debounceDelay) {
        lastDebounceTimeInc = currentMillis;
        if (present_Mode_option == 1) {                  // Toggle Mode — Enable
            handle_outlet_enable();
           // outlet_cmd_pending = true;
             //sendOutletCommands();
            outlet_state_changed = true;
        } else if (present_Mode_option == 2) {           // Current Setting — Increase limit
            if (rt_current_limit_level < 30) {
                rt_current_limit_level++;
                float current_value = rt_current_limit_level * 0.5;
                if (current_value > 15.0) current_value = 15.0;
                Serial.print("Current Limit: ");
                Serial.print(current_value);
                Serial.println(" A");

                // ── UPDATE variable immediately ──────────────────────────────
        if (inc_btn_cnt_olt_seq == 0)      outlet1_set_current_limit = current_value;
        else if (inc_btn_cnt_olt_seq == 1) outlet2_set_current_limit = current_value;
        else if (inc_btn_cnt_olt_seq == 2) outlet3_set_current_limit = current_value;
        // ────────────────────────────────────────────────────────────

                if (inc_btn_cnt_olt_seq == 0) {
                    display_current_limit(current_value, display1);
                } else if (inc_btn_cnt_olt_seq == 1) {
                    display_current_limit(current_value, display2);
                } else if (inc_btn_cnt_olt_seq == 2) {
                    display_current_limit(current_value, display3);
                }
                manual_state_sync_pending = true;   // reset timer on each press
                lastManualSyncTime = millis();       // restart the 500ms window
                current_limit_changed = true;
            }
        }
    }
    }

    // --- Decrease button ---
    // Toggle Mode    : Disable selected outlet
    // Current Setting: Decrease current limit
    if (decreaseButtonPressed) {
    decreaseButtonPressed = false;          // clear FIRST always
    if (currentMillis - lastDebounceTimeDec > debounceDelay) {
        lastDebounceTimeDec = currentMillis;                // Toggle Mode — Disable
            if (present_Mode_option == 1) { 
        handle_outlet_disable();
            //outlet_cmd_pending = true;
             //sendOutletCommands();
            outlet_state_changed = true;
        } else if (present_Mode_option == 2) {           // Current Setting — Decrease limit
            if (rt_current_limit_level > 1) {
                rt_current_limit_level--;
                float current_value = rt_current_limit_level * 0.5;
                Serial.print("Current Limit: ");
                Serial.print(current_value);
                Serial.println(" A");

                // ── UPDATE variable immediately ──────────────────────────────
        if (inc_btn_cnt_olt_seq == 0)      outlet1_set_current_limit = current_value;
        else if (inc_btn_cnt_olt_seq == 1) outlet2_set_current_limit = current_value;
        else if (inc_btn_cnt_olt_seq == 2) outlet3_set_current_limit = current_value;
        // ────────────────────────────────────────────────────────────

                if (inc_btn_cnt_olt_seq == 0) {
                    display_current_limit(current_value, display1);
                } else if (inc_btn_cnt_olt_seq == 1) {
                    display_current_limit(current_value, display2);
                } else if (inc_btn_cnt_olt_seq == 2) {
                    display_current_limit(current_value, display3);
                }
                manual_state_sync_pending = true;   // reset timer on each press
                lastManualSyncTime = millis();       // restart the 500ms window
                current_limit_changed = true;
            }
        }
        }
    }

    // --- Enter button ---
    // Toggle Mode    : Cycle selected outlet (OUT1 -> OUT2 -> OUT3 -> ...)
    // Current Setting: Save limit and move to next outlet
   if (EnterButtonPressed) {
    EnterButtonPressed = false; 
    if (currentMillis - lastDebounceTimeEnter > debounceDelay) {   // ← ADD THIS
        lastDebounceTimeEnter = currentMillis;  
        if (present_Mode_option == 1) {                  // Toggle Mode — Cycle outlets
            outlet_seq_w_7seg();
        } else if (present_Mode_option == 2) {           // Current Setting — Save + Next
            save_current_limit();
            current_limit_pending = true;
            //sendCurrentLimits();
            current_setting_seq_w_7seg();
            current_limit_changed = true;
        }
    }
}
}

// =====================================================================
// OUTLET CONTROL FUNCTIONS
// Outlet sequence — used by Enter button in Toggle Mode
void outlet_seq_w_7seg() {
    inc_btn_cnt_olt_seq = (inc_btn_cnt_olt_seq + 1) % 3;

    Serial.print("Outlet sequence changed to: OUT");
    Serial.println(inc_btn_cnt_olt_seq + 1);

    display1.clear();
    display2.clear();
    display3.clear();

    if (inc_btn_cnt_olt_seq == 0) {
        display1.setSegments(out1);
    } else if (inc_btn_cnt_olt_seq == 1) {
        display2.setSegments(out2);
    } else if (inc_btn_cnt_olt_seq == 2) {
        display3.setSegments(out3);
    }
}

// Enable selected outlet — Increase button in Toggle Mode
void handle_outlet_enable() {
    Serial.print("Enabling Outlet ");
    Serial.println(inc_btn_cnt_olt_seq + 1);

    if (inc_btn_cnt_olt_seq == 0) {
        outlet1_enable = 1;
    } else if (inc_btn_cnt_olt_seq == 1) {
        outlet2_enable = 1;
    } else if (inc_btn_cnt_olt_seq == 2) {
        outlet3_enable = 1;
    }
    sendOutletCommands(inc_btn_cnt_olt_seq, 1);
    //outlet_cmd_pending = true;        // sends immediately to ATmega
    manual_state_sync_pending = true; // syncs to server after 500ms
    lastManualSyncTime = millis();
}

// Disable selected outlet — Decrease button in Toggle Mode
void handle_outlet_disable() {
    Serial.print("Disabling Outlet ");
    Serial.println(inc_btn_cnt_olt_seq + 1);

    if (inc_btn_cnt_olt_seq == 0) {
        outlet1_enable = 0;
    } else if (inc_btn_cnt_olt_seq == 1) {
        outlet2_enable = 0;
    } else if (inc_btn_cnt_olt_seq == 2) {
        outlet3_enable = 0;
    }
    sendOutletCommands(inc_btn_cnt_olt_seq, 0);
    //outlet_cmd_pending = true;        // sends immediately to ATmega
    manual_state_sync_pending = true; // syncs to server after 500ms
    lastManualSyncTime = millis();
}

// =====================================================================
// DISPLAY FUNCTIONS
void display_current_limit(float value, TM1637Display &display) {
    if (value < 0.5)  value = 0.5;
    if (value > 15.0) value = 15.0;
    int     scaled = 0;
    uint8_t dots   = 0;

    if (value < 10.0) {
        scaled = (int)(value * 100.0 + 0.5);
        dots   = 0b01000000;
    } else {
        scaled = (int)(value * 10.0 + 0.5);
        dots   = 0b00100000;
    }
    display.showNumberDecEx(scaled, dots, false, 4, 0);
}

void current_setting_seq_w_7seg() {
    inc_btn_cnt_olt_seq = (inc_btn_cnt_olt_seq + 1) % 3;

    display1.clear();
    display2.clear();
    display3.clear();

    if (inc_btn_cnt_olt_seq == 0) {
        rt_current_limit_level = (int)(outlet1_set_current_limit / 0.5 + 0.5);
        display_current_limit(outlet1_set_current_limit, display1);
        Serial.print("Out1 Cur Limit: ");
        Serial.print(outlet1_set_current_limit);
    } else if (inc_btn_cnt_olt_seq == 1) {
        rt_current_limit_level = (int)(outlet2_set_current_limit / 0.5 + 0.5);
        display_current_limit(outlet2_set_current_limit, display2);
        Serial.print("Out2 Cur Limit: ");
        Serial.print(outlet2_set_current_limit);
    } else if (inc_btn_cnt_olt_seq == 2) {
        rt_current_limit_level = (int)(outlet3_set_current_limit / 0.5 + 0.5);
        display_current_limit(outlet3_set_current_limit, display3);
        Serial.print("Out3 Cur Limit: ");
        Serial.print(outlet3_set_current_limit);
    }
}

void save_current_limit() {
    float current_value = rt_current_limit_level * 0.5;
    if (current_value > 15.0) current_value = 15.0;
    if (current_value < 0.5)  current_value = 0.5;

    if (inc_btn_cnt_olt_seq == 0) {
        outlet1_set_current_limit = current_value;
        cur_lmt_out1_calc         = current_value;
        Serial.print(outlet1_set_current_limit);
    } else if (inc_btn_cnt_olt_seq == 1) {
        outlet2_set_current_limit = current_value;
        cur_lmt_out2_calc         = current_value;
        Serial.print(outlet2_set_current_limit);
    } else if (inc_btn_cnt_olt_seq == 2) {
        outlet3_set_current_limit = current_value;
        cur_lmt_out3_calc         = current_value;
        Serial.print(outlet3_set_current_limit);
    }
    manual_state_sync_pending = true;
}

void displayIRMS1(float value1) {
    if (value1 < 0.0f) value1 = 0.0f;
    if (value1 > 99.99) value1 = 99.99;
    int scaled1 = 0;
    uint8_t dots1 = 0;
    if (value1 < 10.0) {
        scaled1 = (int)(value1 * 1000.0 + 0.5);
        dots1 = 0b10000000;
    } else {
        scaled1 = (int)(value1 * 100.0 + 0.5);
        dots1 = 0b01000000;
    }
    display1.showNumberDecEx(scaled1, dots1, false, 4, 0);
}

void displayIRMS2(float value2) {
    if (value2 < 0.0f) value2 = 0.0f;
    if (value2 > 99.99) value2 = 99.99;
    int scaled2 = 0;
    uint8_t dots2 = 0;
    if (value2 < 10.0) {
        scaled2 = (int)(value2 * 1000.0 + 0.5);
        dots2 = 0b10000000;
    } else {
        scaled2 = (int)(value2 * 100.0 + 0.5);
        dots2 = 0b01000000;
    }
    display2.showNumberDecEx(scaled2, dots2, false, 4, 0);
}

void displayIRMS3(float value3) {
    if (value3 < 0.0f) value3 = 0.0f;
    if (value3 > 99.99) value3 = 99.99;
    int scaled3 = 0;
    uint8_t dots3 = 0;
    if (value3 < 10.0) {
        scaled3 = (int)(value3 * 1000.0 + 0.5);
        dots3 = 0b10000000;
    } else {
        scaled3 = (int)(value3 * 100.0 + 0.5);
        dots3 = 0b01000000;
    }
    display3.showNumberDecEx(scaled3, dots3, false, 4, 0);
}

void setColor(int R, int G, int B) {
    ledcWrite(0, R);   // channel 0 = PIN_RED
    ledcWrite(1, G);   // channel 1 = PIN_GREEN
    ledcWrite(2, B);   // channel 2 = PIN_BLUE
}

void MODE_updateRGB() {
    if      (default_mode_flag == 1)     setColor(0, 255, 255);    // Blue
    else if (current_set_mode_flag == 1) setColor(255, 0, 255);  // Purple
    else if (toggle_mode_flag == 1)      setColor(255, 255, 0);  // Yellow
    else                                 setColor(0, 0, 0);
}

// =====================================================================
// MODE FUNCTIONS
void mode_select_option(int mode_select_btn_count) {
    switch (mode_select_btn_count) {
        case 0:
            Default_mode();
            Serial.println("Mode: Default");
            break;
        case 1:
            Toggle_mode();
            Serial.println("Mode: Toggle");
            break;
        case 2:
            Current_Setting_mode();
            Serial.println("Mode: Current Setting");
            break;

        default:
            Serial.println("Reverting to Default");
            present_Mode_option = 0;
            Default_mode();
            break;
    }
}

void Default_mode() {
    default_mode_flag     = 1;
    current_set_mode_flag = 0;
    toggle_mode_flag      = 0;

    irms_float_cur_sen1 = out1_cur_val;
    displayIRMS1(irms_float_cur_sen1);

    irms_float_cur_sen2 = out2_cur_val;
    displayIRMS2(irms_float_cur_sen2);

    irms_float_cur_sen3 = out3_cur_val;
    displayIRMS3(irms_float_cur_sen3);
}

void Toggle_mode() {
    default_mode_flag     = 0;
    current_set_mode_flag = 0;
    toggle_mode_flag      = 1;
    Serial.println("Yellow mode - Toggle Mode");

    inc_btn_cnt_olt_seq = 0;
    display1.clear();
    display2.clear();
    display3.clear();
    display1.setSegments(out1);
}

void Current_Setting_mode() {
    default_mode_flag     = 0;
    current_set_mode_flag = 1;
    toggle_mode_flag      = 0;
    Serial.println("Current Setting Mode");

    inc_btn_cnt_olt_seq = 0;

    display1.clear();
    display2.clear();
    display3.clear();
    // Load previously saved limit (never reset to default on mode entry)
    rt_current_limit_level = (int)(outlet1_set_current_limit / 0.5 + 0.5);
    display_current_limit(outlet1_set_current_limit, display1);
    Serial.println("Sending current limits to ATmega...");
}

// =====================================================================
// SERIAL COMMUNICATION FUNCTIONS  (unchanged)

void receiveDataFromATmega() {
    while (mySerial.available()) {
        char c = mySerial.read();
        if (c == '\n') {
            if (atmegaBuffer.length() > 0) {
                 
                //Serial.println("Received: " + atmegaBuffer);
                parseCurrentValues(atmegaBuffer);
                parseEnergyValues(atmegaBuffer);
                parseRelayTripTime(atmegaBuffer);
                parseStartupState(atmegaBuffer);   
                newAtmegaData = true;   
                atmegaBuffer = "";
            }
        } else {
            atmegaBuffer += c;
        }
    }
}

void sendDataToATmega() {
    Serial.println("--- Sending data to ATmega ---");
    sendCurrentLimits();
    if (present_Mode_option == 1) {
        sendOutletCommands(0, outlet1_enable);
        sendOutletCommands(1, outlet2_enable);
        sendOutletCommands(2, outlet3_enable);
    }
    Serial.println("--- Data sent ---");
}

void sendCurrentLimits() {
    out1_set_cur_limit = outlet1_set_current_limit;
    out2_set_cur_limit = outlet2_set_current_limit;
    out3_set_cur_limit = outlet3_set_current_limit;

    String out1_limit_msg = "!" + String(out1_set_cur_limit, 2) + "<";
    mySerial.println(out1_limit_msg);
    Serial.println("Sent: " + out1_limit_msg);

    String out2_limit_msg = "G" + String(out2_set_cur_limit, 2) + "<";
    mySerial.println(out2_limit_msg);
    Serial.println("Sent: " + out2_limit_msg);

    String out3_limit_msg = "H" + String(out3_set_cur_limit, 2) + "<";
    mySerial.println(out3_limit_msg);
    Serial.println("Sent: " + out3_limit_msg);
}

void sendOutletCommands(int outletIndex, int enableState) {

    if (outletIndex == 0) {
        if (enableState == 1) {
            mySerial.println("Aout1_en}");
            Serial.println("Sent: Aout1_en}");
        } else {
            mySerial.println("Bout1_dis)");
            Serial.println("Sent: Bout1_dis)");
        }
    } else if (outletIndex == 1) {
        if (enableState == 1) {
            mySerial.println("Cout2_en}");
            Serial.println("Sent: Cout2_en}");
        } else {
            mySerial.println("Dout2_dis)");
            Serial.println("Sent: Dout2_dis)");
        }
    } else if (outletIndex == 2) {
        if (enableState == 1) {
            mySerial.println("Eout3_en}");
            Serial.println("Sent: Eout3_en}");
        } else {
            mySerial.println("Fout3_dis)");
            Serial.println("Sent: Fout3_dis)");
        }
    }
    
}

void parseCurrentValues(String data) {
    int atIndex = data.indexOf('@');
    if (atIndex != -1) {
        int plusIndex = data.indexOf('+', atIndex);
        if (plusIndex != -1) {
            out1_cur_val = data.substring(atIndex + 1, plusIndex).toFloat();
            Serial.print("out1_cur_val = "); Serial.println(out1_cur_val, 4);
        }
    }

    int percentIndex = data.indexOf('%');
    if (percentIndex != -1) {
        int plusIndex = data.indexOf('+', percentIndex);
        if (plusIndex != -1) {
            out2_cur_val = data.substring(percentIndex + 1, plusIndex).toFloat();
            Serial.print("out2_cur_val = "); Serial.println(out2_cur_val, 4);
        }
    }

    int ampersandIndex = data.indexOf('&');
    if (ampersandIndex != -1) {
        int plusIndex = data.indexOf('+', ampersandIndex);
        if (plusIndex != -1) {
            out3_cur_val = data.substring(ampersandIndex + 1, plusIndex).toFloat();
            Serial.print("out3_cur_val = "); Serial.println(out3_cur_val, 4);
        }
    }
}

void parseEnergyValues(String data) {
    int hashIndex = data.indexOf('#');
    if (hashIndex != -1) {
        int lessThanIndex = data.indexOf('<', hashIndex);
        if (lessThanIndex != -1) {
            out1_engy_val = data.substring(hashIndex + 1, lessThanIndex).toFloat();
            Serial.print("out1_engy_val = "); Serial.println(out1_engy_val, 7);
        }
    }

    int dollarIndex = data.indexOf('$');
    if (dollarIndex != -1) {
        int lessThanIndex = data.indexOf('<', dollarIndex);
        if (lessThanIndex != -1) {
            out2_engy_val = data.substring(dollarIndex + 1, lessThanIndex).toFloat();
            Serial.print("out2_engy_val = "); Serial.println(out2_engy_val, 7);
        }
    }

    int questionIndex = data.indexOf('?');
    if (questionIndex != -1) {
        int lessThanIndex = data.indexOf('<', questionIndex);
        if (lessThanIndex != -1) {
            out3_engy_val = data.substring(questionIndex + 1, lessThanIndex).toFloat();
            Serial.print("out3_engy_val = "); Serial.println(out3_engy_val, 7);
        }
    }
}

void parseRelayTripTime(String data) {
    int caretIndex = data.indexOf('^');
    if (caretIndex != -1) {
        int pipeIndex = data.indexOf('=', caretIndex);
        if (pipeIndex != -1) {
            out1_relay_trip_time = data.substring(caretIndex + 1, pipeIndex).toFloat();
             if (out1_relay_trip_time  > 0.0) {                          // only log real trips
                logTripTimes(1, out1_relay_trip_time);    // log immediately
            Serial.print("out1 Relay Trip Time = "); Serial.println(out1_relay_trip_time, 4);
        }
    }
}
    int asteriskIndex = data.indexOf('*');
    if (asteriskIndex != -1) {
        int pipeIndex = data.indexOf('=', asteriskIndex);
        if (pipeIndex != -1) {
            out2_relay_trip_time = data.substring(asteriskIndex + 1, pipeIndex).toFloat();
             if (out2_relay_trip_time > 0.0) {
                logTripTimes(2, out2_relay_trip_time);    // log immediately
            Serial.print("out2 Relay Trip Time = "); Serial.println(out2_relay_trip_time, 4);
        }
    }
}
    int tildeIndex = data.indexOf('~');
    if (tildeIndex != -1) {
        int pipeIndex = data.indexOf('=', tildeIndex);
        if (pipeIndex != -1) {
           out3_relay_trip_time = data.substring(tildeIndex + 1, pipeIndex).toFloat();
           if (out3_relay_trip_time > 0.0) {
                logTripTimes(3, out3_relay_trip_time);    // log immediately
            Serial.print("out3 Relay Trip Time = "); Serial.println(out3_relay_trip_time, 4);
         }
        }
    }
}

void parseStartupState(String data) {

    // ── Outlet current limits ──────────────────────────────────────
    // Format: L8.50|
    int lIndex = data.indexOf('L');
    if (lIndex != -1) {
        int pipeIndex = data.indexOf('|', lIndex);
        if (pipeIndex != -1) {
            float newLimit = data.substring(lIndex + 1, pipeIndex).toFloat();
            if (newLimit >= 0.5 && newLimit <= 15.0) {
                outlet1_set_current_limit = newLimit;
                cur_lmt_out1_calc         = newLimit;
                Serial.print("Startup out1 current limit = ");
                Serial.println(outlet1_set_current_limit, 2);
            }
        }
    }

    // Format: M14.50|
    int mIndex = data.indexOf('M');
    if (mIndex != -1) {
        int pipeIndex = data.indexOf('|', mIndex);
        if (pipeIndex != -1) {
            float newLimit = data.substring(mIndex + 1, pipeIndex).toFloat();
            if (newLimit >= 0.5 && newLimit <= 15.0) {
                outlet2_set_current_limit = newLimit;
                cur_lmt_out2_calc         = newLimit;
                Serial.print("Startup out2 current limit = ");
                Serial.println(outlet2_set_current_limit, 2);
            }
        }
    }

    // Format: N3.00|
    int nIndex = data.indexOf('N');
    if (nIndex != -1) {
        int pipeIndex = data.indexOf('|', nIndex);
        if (pipeIndex != -1) {
            float newLimit = data.substring(nIndex + 1, pipeIndex).toFloat();
            if (newLimit >= 0.5 && newLimit <= 15.0) {
                outlet3_set_current_limit = newLimit;
                cur_lmt_out3_calc         = newLimit;
                Serial.print("Startup out3 current limit = ");
                Serial.println(outlet3_set_current_limit, 2);
            }
        }
    }

    // ── Outlet enable states ──────────────────────────────────────
    // Format: P0[  or  P1[
    int pIndex = data.indexOf('P');
    if (pIndex != -1) {
        int bracketIndex = data.indexOf('[', pIndex);
        if (bracketIndex != -1) {
            int val = data.substring(pIndex + 1, bracketIndex).toInt();
            outlet1_enable = (val == 1) ? 1 : 0;
            Serial.print("Startup out1 enable = ");
            Serial.println(outlet1_enable);
        }
    }

    // Format: Q0[  or  Q1[
    int qIndex = data.indexOf('Q');
    if (qIndex != -1) {
        int bracketIndex = data.indexOf('[', qIndex);
        if (bracketIndex != -1) {
            int val = data.substring(qIndex + 1, bracketIndex).toInt();
            outlet2_enable = (val == 1) ? 1 : 0;
            Serial.print("Startup out2 enable = ");
            Serial.println(outlet2_enable);
        }
    }

    // Format: R0[  or  R1[
    int rIndex = data.indexOf('R');
    if (rIndex != -1) {
        int bracketIndex = data.indexOf('[', rIndex);
        if (bracketIndex != -1) {
            int val = data.substring(rIndex + 1, bracketIndex).toInt();
            outlet3_enable = (val == 1) ? 1 : 0;
            Serial.print("Startup out3 enable = ");
            Serial.println(outlet3_enable);
        }
    }

     // ── Sync to server if any startup value was found ──────────────
    if (lIndex != -1 || mIndex != -1 || nIndex != -1 ||
        pIndex != -1 || qIndex != -1 || rIndex != -1) {
        startup_state_received = true;
        manual_state_sync_pending = true;
        lastManualSyncTime = millis();
        Serial.println("Startup state received — syncing to server");
    }
}

void logTripTimes(int outletNum, float tripTime) {
    String ts = getTimestamp();
    String tripLog =
        ts.substring(0, 10)     + "," +   // date
         ts.substring(11)        + "," +   // time
        String(outletNum)       + "," +
        String(tripTime, 4)     + "\r\n";

    appendFile(SD, "/trip_log.csv", tripLog.c_str());
    Serial.println("Trip event saved to SD: " + tripLog);
}

void updateDisplays() {
    irms_float_cur_sen1 = out1_cur_val;
    displayIRMS1(irms_float_cur_sen1);

    irms_float_cur_sen2 = out2_cur_val;
    displayIRMS2(irms_float_cur_sen2);

    irms_float_cur_sen3 = out3_cur_val;
    displayIRMS3(irms_float_cur_sen3);
}

// =====================================================================
// SD card helpers (DON'T MODIFY)
// =====================================================================
void writeFile(fs::FS &fs, const char *path, const char *message) {
  //Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    //Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    //Serial.println("File written");
  } else {
    //Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message) {
  //Serial.printf("Appending to file: %s\n", path);
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    //Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    //Serial.println("Message appended");
  } else {
    //Serial.println("Append failed");
  }
  file.close();
}