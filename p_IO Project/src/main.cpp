#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <time.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
// Display and Pin Configurations
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3c

#define BUZZER 5
#define LED_1 15
#define LED_2 2
#define PB_CANCEL 34
#define PB_OK 32
#define PB_UP 33
#define PB_DOWN 35
#define DHTPIN 12
#define SERVO_PIN 13

#define NTP_SERVER "time.google.com"
#define UTC_OFFSET_DST 0
#define LDR_PIN 36 // analog pin

char tempAr[6];
// LDR Configuration
// Global Variables
int UTC_OFFSET = 0;
int offset_hours = 0;
int offset_mins = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;
String dayOfWeek = "";
Servo shade_servo;

float theta_offset = 30;
float gammma = 0.75;
float Tmed = 30.0;

bool alarm_enabled = false;
const int N_ALARMS = 3;
int alarm_hours[N_ALARMS] = {0, 1, 0};
int alarm_minutes[N_ALARMS] = {1, 10, 0};
bool alarm_triggered[N_ALARMS] = {false, false, false};
int temp_alarm_hour = 25; // to store the alarm time when the user presses the ok button _initially set to 25 to avoid any confusion with the actual time
int temp_alarm_minute = 00;

// Musical Notes
const int N_NOTES = 8;
const int MUSICAL_NOTES[] = {262, 294, 330, 349, 392, 440, 494, 523};

// Menu Configuration
enum MenuState
{
  HOME_SCREEN,
  MAIN_MENU,
  TIME_ZONE_SETTING,
  ALARM_SETTING
};
#define MAX_SAMPLES 100
float ldr_readings[MAX_SAMPLES];
int ldr_index = 0;
int ldr_sample_count = 24;

unsigned long lastLdrSample = 0;
unsigned long lastLdrUpload = 0;
int valid_sample_count = 0;
int ts = 5;   // Sampling interval (seconds)
int tu = 120; // Upload interval (seconds)

const int MAX_VISIBLE_MENU_ITEMS = 3;
const String MENU_ITEMS[] = {
    "Set Time Zone",
    "Set Alarm 1",
    "Set Alarm 2",
    "Set Alarm 3",
    "Disable Alarms"};

const String DAYS_OF_WEEK[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"};

// Global Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;

// Current States
MenuState currentState = HOME_SCREEN;
int currentMenuIndex = 0;
int menuScrollOffset = 0;

/***************************************************************************************************
 * Function Prototypes
 **************************************************************************************************/
void display_time();
void update_time_with_check_alarm();
void update_time();
void go_to_menu();
void handle_cancel_button();
void reset_to_home_screen();
void display_menu();
int wait_for_menu_button();
void set_time_zone();
void set_alarm(int alarmIndex);
void ring_alarm();
void disable_all_alarms();
void print_line(String text, int column, int row, int text_size);
void check_temp();
void sample_ldr();
void reset_alarm_triggered();
void update_servo_angle();
void connectToBroker();
void setupMqtt();
void receiveCallback(char *topic, byte *payload, unsigned int length);
void publish_light_average();

/***************************************************************************************************
 * setup()
 * Initializes serial, pins, Wi-Fi, time, and OLED display.
 **************************************************************************************************/
void setup()
{
  Serial.begin(115200);

  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(PB_CANCEL, INPUT_PULLUP);
  pinMode(PB_OK, INPUT_PULLUP);
  pinMode(PB_UP, INPUT_PULLUP);
  pinMode(PB_DOWN, INPUT_PULLUP);
  pinMode(LDR_PIN, INPUT);

  dhtSensor.setup(DHTPIN, DHTesp::DHT22);
  shade_servo.attach(SERVO_PIN);
  shade_servo.setPeriodHertz(50);           // Standard 50Hz for servos
  shade_servo.attach(SERVO_PIN, 500, 2400); // Min and max pulse widths

  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected!");

  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);

  struct tm timeinfo;
  int tries = 0;
  while (!getLocalTime(&timeinfo) && tries < 20)
  {
    Serial.println("Waiting for NTP time sync...");
    delay(500);
    tries++;
  }
  if (tries >= 20)
  {
    Serial.println("Failed to sync time from NTP!");
  }
  else
  {
    Serial.println("Time successfully synced!");
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  display.ssd1306_command(0x81);
  display.ssd1306_command(0xFF);

  display.clearDisplay();
  display.setTextWrap(false);

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 16);
  display.println("Welcome");
  display.setCursor(10, 36);
  display.println("Medibox!");
  display.display();
  delay(1000);

  display.clearDisplay();
  display.display();
  setupMqtt();
  Serial.println("Setup complete!");
}

