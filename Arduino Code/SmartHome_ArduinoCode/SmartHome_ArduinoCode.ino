#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Servo.h>

// Initialize the LCD (Address 0x27, 16 columns, 2 rows)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Initialize Servo for Main Door
Servo doorServo;

// Pin Definitions
const int ONE_WIRE_BUS = 9;      // DS18B20 Temperature sensor on Digital Pin 9
const int potPin = A0;           // Potentiometer wiper on A0
const int fanPin = 8;            // Fan MOSFET on Digital Pin 8
const int windowPin = 7;         // Window limit switch on Digital Pin 7
const int buzzerPin = 6;         // Alarm buzzer on Digital Pin 6
const int buttonPin = 5;         // Security mode push button on Digital Pin 5
const int servoPin = 4;          // Door servo motor on Digital Pin 4
const int pirPin = 2;            // PIR motion sensor on Digital Pin 2
const int ldrPin = A3;           // LDR sensor module on Analog Pin A3
const int extLightPin = 12;      // Exterior Light LED (Motion-triggered) on Digital Pin 12
const int indLightPin = 13;      // Indoor Light LED on Digital Pin 13
const int indLightBtnPin = 11;   // Indoor Light Manual Push Button on Digital Pin 11

// I2C Keypad Expander Address (Usually 0x20)
const int KEYPAD_I2C_ADDR = 0x20; 

// Setup OneWire and Dallas Temperature
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// State Tracking Variables
bool securityArmed = false;     
bool lastButtonState = HIGH;
bool lastIndLightBtnState = HIGH;
bool manualOverride = false;    // Tracks if manual override (Force ON) is active

String enteredPin = "";         
const String correctPin = "1234"; 
unsigned long doorOpenTime = 0;
bool doorIsOpen = false;
const unsigned long doorHoldTime = 4000; // Door stays open for 4 seconds

