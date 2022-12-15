/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "EEPROM.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/
#define SERVICE_UUID        "59d5b564-5077-4ad5-b14c-fc00d5d929c8"
#define CHARACTERISTIC_UUID_SETTINGS "a7016028-0c96-4171-874c-f730b9b952d5"
#define CHARACTERISTIC_UUID_READINGS "bc7d3919-54ca-4259-980b-f3269a5621c0"
#define LED_R_PIN (3)
#define LED_G_PIN (5)
#define LED_B_PIN (1)
#define PWM_OUT_PIN (2)

#define PWMDATA_SIZE (9)
typedef union {
  char rawBytes[PWMDATA_SIZE];
  struct {
    uint32_t frequency;
    float duty;
    byte saveToEEPROMFlag;
  };
} PWMData;

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristicSettings = NULL;
BLECharacteristic* pCharacteristicReadings = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;
PWMData pwmSettings, pwmReadings;
unsigned long lastMillis;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      BLEDevice::startAdvertising();
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();

      //      if (value.length() > 0) {
      //        Serial.println("*********");
      //        Serial.print("New value: ");
      //        for (int i = 0; i < value.length(); i++)
      //          Serial.print(value[i]);
      //
      //        Serial.println();
      //        Serial.println("*********");

      memcpy(pwmSettings.rawBytes, value.c_str(), PWMDATA_SIZE);
      memcpy(pwmReadings.rawBytes, value.c_str(), PWMDATA_SIZE);
      ledcSetup(1, pwmSettings.frequency, 10);
      ledcWrite(1, (uint32_t)(pwmSettings.duty / 100.0 * 1023.0));
      if (pwmSettings.saveToEEPROMFlag) {
        Serial.println("Writing EEPROM");
        EEPROM.writeBytes(0x00, pwmSettings.rawBytes, PWMDATA_SIZE);
        EEPROM.commit();
        pwmSettings.saveToEEPROMFlag = 0x00;
      }
      pCharacteristic->notify();
    }
};


void setup() {
  Serial.begin(115200);

  if (!EEPROM.begin(1000)) {
    Serial.println("Failed to initialise EEPROM");
    Serial.println("Restarting...");
    delay(1000);
    ESP.restart();
  }

  EEPROM.readBytes(0x00, pwmSettings.rawBytes, PWMDATA_SIZE);
  memcpy(pwmReadings.rawBytes, pwmSettings.rawBytes, PWMDATA_SIZE);

  Serial.println(pwmSettings.frequency);
  Serial.println(pwmSettings.duty);

  ledcAttachPin(LED_R_PIN, 1);
  ledcAttachPin(PWM_OUT_PIN, 1);
  ledcSetup(1, pwmSettings.frequency, 10);
  ledcWrite(1, (uint32_t)(pwmSettings.duty / 100.0 * 1023.0));

  // Create the BLE Device
  BLEDevice::init("ESP32 PWM Generator");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristicSettings = pService->createCharacteristic(
                              CHARACTERISTIC_UUID_SETTINGS,
                              BLECharacteristic::PROPERTY_READ   |
                              BLECharacteristic::PROPERTY_WRITE  |
                              BLECharacteristic::PROPERTY_NOTIFY //| BLECharacteristic::PROPERTY_INDICATE
                            );

  pCharacteristicReadings = pService->createCharacteristic(
                              CHARACTERISTIC_UUID_READINGS,
                              BLECharacteristic::PROPERTY_READ  |
                              BLECharacteristic::PROPERTY_NOTIFY
                            );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristicSettings->addDescriptor(new BLE2902());
  pCharacteristicReadings->addDescriptor(new BLE2902());

  pCharacteristicSettings->setCallbacks(new MyCharacteristicCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");

  pCharacteristicSettings->setValue((uint8_t*)pwmSettings.rawBytes, PWMDATA_SIZE);
  pCharacteristicReadings->setValue((uint8_t*)pwmReadings.rawBytes, PWMDATA_SIZE);
}

void loop() {
  // notify changed value
  if (deviceConnected) {
    if (millis() - lastMillis > 100) {
      pCharacteristicReadings->setValue((uint8_t*)pwmReadings.rawBytes, 8);
      pCharacteristicReadings->notify();
      lastMillis = millis();
    }
    //    delay(10); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    pCharacteristicSettings->notify();
    pCharacteristicReadings->notify();
    lastMillis = millis();
    oldDeviceConnected = deviceConnected;
  }
}
