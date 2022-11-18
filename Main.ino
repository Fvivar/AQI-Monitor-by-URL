//Libraries required to operate
#include "bsec.h"
#include "Adafruit_PM25AQI.h"
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h> 
#include <ArduinoJson.h>
#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

/*
 * Defs
 */
#define AWS_IOT_PUBLISH_TOPIC "aqi_monitor_url/pub" //AWS Topic to publish
String output; //Used to debug measurements in serial monitor
int InternetCheck = 0; //Status for internet check 0 means no internet, 1 means internet
int control = 0; //Counter for 'void loop', each iterion loop void runs increments its value by 1.
String filenameSD = "/Reading_"; //Name to store data in microSD
int filenameCounter = 0; //Counter to be used in name to store data in microSD

WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

// Helper voids
void checkIaqSensorStatus(void);
void errLeds(void);

// Creating objects for sensors
Bsec iaqSensor; //BME688 using advanced library
Adafruit_PM25AQI aqi = Adafruit_PM25AQI(); //PMSA003I




/*
 * WiFi connection status [Connected to AP]
 */
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected to AP successfully!");
}

/* 
 *  WiFi connection status [Prints device's IP address]
 */
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP()); //Prints assigned IP address
  Serial.println("Gateway:");
  Serial.println(WiFi.gatewayIP());  
}

/*
 * WiFi connection status [When AP disconnection detected it reconnects to AP]
 */
//void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
//  InternetCheck = 0; //Device connected to AP but internet connectivity hasn't been confirmed.
//  Serial.println("Disconnected from WiFi access point. Trying to Reconnect...");
//  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); //Reconnects to AP with credentials stored in 'secrets.h'
//  delay(7000); //Delaying next instruction so it has enough time to connect
//  if (WiFi.status() != WL_CONNECTED){
//    Serial.println("\n");
//    Serial.println("No connection achieved, next reading will be stored locally if no connection is achieved when retried");
//    continue;
//  }
//}

/*
 * Connects to WiFi & AWS's endpoint.
 * The following values are required and must be stored in 'secrets.h':
 * 1. AWS_CERT_CA
 * 2. AWS_CERT_CRT
 * 3. AWS_CERT_PRIVATE
 * 4. AWS_IOT_ENDPOINT
 * 5. THINGNAME
 */
void connectAWS(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); //Connects to AP with credentials stored in 'secrets.h'
  //Serial.println("Connecting to Wi-Fi");
  
//  while (WiFi.status() != WL_CONNECTED)
//  {
//    delay(500);
//    Serial.print(".");
//  }
  delay(7000); //Delaying next instruction so it has enough time to connect

  if(WiFi.status() == WL_CONNECTED){
    
  
  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier using default MQTT port over TLS
  client.setServer(AWS_IOT_ENDPOINT, 8883);
 
  // Create a message handler
  client.setCallback(messageHandler);
 
  Serial.print("Connecting to AWS IOT - - - -");
 
  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    Serial.print(client.state());
    delay(100);
  }
 
  if (!client.connected())
  {
    Serial.println("AWS IoT Timeout!");
    InternetCheck = 0; //Couldn't connect to endpoint; data can't be sent to AWS thus it'll be stored locally in microSD card
    return;
  }
 
  // Subscribe to a topic
  // client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

  if (client.connected())
  {
    Serial.println(" > AWS IoT Connected!");
    InternetCheck = 1; //Connection to AWS succesful; data will be sent to AWS
    
  }
  }
  
  else{
    Serial.println("\n");
    Serial.println("No connection achieved, reading will be stored locally");
    InternetCheck = 0;
    //break;
  }
}

/*
 * [CURRENTLY NOT USED]
 * Handles incoming messages from AWS.
 */
void messageHandler(char* topic, byte* payload, unsigned int length)
{
  Serial.print("incoming: ");
  Serial.println(topic);
 
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* message = doc["message"];
  Serial.println(message);
}

/* 
 *  [SETUP]
 *  Initilizes SD card
 *  Refreshes WiFi connection and initilizes WiFi events
 *  Initializes Wire for I2C over the default on-board ports
 *  Initilizes BME688 sensor using default address 0x76 for I2C
 *  Initilizes PMSA003I sensor using default address 0x12 for I2C
 */
