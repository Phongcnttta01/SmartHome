#include <DHTesp.h>
#include <EspMQTTClient.h>
#include <MQUnifiedsensor.h>

#define Board                   ("ESP8266")
#define Pin                     A0
#define PreheatPin              D7
#define RelayPin                D0   // Quạt
#define Type                    ("MQ-9")
#define Voltage_Resolution      5
#define ADC_Bit_Resolution      10
#define RatioMQ9CleanAir        9.6
#define GAS_THRESHOLD_PPM       200   

#define LED_PIN                 D1
#define DHT_PIN                 D6
#define AIR_SENSOR_PIN          D5
const int lightSensorPin = A0;
const int ledLightPin = D2;

#ifndef ESP8266
  #error This code is for ESP8266 only!
#endif

DHTesp dht;
EspMQTTClient client(
  "Hansa", "12345678", "192.168.43.147",
  "MQTTUsername", "MQTTPassword",
  "TestClient", 1883
);

float lastTemperature = -1000;
float lastHumidity = -1000;
unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 2000;

MQUnifiedsensor MQ9(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);
unsigned long lastGasCheck = 0;
const unsigned long gasCheckInterval = 5000;

void setup() {
  Serial.begin(115200);
  dht.setup(DHT_PIN, DHTesp::DHT11);
  pinMode(LED_PIN, OUTPUT);
  pinMode(ledLightPin, OUTPUT);
  pinMode(PreheatPin, OUTPUT);
  pinMode(RelayPin, OUTPUT);
  digitalWrite(RelayPin, LOW);

  client.enableDebuggingMessages();
  client.enableLastWillMessage("TestClient/lastwill", "I am going offline");

  // MQ9 init
  MQ9.setRegressionMethod(1);
  MQ9.setA(1000.5); MQ9.setB(-2.186);
  MQ9.init();

  Serial.println("Preheating 10s...");
  digitalWrite(PreheatPin, HIGH);
  delay(10000); // Chỉ preheat ban đầu

  float calcR0 = 0;
  for (int i = 0; i < 5; i++) {
    MQ9.update();
    calcR0 += MQ9.calibrate(RatioMQ9CleanAir);
    delay(500);
  }
  MQ9.setR0(calcR0 / 5);
  MQ9.serialDebug(true);
}

void onConnectionEstablished() {
  client.subscribe("Led", [](const String &payload) {
    digitalWrite(LED_PIN, (payload == "1" || payload == "on" || payload == "true") ? HIGH : LOW);
  });

  client.subscribe("Led2", [](const String &payload) {
    digitalWrite(LED_PIN, (payload == "1" || payload == "on" || payload == "true") ? HIGH : LOW);
  });

  client.subscribe("AirQuality", [](const String &payload) {});
  client.subscribe("LightSensor", [](const String &payload) {});
  client.subscribe("GasSensor", [](const String &payload) {});
  client.subscribe("Light", [](const String &payload) {});
  client.subscribe("Temperature", [](const String &payload) {});
  client.subscribe("Humidity", [](const String &payload) {});
}

void loop() {
  client.loop();
  unsigned long now = millis();

  // --- DHT11 ---
  if (now - lastPublishTime >= publishInterval) {
    lastPublishTime = now;
    float temp = dht.getTemperature();
    float hum = dht.getHumidity();

    if (!isnan(temp) && temp != lastTemperature) {
      client.publish("Temperature", String(temp, 1));
      lastTemperature = temp;
      Serial.println("Temp: " + String(temp, 1));
    }
    if (!isnan(hum) && hum != lastHumidity) {
      client.publish("Humidity", String(hum, 1));
      lastHumidity = hum;
      Serial.println("Humidity: " + String(hum, 1));
    }

    // --- Air Sensor ---
    int airValue = analogRead(AIR_SENSOR_PIN);
    Serial.print("Air Quality: "); Serial.println(airValue);
    if (airValue > 700) {
      client.publish("AirQuality", "→ Cảnh báo: Không khí ô nhiễm!");
    } else if (airValue > 400) {
      client.publish("AirQuality", "→ Không khí bắt đầu bị ảnh hưởng.");
    } else {
      client.publish("AirQuality", "→ Không khí trong lành.");
    }

    // --- Light Sensor ---
    int lightValue = analogRead(lightSensorPin);
    Serial.print("Light: "); Serial.println(lightValue);
    if (lightValue < 800) {
      digitalWrite(ledLightPin, HIGH);
      client.publish("LightSensor", "→ Trời tối, bật đèn!");
    } else {
      digitalWrite(ledLightPin, LOW);
      client.publish("LightSensor", "→ Trời sáng, tắt đèn!");
    }
  }

  // --- MQ-9 Gas Sensor ---
  if (now - lastGasCheck >= gasCheckInterval) {
    lastGasCheck = now;

    digitalWrite(PreheatPin, HIGH);
    delay(200); // nhẹ hơn delay(2000)

    MQ9.update();
    float voltage = MQ9.getVoltage();
    float RS = (Voltage_Resolution * 10.0 / voltage) - 10.0;
    float ratio = RS / MQ9.getR0();
    float gas_ppm = MQ9.getA() * pow(ratio, MQ9.getB());

    Serial.print("Gas PPM: "); Serial.println(gas_ppm);

    if (gas_ppm > GAS_THRESHOLD_PPM) {
      digitalWrite(RelayPin, HIGH);
      client.publish("GasSensor", "→ ⚠️  GAS ALERT! Quạt đã bật!");
    } else {
      digitalWrite(RelayPin, LOW);
      client.publish("GasSensor", "→ GAS OK! Quạt đã tắt!");
    }

    digitalWrite(PreheatPin, LOW);
  }
}
