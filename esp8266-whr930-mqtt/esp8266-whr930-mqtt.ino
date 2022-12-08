#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#include <secrets.h>

// Replace with your network credentials
const char* wifi_ssid = WIFI_SSID;
const char* wifi_password = WIFI_PASS;
const char* wifi_hostname = HOSTNAME;
const char* ota_password = OTA_PASS;

const char* mqtt_server = MQTT_IP;
const int mqtt_port = 1883;
const char* mqtt_username = MQTT_USER;
const char* mqtt_password = MQTT_PASS;

const char* mqtt_topic_base = "house/ventilation/whr950";
const char* mqtt_logtopic = "house/ventilation/whr950/log";

const char* mqtt_set_ventilation_topic = "house/ventilation/whr950/setventilation";
const char* mqtt_set_temperature_topic = "house/ventilation/whr950/settemperature";
const char* mqtt_get_update_topic = "house/ventilation/whr950/update";

//useful for debugging, outputs info to a separate mqtt topic
const bool outputMqttLog = true;

// instead of passing array pointers between functions we just define this in the global scope
#define MAXDATASIZE 256
char data[MAXDATASIZE];
int data_length = 0;

// log message to sprintf to
char log_msg[256];

// mqtt topic to sprintf and then publish to
char mqtt_topic[256];

// mqtt
WiFiClient mqtt_wifi_client;
PubSubClient mqtt_client(mqtt_wifi_client);

bool send_command(byte* command, int length) {
  log_message("sending command");
  //sprintf(log_msg, "Data length   : %d", length); log_message(log_msg);
  //sprintf(log_msg, "Ack           : %02X %02X", command[0], command[1]); log_message(log_msg);
  //sprintf(log_msg, "Start         : %02X %02X", command[2], command[3]); log_message(log_msg);
  //sprintf(log_msg, "Command       : %02X %02X", command[4], command[5]); log_message(log_msg);
  //sprintf(log_msg, "Nr data bytes : %02X (integer %d)", command[6], command[6]); log_message(log_msg);

  int bytesSent = Serial.write(command, length);

  // read the serial and return if succesfull
  return readSerial();
}

// Callback function that is called when a message has been pushed to one of your topics.
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  for (int i = 0; i < length; i++) {
    msg[i] = (char)payload[i];
  }
  msg[length] = '\0';

  if (strcmp(topic, mqtt_set_ventilation_topic) == 0) {
    String ventilation_string(msg);
    int ventilation = ventilation_string.toInt() + 1;
    int checksum = (0 + 153 + 1 + ventilation + 173) % 256;

    sprintf(log_msg, "set ventilation to %d", ventilation - 1);
    log_message(log_msg);
    byte command[] = { 0x07, 0xF0, 0x00, 0x99, 0x01, ventilation, checksum, 0x07, 0x0F };
    send_command(command, sizeof(command));
  }
  if (strcmp(topic, mqtt_set_temperature_topic) == 0) {
    String temperature_string(msg);
    int temperature = temperature_string.toInt();

    temperature = (temperature + 20) * 2;
    int checksum = (0 + 211 + 1 + temperature + 173) % 256;

    sprintf(log_msg, "set temperature to %d", temperature);
    log_message(log_msg);
    byte command[] = { 0x07, 0xF0, 0x00, 0xD3, 0x01, temperature, checksum, 0x07, 0x0F };
    send_command(command, sizeof(command));
  }

  if (strcmp(topic, mqtt_get_update_topic) == 0) {
    log_message("Updating..");

    get_filter_status();
    get_temperatures();
    get_ventilation_status();
    get_fan_status();
    get_valve_status();
    get_bypass_control();
  }
}

void get_filter_status() {
  byte command[] = { 0x07, 0xF0, 0x00, 0xD9, 0x00, 0x86, 0x07, 0x0F };
  if (send_command(command, sizeof(command))) {
    int filter_state = (int)(data[8]);

    char* filter_state_string;
    if (filter_state == 0) {
      filter_state_string = "Ok";
    } else if (filter_state == 1) {
      filter_state_string = "Full";
    } else {
      filter_state_string = "Unknown";
    }

    sprintf(log_msg, "received filter state : %d (%s)", filter_state, filter_state_string);
    log_message(log_msg);

    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "filter_state");
    mqtt_client.publish(mqtt_topic, filter_state_string);
  }
}

