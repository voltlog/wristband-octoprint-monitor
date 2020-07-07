#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <OctoPrintAPI.h>

#include "NotoSansBold15.h"
#include "NotoSansBold36.h"
#include "voltlog.h"
#include "charge.h"

// Do not include "" around the array name!
#define AA_FONT_SMALL NotoSansBold15
#define AA_FONT_LARGE NotoSansBold36

const char* ssid = "ssid";
const char* password = "password";
const char* hostname = "T-Wristband";

WiFiClient client;

IPAddress ip(192, 168, 0, 106);       // IP address of your OctoPrint server
const int octoprint_httpPort = 5000;  // Port of your Octoprint server
String octoprint_apikey = "-----------------------------"; // Octoprint API key
OctoprintApi api(client, ip, octoprint_httpPort, octoprint_apikey); 
 
#define TP_PIN_PIN          33
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define IMU_INT_PIN         38
#define RTC_INT_PIN         34
#define BATT_ADC_PIN        35
#define VBUS_PIN            36
#define TP_PWR_PIN          25
#define LED_PIN             4
#define CHARGE_PIN          32

#define VOLTLOG_GREEN       0x8E27      /*   140, 198,   62 */

//variables for blinking an LED with Millis
const int led = 4; // ESP32 Pin to which onboard LED is connected
uint32_t previousMillis = 0;  // will store last time LED was updated
uint32_t previousMs = 0;      // will store last time 
uint32_t interval = 1000;  // interval at which to blink (milliseconds)
uint32_t refresh_rate = 5000;  //interval in ms for updating screen.
int ledState = LOW;  // ledState used to set the LED

char buff[256];
int vref = 1100;
uint8_t omm = 99;
uint8_t func_select = 0;
uint32_t targetTime = 0;       // for next 1 second timeout
uint32_t pressedTime = 0;
bool charge_indication = false;
bool otaStart = false;
bool initial = 1;
bool pressed = false;

int bed_temp_actual;
int bed_temp_target;
int tool0_temp_actual;
int tool0_temp_target;
long progress_completion;
long progress_printtime;
long progress_printtime_left;
String printer_status;   

TFT_eSPI tft = TFT_eSPI();  // Invoke library
WiFiManager wifiManager;

void setup() {
  pinMode(led, OUTPUT);

  // Setup TFT screen
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextFont(2);
  tft.pushImage(0, 0,  160, 80, ttgo);

  // Setup Serial
  Serial.begin(115200);
  Serial.println("Booting");

  // Setup Wifi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(ssid, password);
  
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Setup touch sensing
  pinMode(TP_PIN_PIN, INPUT);
  pinMode(TP_PWR_PIN, PULLUP); // Must be set to pull-up output mode in order to wake up in deep sleep mode
  digitalWrite(TP_PWR_PIN, HIGH);

  // Setup charge indication
  pinMode(CHARGE_PIN, INPUT_PULLUP);
    attachInterrupt(CHARGE_PIN, [] {
        charge_indication = true;
    }, CHANGE);

    if (digitalRead(CHARGE_PIN) == LOW) {
        charge_indication = true;
    }

  // Setup OTA updates
  setupOTA();
    


  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

String getVoltage()
{
    uint16_t v = analogRead(BATT_ADC_PIN);
    float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    return String(battery_voltage) + "V";
}

void drawProgressBar(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h, uint8_t percentage, uint16_t frameColor, uint16_t barColor)
{
    if (percentage == 0) {
        tft.fillRoundRect(x0, y0, w, h, 3, TFT_BLACK);
    }
    uint8_t margin = 2;
    uint16_t barHeight = h - 2 * margin;
    uint16_t barWidth = w - 2 * margin;
    tft.drawRoundRect(x0, y0, w, h, 3, frameColor);
    tft.fillRect(x0 + margin, y0 + margin, barWidth * percentage / 100.0, barHeight, barColor);
}

void setupOTA()
{
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("T-Wrist-OTA");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
      otaStart = true;
      tft.setTextSize(1);
      tft.fillScreen(TFT_BLACK);
      tft.drawString("Updating...", 25, 55 );
    })
    .onEnd([]() {
      Serial.println("\nEnd");
      delay(500);
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      int percentage = (progress / (total / 100));
      tft.setTextDatum(TC_DATUM);
      tft.setTextSize(1);
      tft.setTextPadding(tft.textWidth(" 888% "));
      tft.drawString(String(percentage) + "%", 120, 55);
      drawProgressBar(10, 30, 140, 15, percentage, TFT_WHITE, VOLTLOG_GREEN);
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");

      tft.fillScreen(TFT_BLACK);
      tft.drawString("Update Failed", tft.width() / 2 - 20, 55 );
      delay(3000);
      otaStart = false;
      initial = 1;
      targetTime = millis() + 1000;
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      omm = 99;
    });
    ArduinoOTA.begin();
}