/***************************************************************************************************
 * loop()
 * Continuously checks time, handles button presses, and checks temperature.
 **************************************************************************************************/
void loop()
{
  if (!mqttClient.connected())
  {
    connectToBroker();
  }
  mqttClient.loop();
  update_time_with_check_alarm();

  sample_ldr();
  update_servo_angle();

  if (digitalRead(PB_OK) == LOW)
  {
    delay(200);
    go_to_menu();
  }

  if (digitalRead(PB_CANCEL) == LOW)
  {
    delay(200);
    handle_cancel_button();
  }

  check_temp();
  publish_light_average();
}

/***************************************************************************************************
 * print_line()
 * Quick utility to print a line on the OLED.
 **************************************************************************************************/
void print_line(String text, int column, int row, int text_size)
{
  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

/***************************************************************************************************
 * update_time()
 * Reads current local time from ESP32 system (NTP-synced) and updates global variables.
 **************************************************************************************************/
void update_time()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  hours = timeinfo.tm_hour;
  minutes = timeinfo.tm_min;
  seconds = timeinfo.tm_sec;
  days = timeinfo.tm_mday;
  dayOfWeek = DAYS_OF_WEEK[timeinfo.tm_wday];
}

/***************************************************************************************************
 * display_time()
 * Displays the current time, day, and alarm status on the OLED.
 **************************************************************************************************/
void display_time()
{
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(dayOfWeek);
  display.print(", ");
  display.print(days);

  display.setTextSize(3);
  display.setCursor(10, 16);
  char timeStr[10];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hours, minutes);
  display.print(timeStr);

  display.setTextSize(1);
  display.setCursor(110, 35);
  snprintf(timeStr, sizeof(timeStr), "%02d", seconds);
  display.print(timeStr);

  display.fillRect(0, 56, display.width(), 8, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 57);
  display.print(alarm_enabled ? "ALARM ACTIVE" : "ALARM OFF");

  display.display();
}

/***************************************************************************************************
 * update_time_with_check_alarm()
 * Updates time, displays it if on home screen, and checks alarms.
 **************************************************************************************************/
void update_time_with_check_alarm()
{
  update_time();

  if (currentState == HOME_SCREEN)
  {
    display_time();
  }

  if (alarm_enabled)
  {
    for (int i = 0; i < N_ALARMS; i++)
    {
      if (!alarm_triggered[i] &&
          alarm_hours[i] == hours &&
          alarm_minutes[i] == minutes)
      {
        ring_alarm();
        alarm_triggered[i] = true;
      }
      if (alarm_triggered[i] && temp_alarm_hour == hours && temp_alarm_minute == minutes)
      {
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}

/***************************************************************************************************
 * set_time_zone()
 * Allows user to set hour and minute offsets for time zone in 5-minute increments.
 **************************************************************************************************/
void set_time_zone()
{
  int temp_offset_hour = offset_hours;
  int temp_offset_min = offset_mins;
  bool canceled = false;

  while (true)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Set Time Zone (Hour)");
    display.setTextSize(2);
    display.setCursor(20, 20);
    display.print(temp_offset_hour);
    display.print(" hrs");
    display.display();

    int pressed = wait_for_menu_button();
    if (pressed == PB_UP)
    {
      temp_offset_hour = (temp_offset_hour + 1 > 14) ? -12 : temp_offset_hour + 1;
    }
    else if (pressed == PB_DOWN)
    {
      temp_offset_hour = (temp_offset_hour - 1 < -12) ? 14 : temp_offset_hour - 1;
    }
    else if (pressed == PB_OK)
    {
      break;
    }
    else if (pressed == PB_CANCEL)
    {
      canceled = true;
      break;
    }
  }

  if (!canceled)
  {
    while (true)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Set Time Zone (Mins)");

      display.setTextSize(2);
      display.setCursor(10, 20);
      display.print(temp_offset_min);
      display.print(" min");
      display.display();

      int pressed = wait_for_menu_button();
      if (pressed == PB_UP)
      {
        temp_offset_min = (temp_offset_min + 5) % 60;
      }
      else if (pressed == PB_DOWN)
      {
        temp_offset_min = (temp_offset_min + 60 - 5) % 60;
      }
      else if (pressed == PB_OK)
      {
        offset_hours = temp_offset_hour;
        offset_mins = temp_offset_min;

        if (offset_hours < 0)
        {
          UTC_OFFSET = (offset_hours * 3600) - (offset_mins * 60);
        }
        else
        {
          UTC_OFFSET = (offset_hours * 3600) + (offset_mins * 60);
        }
        configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
        break;
      }
      else if (pressed == PB_CANCEL)
      {
        canceled = true;
        break;
      }
    }
  }

  if (!canceled)
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.print("TZ Updated");
    display.display();
    delay(1000);
  }

  reset_to_home_screen();
}

