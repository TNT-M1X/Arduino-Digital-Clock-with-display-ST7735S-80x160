#include <Wire.h>
#include <RTClib.h>
#include <TM1637Display.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Піни для ST7735S 80x160 TFT дисплея
#define TFT_CS    10
#define TFT_RST   12 
#define TFT_DC    9

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Налаштування TM1637 (для часу HH:MM)
#define CLK_PIN 2
#define DIO_PIN 3
TM1637Display timeDisplay(CLK_PIN, DIO_PIN);

// Піни кнопок
#define BTN_MODE   4  
#define BTN_UP     5  
#define BTN_DOWN   6  
#define BTN_SELECT 7  

// Пін керування динаміком через транзистор D304X
#define SPEAKER_PIN 8

// Піни DIP-перемикачів (4 тумблери)
#define DIP_NIGHT    A0 
#define DIP_TEMP_F   A1 
#define DIP_ALARM_EN A2 
#define DIP_LOCK     A3 

RTC_DS3231 rtc;
bool rtcFound = false;

// Режими роботи пристрою
enum SystemMode {
  MODE_DASHBOARD,     
  MODE_MENU,          
  MODE_SET_ALARM,     
  MODE_SET_TIMER,     
  MODE_SET_INTERVALS, 
  MODE_SET_MELODY,    
  MODE_SET_VOLUME,    
  MODE_SET_DURATION,  
  MODE_ALARM_RINGING  
};
SystemMode currentMode = MODE_DASHBOARD;

int menuIndex = 0;
const int totalMenuItems = 6; 

int alarmHour = 7;
int alarmMinute = 30;

int timerSettingSec = 60;
int timerRemaining = 0;
bool timerIsRunning = false;

int selectedMelody = 0;
int soundVolume = 2;
int alarmDurationSec = 60; 
unsigned long alarmStartTime = 0; 

int mainDisplayDuration = 10;   
int altDisplayDuration = 5;     

bool isAltScreen = false;
unsigned long lastScreenSwitch = 0;

unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 220;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  pinMode(DIP_NIGHT, INPUT_PULLUP);
  pinMode(DIP_TEMP_F, INPUT_PULLUP);
  pinMode(DIP_ALARM_EN, INPUT_PULLUP);
  pinMode(DIP_LOCK, INPUT_PULLUP);

  // 1. Ініціалізація екрана
  tft.initR(INITR_MINI160x80);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);

  // Напис при запускi
  tft.setCursor(10, 35);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_GREEN);
  tft.print(F("INITIALIZING..."));
  delay(1000);

  timeDisplay.setBrightness(0x0a);

  // 2. Безпечна ініціалізація RTC
  if (rtc.begin()) {
    rtcFound = true;
  } else {
    Serial.println(F("Помилка: RTC не знайдено!"));
  }
}

void playMelody(int melodyType, int volume) {
  int pauseAfterNote = (volume == 0) ? 250 : (volume == 1) ? 100 : 30;

  if (melodyType == 0) {
    tone(SPEAKER_PIN, 2000, 100);
    delay(100 + pauseAfterNote);
  } 
  else if (melodyType == 1) {
    for (int freq = 1200; freq <= 2200; freq += 200) {
      tone(SPEAKER_PIN, freq, 25);
      delay(25);
    }
    delay(pauseAfterNote);
  } 
  else if (melodyType == 2) {
    int notes[] = {1046, 1318, 1568};
    for (int i = 0; i < 3; i++) {
      tone(SPEAKER_PIN, notes[i], 80);
      delay(100);
    }
    delay(pauseAfterNote);
  }
}