void setup() {
  Wire.begin(); // Join I2C bus for LCD and Keypad expander

  pinMode(fanPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(extLightPin, OUTPUT);
  pinMode(indLightPin, OUTPUT);
  pinMode(windowPin, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(indLightBtnPin, INPUT_PULLUP); // Indoor light button with internal pull-up
  pinMode(pirPin, INPUT);
  pinMode(ldrPin, INPUT);
  
  doorServo.attach(servoPin);
  doorServo.write(0); // Door closed position (0 degrees)

  Serial.begin(9600);
  sensors.begin();

  // Initialize LCD
  lcd.init();                        
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Home Sys");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1500);
  lcd.clear();
}

// Function to scan the 4x4 Keypad via PCF8574 I2C Expander
char readI2CKeypad() {
  char keys[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
  };

  for (int row = 0; row < 4; row++) {
    Wire.beginTransmission(KEYPAD_I2C_ADDR);
    Wire.write(~(1 << row)); 
    Wire.endTransmission();

    Wire.requestFrom(KEYPAD_I2C_ADDR, 1);
    if (Wire.available()) {
      uint8_t p = Wire.read();
      for (int col = 0; col < 4; col++) {
        if (!(p & (1 << (col + 4)))) { 
          Wire.beginTransmission(KEYPAD_I2C_ADDR);
          Wire.write(0xFF);
          Wire.endTransmission();
          return keys[row][col];
        }
      }
    }
  }

  Wire.beginTransmission(KEYPAD_I2C_ADDR);
  Wire.write(0xFF);
  Wire.endTransmission();
  
  return '\0'; 
}

void loop() {
  // 1. Read Potentiometer and map to temperature threshold (0 to 100 °C)
  int potValue = analogRead(potPin);                     
  float tempThreshold = map(potValue, 0, 1023, 0, 100); 

  // 2. Read DS18B20 Temperature Sensor
  sensors.requestTemperatures();               
  float temperatureC = sensors.getTempCByIndex(0); 

  if (temperatureC == -127.31 || temperatureC == -127.00) {
    Serial.println(F("Error: Could not find DS18B20 temperature sensor!"));
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error!   ");
    delay(2000);
    return;
  }

  // 3. Read Sensors & Inputs
  bool isWindowOpen = (digitalRead(windowPin) == LOW);
  bool motionDetected = (digitalRead(pirPin) == HIGH);
  int ldrValue = analogRead(ldrPin);

  // 4. Toggle Security Mode via Push Button (Pin 5)
  bool currentButtonState = digitalRead(buttonPin);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    securityArmed = !securityArmed; 
    delay(200); 
  }
  lastButtonState = currentButtonState;

  // 5. Toggle Indoor Light Override via Push Button (Pin 11)
  bool currentIndLightBtnState = digitalRead(indLightBtnPin);
  if (currentIndLightBtnState == LOW && lastIndLightBtnState == HIGH) {
    manualOverride = !manualOverride; // Toggle between Force-ON and Sensor mode
    delay(200);                       // Debounce delay
  }
  lastIndLightBtnState = currentIndLightBtnState;

  // 6. Keypad Password Handling for Main Door Access
  char key = readI2CKeypad();
  if (key != '\0') {
    delay(200); 
    if (key == '#') { 
      if (enteredPin == correctPin) {
        doorServo.write(90); 
        doorIsOpen = true;
        doorOpenTime = millis();
        lcd.setCursor(0, 1);
        lcd.print("Door Unlocked!  ");
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Incorrect PIN!  ");
      }
      enteredPin = "";
      delay(1500);
    } else if (key == '*') { 
      enteredPin = "";
      lcd.setCursor(0, 1);
      lcd.print("PIN Cleared     ");
      delay(1000);
    } else {
      if (enteredPin.length() < 4) {
        enteredPin += key;
      }
    }
  }

  // Auto-close door after preset delay
  if (doorIsOpen && (millis() - doorOpenTime >= doorHoldTime)) {
    doorServo.write(0); 
    doorIsOpen = false;
  }

  // 7. Fan Control Logic (Climate Control)
  int fanSpeed = (temperatureC >= tempThreshold) ? 255 : 0;
  analogWrite(fanPin, fanSpeed);

  // 8. Security System & Lighting Logic
  bool securityAlarmTriggered = (securityArmed && isWindowOpen);
  digitalWrite(buzzerPin, securityAlarmTriggered ? HIGH : LOW);

  // --- PIR Motion Light with a Latch Timer (Stays on for 3 seconds once triggered) ---
  static unsigned long pirTimer = 0;
  if (motionDetected) {
    pirTimer = millis(); // Reset timer every time motion is detected
  }
  bool extLightState = (millis() - pirTimer < 3000);
  digitalWrite(extLightPin, extLightState ? HIGH : LOW);

  // --- Indoor Light Logic (Manual Force-ON Override or Auto LDR fallback) ---
  bool indLightState = false;
  if (manualOverride) {
    indLightState = HIGH; // Forces light ON regardless of LDR sensor
  } else {
    // Automatic LDR fallback logic
    static bool autoIndLightState = false;
    if (ldrValue > 250) {
      autoIndLightState = true;   // Turn ON when dark
    } else if (ldrValue < 200) {
      autoIndLightState = false;  // Turn OFF when bright
    }
    indLightState = autoIndLightState;
  }
  digitalWrite(indLightPin, indLightState ? HIGH : LOW);
  
  // 9. Update LCD Display (16x2) cleanly without glitching
  lcd.setCursor(0, 0);
  lcd.print("Sec:");
  lcd.print(securityArmed ? "ON " : "OFF");
  lcd.print(" T=");
  lcd.print(temperatureC, 1);
  lcd.print("C ");

  lcd.setCursor(0, 1);
  if (enteredPin.length() > 0) {
    lcd.print("PIN:");
    lcd.print(enteredPin);
    for (int i = enteredPin.length(); i < 12; i++) lcd.print(" ");
  } else {
    // Show Threshold and Window status on row 2
    lcd.print("Thr:");
    lcd.print((int)tempThreshold);
    lcd.print("C ");
    lcd.print(isWindowOpen ? "W:OPEN " : "W:CLSD ");
  }

  // 10. Print to Serial Monitor
  Serial.print("Threshold: "); Serial.print(tempThreshold);
  Serial.print(" °C | Temp: "); Serial.print(temperatureC);
  Serial.print(" °C | LDR: "); Serial.print(ldrValue);
  Serial.print(" | Fan: "); Serial.print(fanSpeed > 0 ? "ON" : "OFF");
  Serial.print(" | Door: "); Serial.print(doorIsOpen ? "OPEN" : "CLOSED");
  Serial.print(" | Sec: "); Serial.println(securityArmed ? "ARMED" : "DISARMED");

  delay(200); 
}