/***************************************************************************************************
 * set_alarm()
 * Allows the user to set one of the three alarms (hour + minute).
 **************************************************************************************************/
void set_alarm(int alarmIndex)
{
  alarm_enabled = true;

  int temp_hour = alarm_hours[alarmIndex];
  int temp_minute = alarm_minutes[alarmIndex];
  bool canceled = false;
  bool hourSet = false;
  bool minuteSet = false;

  while (true)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Set Alarm ");
    display.print(alarmIndex + 1);

    display.setTextSize(2);
    display.setCursor(20, 20);
    char alarmStr[10];
    snprintf(alarmStr, sizeof(alarmStr), "%02d:%02d", temp_hour, temp_minute);
    display.print(alarmStr);
    display.display();

    int pressed = wait_for_menu_button();
    if (pressed == PB_UP)
    {
      temp_hour = (temp_hour + 1) % 24;
    }
    else if (pressed == PB_DOWN)
    {
      temp_hour = (temp_hour - 1 + 24) % 24;
    }
    else if (pressed == PB_OK)
    {
      alarm_hours[alarmIndex] = temp_hour;
      hourSet = true;
      break;
    }
    else if (pressed == PB_CANCEL)
    {
      canceled = true;
      break;
    }
  }

  if (!canceled)
  {
    while (true)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Set Alarm Mins");

      display.setTextSize(2);
      display.setCursor(20, 20);
      char alarmStr[10];
      snprintf(alarmStr, sizeof(alarmStr), "%02d:%02d", temp_hour, temp_minute);
      display.print(alarmStr);
      display.display();

      int pressed = wait_for_menu_button();
      if (pressed == PB_UP)
      {
        temp_minute = (temp_minute + 1) % 60;
      }
      else if (pressed == PB_DOWN)
      {
        temp_minute = (temp_minute - 1 + 60) % 60;
      }
      else if (pressed == PB_OK)
      {
        alarm_minutes[alarmIndex] = temp_minute;
        minuteSet = true;
        break;
      }
      else if (pressed == PB_CANCEL)
      {
        canceled = true;
        break;
      }
    }
  }

  if (!canceled && hourSet && minuteSet)
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.print("Alarm ");
    display.print(alarmIndex + 1);
    display.setCursor(10, 40);
    display.print("Set!");
    display.display();
    delay(1000);
  }

  reset_to_home_screen();
}

/***************************************************************************************************
 * ring_alarm()
 * Rings the buzzer, lights LED_1, and waits until PB_CANCEL is pressed or runs through a tone cycle.
 **************************************************************************************************/
void ring_alarm()
{
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.print("MEDICINE");
  display.setCursor(20, 40);
  display.print("TIME!");
  display.display();
  delay(100);

  digitalWrite(LED_1, HIGH);

  bool break_happened = false;
  while (!break_happened && digitalRead(PB_CANCEL) == HIGH && digitalRead(PB_OK) == HIGH)
  {
    for (int i = 0; i < N_NOTES; i++)
    {
      // Check for Cancel button press
      if (digitalRead(PB_CANCEL) == LOW)
      {
        delay(200);
        break_happened = true;
        display.clearDisplay();
        alarm_enabled = false;
        print_line("Alarm", 10, 20, 2);
        print_line("OFF", 10, 50, 2);
        noTone(BUZZER);
        digitalWrite(LED_1, LOW);
        delay(1000);
        break;
      }

      // Check for OK button press
      if (digitalRead(PB_OK) == LOW)
      {
        delay(200);
        break_happened = true;
        temp_alarm_hour = hours;
        temp_alarm_minute = minutes + 5;
        alarm_enabled = true;
        display.clearDisplay();
        print_line("Alarm", 10, 20, 2);
        print_line("Snoozed", 10, 50, 2);
        noTone(BUZZER);
        digitalWrite(LED_1, LOW);
        delay(1000);
        break;
      }

      // Play tone with a slight delay
      tone(BUZZER, MUSICAL_NOTES[i], 200);
      delay(220);
      noTone(BUZZER);
      delay(20);
    }
  }

  digitalWrite(LED_1, LOW);
  reset_to_home_screen();
}