void setup()
{
  Serial.begin(115200); //Rate for serial port

  /* ------------------------- SD handler SETUP [START] ----------------------------------*/
  if(!SD.begin()){
        Serial.println("Card Mount Failed");
        return;
    }

  uint8_t cardType = SD.cardType(); //Get SD card info

  //Alert if no SD card was found
  if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return;
    }
  /* ------------------------- SD handler SETUP [END] ----------------------------------*/

  /* ------------------------- WiFi & AWS SETUP [START] ----------------------------------*/
  WiFi.disconnect(true); //Force disconnection of WiFi AP to ensure new connection's 'fresh' condition
  WiFi.onEvent(WiFiStationConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED); //WiFi connection status [Connected to AP]
  WiFi.onEvent(WiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP); //WiFi connection status [Prints device's IP address]
  //WiFi.onEvent(WiFiStationDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED); //WiFi connection status [When AP disconnection detected it reconnects to AP]
  
  // Wait one second for sensor to boot up!
  delay(1000);
  
  connectAWS(); //Initialize connection to AWS
  /* ------------------------- WiFi & AWS SETUP [END] ----------------------------------*/

  /* ------------------------- I2C over default ports SETUP [START] ----------------------------------*/
  Wire.begin(); //Initializing default I2C pins on ESP32-WROOM board, 21 (SDA), 22 (SCL)
  /* ------------------------- I2C over default ports SETUP [START] ----------------------------------*/
  
  /* ------------------------- BME688 SETUP [START]  ----------------------------------*/
  iaqSensor.begin(BME680_I2C_ADDR_PRIMARY, Wire); //Initializing BME688 sensor with default 7-bit address 0x76. Using advanced library (BSEC).
  output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output); //Prints BSEC library info
  checkIaqSensorStatus(); //Checks for BME688 sensor status

  //Defines the values we are interested in gathering.
  bsec_virtual_sensor_t sensorList[10] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP); //Updating BME688 sensor working parameters, note the selected mode for operation 'BSEC_SAMPLE_RATE_LP'
  checkIaqSensorStatus(); //Checks for BME688 sensor status
    
  // Print the header for the BME688 values that will be officially gathered
  //output = "Temperature [°C], pressure [hPa], humidity [%], IAQ, IAQ accuracy, CO2 equivalent, breath VOC equivalent";
  //Serial.println(output);
  /* ------------------------- BME688 SETUP [END] ----------------------------------*/
  
  /* ------------------------- PMSA003I SETUP [START] ----------------------------------*/
  // connect to the sensor over I2C using default 7-bit address 0x12, this address is already contained within the library as the default address to start the I2C connection.
  //PM25_AQI_Data data;
  if (! aqi.begin_I2C()) {      
    Serial.println("Could not find PM 2.5 sensor!");
    while (1) delay(10);
  }
  Serial.println("PM25 found!");
  /* ------------------------- PMSA003I SETUP [END] ----------------------------------*/
}

/* 
 *  [LOOP]
 *  
 */