void get_temperatures() {
  byte command[] = { 0x07, 0xF0, 0x00, 0xD1, 0x00, 0x7E, 0x07, 0x0F };
  if (send_command(command, sizeof(command))) {
    float ComfortTemp = (float)data[0] / 2.0 - 20;
    float OutsideAirTemp = (float)data[1] / 2.0 - 20;
    float SupplyAirTemp = (float)data[2] / 2.0 - 20;
    float ReturnAirTemp = (float)data[3] / 2.0 - 20;
    float ExhaustAirTemp = (float)data[4] / 2.0 - 20;

    sprintf(log_msg, "received temperatures (comfort, outside, supply, return, exhaust): %.2f, %.2f, %.2f, %.2f, %.2f", ComfortTemp, OutsideAirTemp, SupplyAirTemp, ReturnAirTemp, ExhaustAirTemp);
    log_message(log_msg);

    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "comfort_temp");
    mqtt_client.publish(mqtt_topic, String(ComfortTemp).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "outside_air_temp");
    mqtt_client.publish(mqtt_topic, String(OutsideAirTemp).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "supply_air_temp");
    mqtt_client.publish(mqtt_topic, String(SupplyAirTemp).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "return_air_temp");
    mqtt_client.publish(mqtt_topic, String(ReturnAirTemp).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "exhaust_air_temp");
    mqtt_client.publish(mqtt_topic, String(ExhaustAirTemp).c_str());
  }
}

void get_ventilation_status() {
  byte command[] = { 0x07, 0xF0, 0x00, 0xCD, 0x00, 0x7A, 0x07, 0x0F };
  if (send_command(command, sizeof(command))) {
    float ReturnAirLevel = (float)data[6];
    float SupplyAirLevel = (float)data[7];
    int FanLevel = (int)data[8] - 1;
    int IntakeFanActive = (int)data[9];

    char* IntakeFanActive_string;
    if (IntakeFanActive == 1) {
      IntakeFanActive_string = "Yes";
    } else if (IntakeFanActive == 0) {
      IntakeFanActive_string = "No";
    } else {
      IntakeFanActive_string = "Unknown";
    }

    sprintf(log_msg, "received ventilation status (return air level, supply air level, fan level, intake fan active): %.2f, %.2f, %d, %d", ReturnAirLevel, SupplyAirLevel, FanLevel, IntakeFanActive);
    log_message(log_msg);

    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "return_air_level");
    mqtt_client.publish(mqtt_topic, String(ReturnAirLevel).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "supply_air_level");
    mqtt_client.publish(mqtt_topic, String(SupplyAirLevel).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "ventilation_level");
    mqtt_client.publish(mqtt_topic, String(FanLevel).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "intake_fan_active");
    mqtt_client.publish(mqtt_topic, IntakeFanActive_string);
  }
}

void get_fan_status() {
  byte command[] = { 0x07, 0xF0, 0x00, 0x0B, 0x00, 0xB8, 0x07, 0x0F };
  if (send_command(command, sizeof(command))) {
    int IntakeFanSpeed = 1875000.0f / (((int)data[2] << 8) | (int)data[3]);
    int ExhaustFanSpeed = 1875000.0f / (((int)data[4] << 8) | (int)data[5]);

    sprintf(log_msg, "received fan speeds (intake, exhaust): %.2f, %.2f", IntakeFanSpeed, ExhaustFanSpeed);
    log_message(log_msg);

    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "intake_fan_speed");
    mqtt_client.publish(mqtt_topic, String(IntakeFanSpeed).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "exhaust_fan_speed");
    mqtt_client.publish(mqtt_topic, String(ExhaustFanSpeed).c_str());
  }
}

void get_valve_status() {
  byte command[] = { 0x07, 0xF0, 0x00, 0x0D, 0x00, 0xBA, 0x07, 0x0F };
  if (send_command(command, sizeof(command))) {
    int ByPass = (int)data[0];
    int PreHeating = (int)data[1];
    int ByPassMotorCurrent = (int)data[2];
    int PreHeatingMotorCurrent = (int)data[3];

    sprintf(log_msg, "received fan status (bypass, preheating, bypassmotorcurrent, preheatingmotorcurrent): %d, %d, %d, %d", ByPass, PreHeating, ByPassMotorCurrent, PreHeatingMotorCurrent);
    log_message(log_msg);

    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "valve_bypass_percentage");
    mqtt_client.publish(mqtt_topic, String(ByPass).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "valve_preheating");
    mqtt_client.publish(mqtt_topic, String(PreHeating).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "bypass_motor_current");
    mqtt_client.publish(mqtt_topic, String(ByPassMotorCurrent).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "preheating_motor_current");
    mqtt_client.publish(mqtt_topic, String(PreHeatingMotorCurrent).c_str());
  }
}