/***************************************************************************************************
 * go_to_menu()
 * Handles the OK button press logic, switching states as needed.
 **************************************************************************************************/
void go_to_menu()
{
  switch (currentState)
  {
  case HOME_SCREEN:
    currentState = MAIN_MENU;
    display_menu();
    break;

  case MAIN_MENU:
    switch (currentMenuIndex)
    {
    case 0: // Set Time Zone
      currentState = TIME_ZONE_SETTING;
      set_time_zone();
      break;
    case 1:
    case 2:
    case 3: // Set Alarms
      currentState = ALARM_SETTING;
      set_alarm(currentMenuIndex - 1);
      break;
    case 4: // Disable Alarms
      disable_all_alarms();
      break;
    }
    break;
  }
}

/***************************************************************************************************
 * handle_cancel_button()
 * Returns to home screen if cancel is pressed.
 **************************************************************************************************/
void handle_cancel_button()
{
  reset_to_home_screen();
}

/***************************************************************************************************
 * reset_to_home_screen()
 * Resets the state to HOME_SCREEN and shows the time.
 **************************************************************************************************/
void reset_to_home_screen()
{
  currentState = HOME_SCREEN;
  currentMenuIndex = 0;
  menuScrollOffset = 0;
  display_time();
}

/***************************************************************************************************
 * wait_for_menu_button()
 * Waits for any button press related to the menu (UP, DOWN, OK, CANCEL).
 **************************************************************************************************/
int wait_for_menu_button()
{
  while (true)
  {
    if (digitalRead(PB_UP) == LOW)
    {
      delay(200);
      return PB_UP;
    }
    else if (digitalRead(PB_DOWN) == LOW)
    {
      delay(200);
      return PB_DOWN;
    }
    else if (digitalRead(PB_OK) == LOW)
    {
      delay(200);
      return PB_OK;
    }
    else if (digitalRead(PB_CANCEL) == LOW)
    {
      delay(200);
      return PB_CANCEL;
    }
  }
}

/***************************************************************************************************
 * display_menu()
 * Shows the main menu and handles navigation (up/down).
 **************************************************************************************************/
