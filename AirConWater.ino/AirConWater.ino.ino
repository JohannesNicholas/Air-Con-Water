
#include "Arduino_LED_Matrix.h"

#include "WiFiS3.h"
#include "WiFiSSLClient.h"
#include "IPAddress.h"

#include "arduino_secrets.h" 



//ultrasonic 
const int trigPin = 9;
const int echoPin = 10;
float duration, distance;
const float maxDistanceToWater = 10;
const float minDistanceToWater = 7;


//Display
ArduinoLEDMatrix matrix;
byte frame[8][12] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

//WIFI
char ssid[] = SECRET_SSID; //Gets these from the arduino_secrets.h file
char pass[] = SECRET_PASS; 
int status = WL_IDLE_STATUS;
char server[] = "evrbiwgkzqwgxjmauhoh.supabase.co";
WiFiSSLClient client;



//My pump logic
int pumpCountdown = 10;
const int MOS_PIN = 8; //the pin the mosfet is connected to that controls the pump
int nTimeouts = 0; //now many times the pump has timed out



//Call this when the pump turns on or off and it will insert to the DB
void writeToLog(bool isOn) {
  Serial.print("\nWriting to log...");
  Serial.println(isOn);


  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network.
    status = WiFi.begin(ssid, pass);
     
    // wait 10 seconds for connection:
    delay(10000);
  }
  
  printWifiStatus();
 
  Serial.println("\nStarting connection to server...");
  // if you get a connection, report back via serial:
  
  if (client.connect(server, 443)) {
    Serial.println("connected to server");
    // Make a HTTP request:
    client.println("POST /rest/v1/pumplog HTTP/1.1");
    client.println("Host: evrbiwgkzqwgxjmauhoh.supabase.co");

    String apikey_header = "apikey: ";
    apikey_header += API_KEY;
    client.println(apikey_header);

    String auth_header = "Authorization: Bearer ";
    auth_header += API_KEY;
    client.println(auth_header);
    client.println("Prefer: return=minimal");
    

    String data = "{ \"is_on\": \"false\"}";
    if (isOn) {
      data = "{ \"is_on\": \"true\"}";
    }

    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(data.length());
    client.println();
    client.print(data);

    delay(500); // Can be changed
    if (client.connected()) { 
      client.stop();  // DISCONNECT FROM THE SERVER
    }
    Serial.println();
    Serial.println("closing connection");
    delay(5000);
  }
}


/* -------------------------------------------------------------------------- */
void printWifiStatus() {
/* -------------------------------------------------------------------------- */  
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}


/* just wrap the received data up to 80 columns in the serial print*/
/* -------------------------------------------------------------------------- */
void read_response() {
/* -------------------------------------------------------------------------- */  
  uint32_t received_data_num = 0;
  while (client.available()) {
    /* actual data reception */
    char c = client.read();
    /* print data to serial port */
    Serial.print(c);
    /* wrap data to 80 columns*/
    received_data_num++;
    if(received_data_num % 80 == 0) { 
      Serial.println();
    }
  }  

  if (!client.connected()) {
    Serial.println();
    Serial.println("disconnecting from server.");
    client.stop();
  }
}



void turnPumpOn() {
  //log then turn on pump
  writeToLog(true);
  digitalWrite(MOS_PIN, HIGH);
}

void turnPumpOff() {
  //turn off pump then write to log
  digitalWrite(MOS_PIN, LOW);
  writeToLog(false);
}



void setup() {
  //ultrasonic
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  Serial.begin(115200);
  Serial.println("Starting up!");

  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }


  //display
  matrix.begin();


  //logic
  pinMode(MOS_PIN, OUTPUT);
}

void loop() {

  //WIFI:
  read_response();

  //Ultrasonic
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = (duration*.0343)/2;
  Serial.print("Distance: ");
  Serial.println(distance);


  //Percent Full (lower distance = higher percent full)
  int percent = 100 - (distance - minDistanceToWater) / (maxDistanceToWater - minDistanceToWater) * 100;

  //Display
  int distanceGraph = 8 * percent / 100;
  for (int i = 0; i < 8; i++) {
    frame[i][0] = 7 - i < distanceGraph;
  }
  matrix.renderBitmap(frame, 8, 12);


  

  //My pump logic
  if (distance < minDistanceToWater && pumpCountdown == 0 && nTimeouts < 4) {
    //Turn on pump because water is to high
    pumpCountdown = 700; //how many 10ths of a second, so 500 is 50 seconds
    turnPumpOn();
  }

  if (pumpCountdown > 0) {
    pumpCountdown--;
    if (pumpCountdown == 0) {
      //timeout on pump
      nTimeouts++;
      //Turn off pump
      turnPumpOff();
    }

    if (distance > maxDistanceToWater) {
      //Turn off pump because water is too low
      pumpCountdown = 0;
      turnPumpOff();

      nTimeouts--;
    }
  }

  



  delay(100);
}