void blink_led() {
    //loop to blink without delay
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
  // save the last time you blinked the LED
  previousMillis = currentMillis;
  // if the LED is off turn it on and vice-versa:
  ledState = not(ledState);
  // set the LED with the ledState of the variable:
  digitalWrite(led,  ledState);
  }
 }

 bool Time_elapsed(uint32_t duration)
 {
  //duration in miliseconds
   uint32_t currentMillis = millis();
   if (currentMillis - previousMs >= duration) {
  // save the last time it happened
  previousMs = currentMillis;
  // signal time elapsed
  return true;
  } else return false;
 }

  void Octoprint_Show()
{
  if(Time_elapsed(refresh_rate))
  {
    refresh_Octoprint();  //get new data

    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    //show target temp data
    snprintf(buff, sizeof(buff), "TOOL %d", tool0_temp_target);
    tft.drawString(buff, 0, 0);
    snprintf(buff, sizeof(buff), "BED %d", bed_temp_target);
    tft.drawString(buff, 92, 0);
    //show actual temp data
    tft.setTextSize(2);
    if(tool0_temp_target > 0) tft.setTextColor(TFT_RED, TFT_BLACK); else tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buff, sizeof(buff), "%d`C", tool0_temp_actual);
    tft.drawString(buff, 0, 13);
    if(bed_temp_target > 0) tft.setTextColor(TFT_RED, TFT_BLACK); else tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buff, sizeof(buff), "%d`C", bed_temp_actual);
    tft.drawString(buff, 90, 13);
    //show job status
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    snprintf(buff, sizeof(buff), " %d%% done", progress_completion);
    tft.drawString(printer_status+buff, 0, 42);
    //show time statistics
    snprintf(buff, sizeof(buff), "Total %dm ", progress_printtime/60);  //minutes
    tft.drawString(buff, 0, 58);
    snprintf(buff, sizeof(buff), "Left %dm", progress_printtime_left/60); //minutes
    tft.drawString(buff, 64, 58);

    
    delay(200);
  }
}

void refresh_Octoprint(void)
{
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status

  if(api.getPrinterStatistics()){
    bed_temp_actual = api.printerStats.printerBedTempActual;
    bed_temp_target = api.printerStats.printerBedTempTarget;
    tool0_temp_actual = api.printerStats.printerTool0TempActual;
    tool0_temp_target = api.printerStats.printerTool0TempTarget;
    progress_completion = api.printJob.progressCompletion;
    progress_printtime = api.printJob.progressPrintTime;
    progress_printtime_left = api.printJob.progressPrintTimeLeft;
    
    if(api.printerStats.printerStatePrinting == 1){
        printer_status = "Printing"; //if printing, set status to printing
      }else if(api.printerStats.printerStatepaused == 1){
        printer_status = "Paused";
      }else if(api.printerStats.printerStateready == 1){
        printer_status = "Ready";
      }else{
        printer_status = "Other";
      }
  }

  if(api.getPrintJob()){
    
  }
}
}
void loop() {
  ArduinoOTA.handle();

//! If OTA starts, skip the following operation
    if (otaStart)
        return;
        
  if (charge_indication) {
        charge_indication = false;
        if (digitalRead(CHARGE_PIN) == LOW) {
            tft.pushImage(140, 55, 16, 16, charge);
        } else {
            tft.fillRect(140, 55, 16, 16, TFT_BLACK);
        }
    }

    if (digitalRead(TP_PIN_PIN) == HIGH) {
        if (!pressed) {
            initial = 1;
            targetTime = millis() + 1000;
            tft.fillScreen(TFT_BLACK);
            omm = 99;
            func_select = func_select + 1 > 2 ? 0 : func_select + 1;
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            pressed = true;
            pressedTime = millis();
        } 
    } else {
        pressed = false;
    }

    switch (func_select) {
    case 0:
        Octoprint_Show();
        break;
    case 1:
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Press again to wake up",  tft.width() / 2, tft.height() / 2 );
        //IMU.setSleepEnabled(true);
        Serial.println("Go to Sleep");
        delay(3000);
        tft.writecommand(ST7735_SLPIN);
        tft.writecommand(ST7735_DISPOFF);
        esp_sleep_enable_ext1_wakeup(GPIO_SEL_33, ESP_EXT1_WAKEUP_ANY_HIGH);
        esp_deep_sleep_start();
        break;
    default:
        break;
  }
}