void display_menu()
{
  int totalMenuItems = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
  int visibleItems = min(MAX_VISIBLE_MENU_ITEMS, totalMenuItems);

  display.clearDisplay();
  display.setTextSize(1);

  for (int i = 0; i < visibleItems; i++)
  {
    int itemIndex = i + menuScrollOffset;
    if (itemIndex >= totalMenuItems)
      break;

    if (itemIndex == currentMenuIndex)
    {
      display.fillRect(0, i * 20, SCREEN_WIDTH, 20, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    else
    {
      display.setTextColor(SSD1306_WHITE);
    }

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(MENU_ITEMS[itemIndex], 0, 0, &x1, &y1, &w, &h);
    int16_t x = (SCREEN_WIDTH - w) / 2;

    display.setCursor(x, i * 20 + 10);
    display.println(MENU_ITEMS[itemIndex]);
    display.setTextColor(SSD1306_WHITE);
  }

  display.display();

  int pressed = wait_for_menu_button();
  if (pressed == PB_UP)
  {
    if (currentMenuIndex == 0)
      currentMenuIndex = totalMenuItems - 1;
    else
      currentMenuIndex--;

    if (currentMenuIndex < menuScrollOffset)
    {
      menuScrollOffset = currentMenuIndex;
    }
    else if (currentMenuIndex >= menuScrollOffset + visibleItems)
    {
      menuScrollOffset = currentMenuIndex - visibleItems + 1;
    }
    display_menu();
  }
  else if (pressed == PB_DOWN)
  {
    if (currentMenuIndex == totalMenuItems - 1)
      currentMenuIndex = 0;
    else
      currentMenuIndex++;

    if (currentMenuIndex < menuScrollOffset)
    {
      menuScrollOffset = currentMenuIndex;
    }
    else if (currentMenuIndex >= menuScrollOffset + visibleItems)
    {
      menuScrollOffset = currentMenuIndex - visibleItems + 1;
    }
    display_menu();
  }
  else if (pressed == PB_OK)
  {
    go_to_menu();
  }
  else if (pressed == PB_CANCEL)
  {
    reset_to_home_screen();
  }
}

/***************************************************************************************************
 * disable_all_alarms()
 * Disables all alarms and shows a brief message.
 **************************************************************************************************/
void disable_all_alarms()
{
  alarm_enabled = false;
  for (int i = 0; i < N_ALARMS; i++)
  {
    alarm_triggered[i] = false;
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.print("Alarms");
  display.setCursor(20, 40);
  display.print("Disabled");
  display.display();

  delay(1000);
  reset_to_home_screen();
}

/***************************************************************************************************
 * check_temp()
 * Reads temperature/humidity and displays a big ALERT if out of the specified range.
 **************************************************************************************************/
void check_temp()
{
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  float temperature = data.temperature;
  float humidity = data.humidity;
  String(data.temperature, 2).toCharArray(tempAr, 6);

  const float MIN_TEMP = 26.0;
  const float MAX_TEMP = 32.0;
  const float MIN_HUM = 60.0;
  const float MAX_HUM = 80.0;

  bool tempLow = temperature < MIN_TEMP;
  bool tempHigh = temperature > MAX_TEMP;
  bool humLow = humidity < MIN_HUM;
  bool humHigh = humidity > MAX_HUM;

  if (tempLow || tempHigh || humLow || humHigh)
  {
    display.clearDisplay();
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);

    int y = 0;

    display.setCursor(0, y);
    display.println("ALERT!");
    y += 35;

    display.setTextSize(1);

    if (tempLow)
    {
      display.setCursor(0, y);
      display.println("Temp is TOO LOW!");
      y += 12;
    }
    else if (tempHigh)
    {
      display.setCursor(0, y);
      display.println("Temp is TOO HIGH!");
      y += 12;
    }

    if (humLow)
    {
      display.setCursor(0, y);
      display.println("Humidity is LOW!");
      y += 12;
    }
    else if (humHigh)
    {
      display.setCursor(0, y);
      display.println("Humidity is HIGH!");
      y += 12;
    }

    display.display();

    for (int i = 0; i < 4; i++)
    {
      digitalWrite(LED_2, HIGH);
      delay(200);
      digitalWrite(LED_2, LOW);
      delay(200);
    }

    delay(1000);

    if (currentState == HOME_SCREEN)
    {
      display_time();
    }
  }
}

/***************************************************************************************************
 * void update_sampling_parameters(int new_ts, int new_tu)
 * change sampling paramiters according to chnged tu and ts.
 **************************************************************************************************/

void update_sampling_parameters(int new_ts, int new_tu)
{
  ts = new_ts;
  tu = new_tu;

  ldr_sample_count = tu / ts;
  if (ldr_sample_count > MAX_SAMPLES)
  {
    ldr_sample_count = MAX_SAMPLES; // prevent overflow
  }

  ldr_index = 0; // reset buffer position
}

/***************************************************************************************************
 * float read_ldr_normalized()
 * Reads the LDR value and normalizes it to a range of 0-1.
 **************************************************************************************************/
float read_ldr_normalized()
{
  int raw = analogRead(LDR_PIN);
  return raw / 4095.0; // Normalize to 0-1
}
/***************************************************************************************************
 * void sample_ldr()
 * Samples the LDR value at regular intervals and stores it in a circular buffer.
 **************************************************************************************************/
void sample_ldr()
{
  if (millis() - lastLdrSample >= ts * 1000)
  {
    ldr_readings[ldr_index] = read_ldr_normalized();
    // Serial.print("LDR[");
    // Serial.print(ldr_index);
    // Serial.print("] = ");
    // Serial.println(ldr_readings[ldr_index], 4);

    ldr_index = (ldr_index + 1) % ldr_sample_count;

    if (valid_sample_count < ldr_sample_count)
    {
      valid_sample_count++;
    }

    lastLdrSample = millis();
  }
}
/***************************************************************************************************
 * float calculate_average_ldr()
 * Calculates the average of the LDR readings stored in the circular buffer.loop
 **************************************************************************************************/
float calculate_average_ldr()
{
  if (valid_sample_count == 0)
    return 0;

  float sum = 0;
  for (int i = 0; i < valid_sample_count; i++)
  {
    sum += ldr_readings[i];
  }
  return sum / valid_sample_count;
}

/***************************************************************************************************
 * void update_servo_angle()
 * Calculates the servo angle based on LDR readings and temperature, then updates the servo position.
 **************************************************************************************************/
void update_servo_angle()
{
  float I = calculate_average_ldr(); // 0 to 1
  float T = dhtSensor.getTempAndHumidity().temperature;

  float ratio = log((float)ts / tu);
  float theta = theta_offset + (180 - theta_offset) * I * gammma * ratio * (T / Tmed); // corrected variable name

  theta = constrain(theta, 0, 180); // safety
  shade_servo.write((int)theta);
}

/***************************************************************************************************
 * void recieveCallback()
 * Callback function for MQTT messages.
 **************************************************************************************************/
void recieveCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char payloadCharAr[length + 1]; // +1 for null-terminator
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
    payloadCharAr[i] = (char)payload[i];
  }
  payloadCharAr[length] = '\0'; // Null-terminate the string

  Serial.println();

  if (strcmp(topic, "ENTC-ADMIN-MAIN-ON-OFF") == 0)
  {
    if (payloadCharAr[0] == '1')
    {
      Serial.println("Turning ON");
      tone(BUZZER, 1000, 200);
    }
    else if (payloadCharAr[0] == '0')
    {
      Serial.println("Turning OFF");
      noTone(BUZZER);
    }
  }
  else if (strcmp(topic, "ENTC-ADMIN-LIGHT-Tu") == 0)
  {
    int new_tu = atoi(payloadCharAr);
    update_sampling_parameters(ts, new_tu);
    Serial.print("Updated tu = ");
    Serial.println(new_tu);
  }
  else if (strcmp(topic, "ENTC-ADMIN-LIGHT-Ts") == 0)
  {
    int new_ts = atoi(payloadCharAr);
    update_sampling_parameters(new_ts, tu);
    Serial.print("Updated ts = ");
    Serial.println(new_ts);
  }
  else if (strcmp(topic, "medibox/theta_offset") == 0)
  {
    theta_offset = atof(payloadCharAr);
    Serial.print("Updated theta_offset: ");
    Serial.println(theta_offset);
  }
  else if (strcmp(topic, "medibox/gamma") == 0)
  {
    gammma = atof(payloadCharAr);
    Serial.print("Updated gamma: ");
    Serial.println(gammma);
  }
  else if (strcmp(topic, "medibox/tmed") == 0)
  {
    Tmed = atof(payloadCharAr);
    Serial.print("Updated Tmed: ");
    Serial.println(Tmed);
  }
}