void handleButtons() {
  if (millis() - lastButtonPress < debounceDelay) return;

  if (currentMode == MODE_ALARM_RINGING) {
    if (digitalRead(BTN_MODE) == LOW || digitalRead(BTN_UP) == LOW || 
        digitalRead(BTN_DOWN) == LOW || digitalRead(BTN_SELECT) == LOW) {
      lastButtonPress = millis();
      noTone(SPEAKER_PIN);
      currentMode = MODE_DASHBOARD;
    }
    return;
  }

  if (digitalRead(BTN_MODE) == LOW) {
    lastButtonPress = millis();
    if (currentMode == MODE_DASHBOARD) {
      currentMode = MODE_MENU;
      menuIndex = 0;
    } else {
      currentMode = MODE_DASHBOARD;
    }
    return;
  }

  switch (currentMode) {
    case MODE_DASHBOARD:
      if (digitalRead(BTN_SELECT) == LOW) {
        lastButtonPress = millis();
        isAltScreen = !isAltScreen;
      }
      break;

    case MODE_MENU:
      if (digitalRead(BTN_UP) == LOW) {
        lastButtonPress = millis();
        menuIndex = (menuIndex - 1 + totalMenuItems) % totalMenuItems;
      }
      if (digitalRead(BTN_DOWN) == LOW) {
        lastButtonPress = millis();
        menuIndex = (menuIndex + 1) % totalMenuItems;
      }
      if (digitalRead(BTN_SELECT) == LOW) {
        lastButtonPress = millis();
        if (menuIndex == 0) currentMode = MODE_SET_ALARM;
        else if (menuIndex == 1) currentMode = MODE_SET_TIMER;
        else if (menuIndex == 2) currentMode = MODE_SET_INTERVALS;
        else if (menuIndex == 3) currentMode = MODE_SET_MELODY;
        else if (menuIndex == 4) currentMode = MODE_SET_VOLUME;
        else if (menuIndex == 5) currentMode = MODE_SET_DURATION;
      }
      break;

    case MODE_SET_ALARM:
      if (digitalRead(BTN_UP) == LOW) { lastButtonPress = millis(); alarmMinute = (alarmMinute + 5) % 60; }
      if (digitalRead(BTN_DOWN) == LOW) { lastButtonPress = millis(); alarmHour = (alarmHour + 1) % 24; }
      break;

    case MODE_SET_TIMER:
      if (digitalRead(BTN_UP) == LOW) { lastButtonPress = millis(); timerSettingSec += 15; }
      if (digitalRead(BTN_DOWN) == LOW) { lastButtonPress = millis(); if (timerSettingSec > 15) timerSettingSec -= 15; }
      if (digitalRead(BTN_SELECT) == LOW) { 
        lastButtonPress = millis(); 
        timerRemaining = timerSettingSec; 
        timerIsRunning = true; 
        currentMode = MODE_DASHBOARD; 
      }
      break;

    case MODE_SET_INTERVALS:
      if (digitalRead(BTN_UP) == LOW) { lastButtonPress = millis(); mainDisplayDuration = (mainDisplayDuration % 30) + 5; }
      if (digitalRead(BTN_DOWN) == LOW) { lastButtonPress = millis(); altDisplayDuration = (altDisplayDuration % 15) + 3; }
      break;

    case MODE_SET_MELODY:
      if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
        lastButtonPress = millis();
        selectedMelody = (selectedMelody + 1) % 3;
        playMelody(selectedMelody, soundVolume);
      }
      break;

    case MODE_SET_VOLUME:
      if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
        lastButtonPress = millis();
        soundVolume = (soundVolume + 1) % 3;
        playMelody(selectedMelody, soundVolume);
      }
      break;

    case MODE_SET_DURATION:
      if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
        lastButtonPress = millis();
        if (alarmDurationSec == 30) alarmDurationSec = 60;
        else if (alarmDurationSec == 60) alarmDurationSec = 120;
        else if (alarmDurationSec == 120) alarmDurationSec = 300;
        else alarmDurationSec = 30;
      }
      break;
  }
}