void get_bypass_control() {
  byte command[] = { 0x07, 0xF0, 0x00, 0xDF, 0x00, 0x8C, 0x07, 0x0F };
  if (send_command(command, sizeof(command))) {
    int ByPassFactor = (int)data[2];
    int ByPassStep = (int)data[3];
    int ByPassCorrection = (int)data[4];

    char* summerModeString;
    if ((int)data[6] == 1) {
      summerModeString = "Yes";
    } else {
      summerModeString = "No";
    }
    sprintf(log_msg, "received bypass control (bypassfactor, bypass step, bypass correction, summer mode): %d, %d, %d, %s", ByPassFactor, ByPassStep, ByPassCorrection, summerModeString);
    log_message(log_msg);

    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "bypass_factor");
    mqtt_client.publish(mqtt_topic, String(ByPassFactor).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "bypass_step");
    mqtt_client.publish(mqtt_topic, String(ByPassStep).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "bypass_correction");
    mqtt_client.publish(mqtt_topic, String(ByPassCorrection).c_str());
    sprintf(mqtt_topic, "%s/%s", mqtt_topic_base, "summermode");
    mqtt_client.publish(mqtt_topic, String(summerModeString).c_str());
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Hello world!");

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  WiFi.hostname(wifi_hostname);

  pinMode(LED_BUILTIN, OUTPUT);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(wifi_hostname);

  // Set authentication
  ArduinoOTA.setPassword(ota_password);

  ArduinoOTA.onStart([]() {
  });
  ArduinoOTA.onEnd([]() {
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

  });
  ArduinoOTA.onError([](ota_error_t error) {

  });
  ArduinoOTA.begin();

  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
}

void loop() {
  // Handle OTA first.
  ArduinoOTA.handle();

  if (!mqtt_client.connected()) {
    mqtt_reconnect();
  }
  mqtt_client.loop();

  get_filter_status();
  get_temperatures();
  get_ventilation_status();
  get_fan_status();
  get_valve_status();
  get_bypass_control();

  delay(5000);
}

void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    if (mqtt_client.connect(wifi_hostname, mqtt_username, mqtt_password)) {
      mqtt_client.subscribe(mqtt_set_ventilation_topic);
      mqtt_client.subscribe(mqtt_set_temperature_topic);
      mqtt_client.subscribe(mqtt_get_update_topic);
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
void log_message(char* string) {
  if (outputMqttLog) {
    mqtt_client.publish(mqtt_logtopic, string);
  }
}

bool readSerial() {
  long previousMillis = millis();
  bool dataReceived = false;
  bool waitingForAck = true;
  int index = 0;

  digitalWrite(LED_BUILTIN, LOW);
  while ((millis() - previousMillis <= 5000) && (dataReceived == false)) {
    if (Serial.available() > 0) {
      digitalWrite(LED_BUILTIN, LOW);
      byte iByte = Serial.read();
      data[index] = iByte;

      if (waitingForAck == true) {
        if (data[index] == 0xF3 && data[index - 1] == 0x07) {
          log_message("Got an ACK!");
          waitingForAck = false;
          index = 0;
          data_length = 0;
        }
      } else {
        //sprintf(log_msg, "%02X", iByte); log_message(log_msg);

        // reset the internal counter to zero when we encounter a start of a message
        if (index > 0 && data[index] == 0xF0 && data[index - 1] == 0x07) {
          log_message("Start of msg");
          index = 0;
        }
        if (data[index] == 0x0F && data[index - 1] == 0x07) {
          dataReceived = true;
          sprintf(log_msg, "end of msg of length %d", index + 1);
          log_message(log_msg);
          data_length = index + 1;
        }
      }
      index += 1;
    }
  }
  if (dataReceived == false) log_message("Serial timed out");

  //Strip start and stop so only the pure data is in data[]
  data_length = data[3];
  for (int i = 0; i < data_length; i++) {
    data[i] = data[i + 4];
  }
  digitalWrite(LED_BUILTIN, HIGH);
  return dataReceived;
}