/***************************************************************************************************
 * void setupMqtt()
 * Sets up the MQTT client with the server and callback.
 **************************************************************************************************/

void setupMqtt()
{
  mqttClient.setServer("test.mosquitto.org", 1883);
  mqttClient.setCallback(recieveCallback);
}

/***************************************************************************************************
 * void connectToBroker()
 * Connects to the MQTT broker and subscribes to topics.
 **************************************************************************************************/
void connectToBroker()
{
  while (!mqttClient.connected())
  {
    Serial.println("Attempting MQTT connetion");
    if (mqttClient.connect("ESP32-75645365"))
    {
      Serial.println("connected");
      mqttClient.subscribe("ENTC-ADMIN-MAIN-ON-OFF");
      mqttClient.subscribe("ENTC-ADMIN-LIGHT-Tu");
      mqttClient.subscribe("ENTC-ADMIN-LIGHT-Ts");
      mqttClient.subscribe("medibox/theta_offset");
      mqttClient.subscribe("medibox/gamma");
      mqttClient.subscribe("medibox/tmed");
    }
    else
    {
      Serial.print("failed");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

/***************************************************************************************************
 * void publish_light_average()
 * Publishes the average LDR reading to the MQTT broker.
 **************************************************************************************************/
void publish_light_average()
{
  float avg = calculate_average_ldr();
  char buffer[10];
  dtostrf(avg, 4, 2, buffer);
  mqttClient.publish("ENTC-ADMIN-LIGHT", buffer, true); // Retain = true
  Serial.println(buffer);
  lastLdrUpload = millis();
}