void loop() {
  handleButtons();
  
  static SystemMode lastMode = (SystemMode)-1;
  static int lastSecond = -1;
  static int lastTimer = -1;

  int h = 12, m = 34, s = 0;
  int d = 1, mo = 1, y = 2026;
  float temp = 25.0;

  if (rtcFound) {
    DateTime now = rtc.now();
    h = now.hour();
    m = now.minute();
    s = now.second();
    d = now.day();
    mo = now.month();
    y = now.year();
    temp = rtc.getTemperature();
  }

  bool nightMode = (digitalRead(DIP_NIGHT) == LOW);
  bool useFahrenheit = (digitalRead(DIP_TEMP_F) == LOW);
  bool hardwareAlarmEnabled = (digitalRead(DIP_ALARM_EN) == LOW);
  bool lockScreen = (digitalRead(DIP_LOCK) == LOW);

  if (nightMode) {
    timeDisplay.setBrightness(0x02);
  } else {
    timeDisplay.setBrightness(0x0a);
  }

  static unsigned long lastTimerTick = 0;
  if (timerIsRunning && millis() - lastTimerTick >= 1000) {
    lastTimerTick = millis();
    if (timerRemaining > 0) {
      timerRemaining--;
      if (timerRemaining == 0) {
        timerIsRunning = false;
        currentMode = MODE_ALARM_RINGING;
        alarmStartTime = millis();
      }
    }
  }

  if (rtcFound && hardwareAlarmEnabled && currentMode != MODE_ALARM_RINGING) {
    DateTime now = rtc.now();
    if (now.hour() == alarmHour && now.minute() == alarmMinute && now.second() == 0) {
      currentMode = MODE_ALARM_RINGING;
      alarmStartTime = millis();
    }
  }

  if (currentMode == MODE_ALARM_RINGING) {
    if (millis() - alarmStartTime >= (unsigned long)(alarmDurationSec * 1000UL)) {
      noTone(SPEAKER_PIN);
      currentMode = MODE_DASHBOARD;
    } else {
      playMelody(selectedMelody, soundVolume);
    }
  }

  unsigned long currentMillis = millis();
  unsigned long currentInterval = isAltScreen ? (altDisplayDuration * 1000UL) : (mainDisplayDuration * 1000UL);
  
  bool forceRedraw = false;
  if (!lockScreen && currentMode == MODE_DASHBOARD && (currentMillis - lastScreenSwitch >= currentInterval)) {
    lastScreenSwitch = currentMillis;
    isAltScreen = !isAltScreen;
    forceRedraw = true; 
  }

  int timeVal = h * 100 + m;
  uint8_t colon = (s % 2 == 0) ? 0b10000000 : 0x00;
  timeDisplay.showNumberDecEx(timeVal, colon, true, 4, 0);

  if (currentMode != lastMode || forceRedraw) {
    tft.fillScreen(ST7735_BLACK);
    lastMode = currentMode;
    
    if (currentMode == MODE_DASHBOARD) {
      tft.setCursor(5, 2);
      tft.setTextSize(1);
      tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(isAltScreen ? F("- TEMPERATURE -") : F("-- SECONDS & DATE --"));
    }
  }

  switch (currentMode) {
    case MODE_DASHBOARD:
      if (!isAltScreen) {
        if (s != lastSecond) {
          lastSecond = s;
          tft.setCursor(20, 20);
          tft.setTextSize(3);
          tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
          if (s < 10) tft.print("0");
          tft.print(s); 
          tft.print(" ");

          tft.setCursor(10, 58);
          tft.setTextSize(2);
          tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
          tft.print(d); tft.print("."); tft.print(mo); tft.print("."); tft.print(y % 100);
        }
      } else {
        tft.setCursor(10, 20);
        tft.setTextSize(3);
        tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
        if (useFahrenheit) temp = temp * 9.0 / 5.0 + 32.0;
        tft.print((int)temp);
        tft.print((char)247);
        tft.print(useFahrenheit ? F("F") : F("C"));

        tft.setCursor(10, 58);
        tft.setTextSize(1);
        tft.setTextColor(hardwareAlarmEnabled ? ST7735_GREEN : ST7735_RED, ST7735_BLACK);
        if (hardwareAlarmEnabled) {
          tft.print("ALARM: ");
          if (alarmHour < 10) tft.print("0");
          tft.print(alarmHour); tft.print(":");
          if (alarmMinute < 10) tft.print("0");
          tft.print(alarmMinute);
          tft.print("  ");
        } else {
          tft.print(F("ALARM: OFF      "));
        }
      }
      
      if (timerIsRunning && timerRemaining != lastTimer) {
        lastTimer = timerRemaining;
        tft.setCursor(85, 2);
        tft.setTextSize(1);
        tft.setTextColor(ST7735_MAGENTA, ST7735_BLACK);
        tft.print("T:"); tft.print(timerRemaining); tft.print("s ");
      } else if (!timerIsRunning && lastTimer != -1) {
        lastTimer = -1;
        tft.fillRect(85, 2, 70, 8, ST7735_BLACK);
      }
      break;

    case MODE_MENU:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== SELECT MENU ==="));
      
      {
        const char* menuNames[] = {"1. Alarm Time", "2. Timer Setup", "3. Intervals", "4. Melody", "5. Volume", "6. Duration"};
        for (int i = 0; i < 6; i++) {
          tft.setCursor(5, 14 + (i * 10));
          tft.setTextSize(1);
          if (i == menuIndex) {
            tft.setTextColor(ST7735_BLACK, ST7735_YELLOW); 
            tft.print(">"); tft.print(menuNames[i]); tft.print("             ");
          } else {
            tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
            tft.print(" "); tft.print(menuNames[i]); tft.print("             ");
          }
        }
      }
      break;

    case MODE_SET_ALARM:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== ALARM TIME ==="));
      tft.setCursor(15, 25); tft.setTextSize(3); tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
      if (alarmHour < 10) tft.print("0");
      tft.print(alarmHour); tft.print(":");
      if (alarmMinute < 10) tft.print("0");
      tft.print(alarmMinute);
      tft.setCursor(5, 60); tft.setTextSize(1); tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.print(hardwareAlarmEnabled ? F("DIP 3: ON") : F("DIP 3: OFF"));
      break;

    case MODE_SET_TIMER:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== TIMER SETUP ==="));
      tft.setCursor(20, 25); tft.setTextSize(3); tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
      tft.print(timerSettingSec); tft.print(F("s  "));
      tft.setCursor(5, 60); tft.setTextSize(1); tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.print(F("Select: Start"));
      break;

    case MODE_SET_INTERVALS:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== INTERVALS ==="));
      tft.setCursor(5, 22); tft.setTextSize(2); tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.print(F("Main: ")); tft.print(mainDisplayDuration); tft.print("s ");
      tft.setCursor(5, 45);
      tft.print(F("Alt:  ")); tft.print(altDisplayDuration); tft.print("s ");
      break;

    case MODE_SET_MELODY:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== MELODY ==="));
      tft.setCursor(15, 28); tft.setTextSize(2); tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
      tft.print(F("Style #")); tft.print(selectedMelody + 1);
      tft.setCursor(5, 58); tft.setTextSize(1); tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.print(F("UP/DOWN to test"));
      break;

    case MODE_SET_VOLUME:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== VOLUME ==="));
      tft.setCursor(15, 28); tft.setTextSize(2); tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
      if (soundVolume == 0) tft.print(F("QUIET  "));
      else if (soundVolume == 1) tft.print(F("MEDIUM "));
      else tft.print(F("LOUD   "));
      tft.setCursor(5, 58); tft.setTextSize(1); tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.print(F("UP/DOWN to change"));
      break;

    case MODE_SET_DURATION:
      tft.setCursor(5, 2); tft.setTextSize(1); tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
      tft.print(F("=== DURATION ==="));
      tft.setCursor(15, 28); tft.setTextSize(2); tft.setTextColor(ST7735_MAGENTA, ST7735_BLACK);
      tft.print(alarmDurationSec); tft.print(F(" sec "));
      tft.setCursor(5, 58); tft.setTextSize(1); tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
      tft.print(F("UP/DOWN: 30s-5m"));
      break;

    case MODE_ALARM_RINGING:
      tft.fillScreen(ST7735_RED);
      tft.setCursor(15, 20);
      tft.setTextSize(2);
      tft.setTextColor(ST7735_WHITE, ST7735_RED);
      tft.print(F("WAKE UP!"));
      tft.setCursor(10, 50);
      tft.setTextSize(1);
      tft.print(F("Press ANY button"));
      break;
  }

  delay(50);
}