void loop(void)
{
  delay(3000); //Delays loop to get stable readings. By stable I mean full reading from both sensors, otherwise the payload only contains the PMSA003I data
  /* ------------------------- JSON SETUP [START] ----------------------------------*/
  StaticJsonDocument<200> doc; //Create a JSON document of size 200 bytes, and populate it
  /* ------------------------- JSON SETUP [END] ----------------------------------*/
  
  /* ------------------------- Adding BME688 data to JSON [START] ----------------------------------*/
  if (iaqSensor.run()) { // If new data is available
    doc["a"] = String(iaqSensor.temperature); //Compensated temperature 'BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE'
    //doc["b"] = String(iaqSensor.pressure); // Pressure 'BSEC_OUTPUT_RAW_PRESSURE,'
    doc["c"]= String(iaqSensor.humidity); //Compensated humidity 'BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY'
    //doc["d"] = String(iaqSensor.iaq); //Mobile AQI 'BSEC_OUTPUT_IAQ' the use of this measure is recommended for mobile applications as the BME688 datasheet states (Page 31)
    doc["d"] = String(iaqSensor.staticIaq); //Static AQI 'BSEC_OUTPUT_STATIC_IAQ' the use of this measure is suggested for stationary appliacations as the BME688 datasheet states (Page 31)
    doc["e"] = String(iaqSensor.iaqAccuracy); //AQI accuracy
    doc["f"] = String(iaqSensor.co2Equivalent); //Co2 equivalent 'BSEC_OUTPUT_CO2_EQUIVALENT'; refer to BME688 datasheet page 31 for further info
    //doc["g"] = String(iaqSensor.breathVocEquivalent); //Breath VOC Equivalent 'BSEC_OUTPUT_BREATH_VOC_EQUIVALENT'; refer to BME688 datasheet page 31 for further info
  } 
  else {
    checkIaqSensorStatus(); //Check sensor status
  }
  /* ------------------------- Adding BME688 data to JSON [END] ----------------------------------*/

  /* ------------------------- Adding PMSA003I data to JSON [START] ----------------------------------*/
  PM25_AQI_Data data; //CLASS INSTANCE FOR PM DATA
  if (! aqi.read(&data)) {
    Serial.println("Could not read from AQI");
    delay(500);  // try again in a bit!
    return;
  }

  //DATA COLLECTION FOR PMs
  doc["h"] = String(data.pm10_env); //Concentration Units (PM 1.0)
  doc["i"] = String(data.pm25_env); //Concentration Units (PM 2.5)
  doc["j"] = String(data.pm100_env); //Concentration Units (PM 10)
  //  doc["k"] = String(data.particles_03um);
  //  doc["l"] = String(data.particles_05um); 
  //  doc["m"] = String(data.particles_10um);
  //  doc["n"] = String(data.particles_25um);
  //  doc["o"] = String(data.particles_50um);
  //  doc["p"] = String(data.particles_100um);
  /* ------------------------- Adding PMSA003I data to JSON [END] ----------------------------------*/
  
  //ADDING TIMESTAMP
  unsigned long time_trigger = millis();
  //  doc["t"] = String(time_trigger);

  /* ------------------------- JSON & BUFFER [START] ----------------------------------*/
  char jsonBuffer[512]; //buffer to store JSON data
  serializeJson(doc, jsonBuffer); //Transfering JSON to buffer
  Serial.print("\n"); 
  serializeJson(doc, Serial); //print to Serial monitor
  control = control +1; //Increase counter by 1
  Serial.print(control);
  /* ------------------------- JSON & BUFFER [START] ----------------------------------*/

  
  /* JSON will be published to AWS when loop iterations reach 159 at the current delay (1900).
   * The 159 iterations will happen at around 5 minutes and 23 seconds
   * At this point the AQI accuracy value from the BME688 will be 1 instead of 0 indicating a more precise reading
   * As the BME688 datasheet states, the value for AQI accuracy ranges from 0 to 3, thus it is expected that it reaches a higher value when at least 36 hours of runtime have been reached
   */
  if (control == 100){
    control = 0; //Reseting control 
    //filenameCounter = 4;
     
    
    connectAWS(); //Reconnection to AWS
    Serial.print("\n");
    // default if parameter InternetCheck == 1
    //Serial.print("Client State");
    //Serial.print(client.state());
    Serial.print("\n");

    /* MQTT Broker Check prior to publish data
     * >>>> client.state = 0: MQTT_CONNECTED. Refer to the 'PubSubClient' Library to check the other possible states at https://pubsubclient.knolleary.net/api#connect
     * If (InternetCheck == 1), JSON will be published to AWS; also, if files have been written to the microSD, they will be read and sent to AWS after the main JSON is sent.
     * If (InternetCheck != 0), JSON will be stored locally in the microSD card as "Reading_x.txt".
     */
    if (InternetCheck == 1) {  
      //delay(2000); //delay to be able to sample a full measure, otherwise only the tail of the JSON is taken.

      //Sending to AWS
      Serial.println("Stable data (calibrated!)");
      serializeJson(doc, Serial); //print JSON (to be published) to Serial monitor
      client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer); //publishing JSON to AWS
      Serial.print("\n");
      Serial.print("Published data!");
      
      //delay(1500);
      
      /*
       * [DATA STORED IN MICROSD]
       * Checking if there's data stored locally (microSD)
       * A. If so, a for loop is activated to run through every .txt file.
       *    After a .txt file is opened, a new buffer is created and will be populated char by char from the file being read.
       *    Once the buffer is populated , the buffer is published to AWS.
       *    Then the file is closed and removed from the SD card
       * 
       * B. If not, normal flow of loop will continue
       */
      if (filenameCounter != 0){
        Serial.print("\n");
        Serial.print("Reading files from microSD...");
        for (int i = 0; i <= filenameCounter; i++) {
          /* -------------------- SD name handler to read from .txt file [START] ---------------------- */
          filenameSD = "/Reading_"; //Resets name
          filenameSD = filenameSD + String(i)+".txt"; //File name for .txt
          /* -------------------- SD name handler to read from .txt file [END] ---------------------- */
         
          char jsonBufferA[512]; //buffer used to store chars from file read. Further published to AWS
          int j = 0; //Aux counter used to select buffer positions while storing chars from file
          File file = SD.open(filenameSD); //Opening file stored in microSD
          /* -------------------- Populating the new buffer with .txt file [START] ---------------------- */
          while (file.available()) {
            jsonBufferA[j] = (char)file.read(); //Stores data in buffer char by char. Files have the following structure {"a":"11.11","b":"11.11","c":"11.11",...}
            j = j + 1;
          }
          /* -------------------- Populating the new buffer with .txt file [END] ---------------------- */
          
          Serial.print("\n");
          Serial.print("printing newBuffer");
          Serial.print("\n");
          Serial.print(jsonBufferA);
          client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBufferA); //Publishing new buffer to AWS
          file.close(); //Closing file 
          SD.remove(filenameSD); //Removing file from SD
          //ENDING READ LINES
          Serial.print("\n");
          Serial.print("File read & published succesfully!");
          Serial.print("\n");
          delay(10);
        }
        filenameCounter = 0; //Reseting counter. Once all readings have been sent
        Serial.print("Readings complete");
      } 
    }

    /*
     * [STORE READINGS LOCALLY (microSD)]
     * Due to lack of connectivity with broker, readings from sensors will be stored in microSD card
     * Once the file name has been stablished (Reading_x.txt), the file will be created and the JSON will be transfered to it.
     * Note the '/' character at the begining of the filename, this is included to properly handle the directory within the microSD card.
     */
    
    else{
      /* -------------------- SD handler to write from .txt file [START] ---------------------- */
      Serial.println("\n");
      Serial.println("Storing reading locally");
      filenameCounter = filenameCounter + 1; // counter++
      filenameSD = "/Reading_"; //Resets name
      filenameSD = filenameSD + String(filenameCounter)+".txt"; //File name for .txt
      File file = SD.open(filenameSD, FILE_WRITE); //Creating the file. If by any reason, a file with the same name already exists within the microSD card its data will be lost because the new file will overwrite its contents
      serializeJson(doc, file); //Writing JSON to .txt file, storing within microSD card
      file.close();
      Serial.println("\n");
      Serial.println("Successfuly stored reading!");
      /* -------------------- SD handler to write from .txt file [END] ---------------------- */
    }
       
    }
  
  //delay(1800);
}

/*
 *  Helper function to check BME688's status
 */
void checkIaqSensorStatus(void)
{
  if (iaqSensor.status != BSEC_OK) {
    if (iaqSensor.status < BSEC_OK) {
      output = "BSEC error code : " + String(iaqSensor.status);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BSEC warning code : " + String(iaqSensor.status);
      Serial.println(output);
    }
  }

  if (iaqSensor.bme680Status != BME680_OK) {
    if (iaqSensor.bme680Status < BME680_OK) {
      output = "BME680 error code : " + String(iaqSensor.bme680Status);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BME680 warning code : " + String(iaqSensor.bme680Status);
      Serial.println(output);
    }
  }
}

void errLeds(void)
{
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  delay(100);
  digitalWrite(2, LOW);
  delay(100);
}
