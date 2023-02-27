#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <AsyncElegantOTA.h>
#include <ESP8266HTTPClient.h>
#include "C:\Dev\Arduino\libraries\MyCustomStaticDefinitions\DefsWiFi.h"
#include <Arduino_JSON.h>
#include <EEPROM.h>

const char *softwareVersion = "0.90";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

WiFiEventHandler e1;

// EEPROM settings

static byte valveopen = 50;
static byte valveclose = 50;
static byte valveposition;
static long sync;
static long globalsync;
static bool timer = false;
static bool valve = false;

// Initialize LittleFS
void initLittleFS()
{
  if (!LittleFS.begin())
  {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  else
  {
    Serial.println("LittleFS mounted successfully");
  }
}

char *millisToTime(unsigned long currentMillis)
{
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  currentMillis %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  static char buffer[50];
  if (days == 0 && hours == 0 && minutes == 0)
    sprintf(buffer, "%lu sec ", seconds);
  else if (days == 0 && hours == 0 && minutes > 0)
    sprintf(buffer, "%lu min %lu sec ", minutes, seconds);
  else if (days == 0 && hours > 0)
    sprintf(buffer, "%lu h %lu m %lu s ", hours, minutes, seconds);
  else
    sprintf(buffer, "%lud %luh %lum %lus ", days, hours, minutes, seconds);
  return buffer;
}

String getOutputStates()
{
  JSONVar myArray;
  // sending stats
  myArray["stats"]["ssid"] = WiFi.SSID().c_str();
  myArray["stats"]["softwareVersion"] = softwareVersion;
  // myArray["stats"]["lastInternetTime"] = millisToTime((millis() - lastInternetTime - loopDelay));
  // myArray["stats"]["nextCheckIn"] = millisToTime((timer - millis() + loopDelay));
  // myArray["stats"]["fails"] = fails;
  // myArray["stats"]["reboots"] = reboots;
  myArray["stats"]["uptime"] = millisToTime(millis());
  myArray["stats"]["ram"] = (int)ESP.getFreeHeap();
  myArray["stats"]["frag"] = (int)ESP.getHeapFragmentation();
  if (valve)
    myArray["stats"]["valve"] = "Open";
  else
    myArray["stats"]["valve"] = "Closed";

  // // sending values
  myArray["settings"]["valveopen"] = valveopen;
  myArray["settings"]["valveclose"] = valveclose;
  // myArray["settings"]["rearCamMode"] = rearCamMode;
  // myArray["settings"]["serialOutput"] = serialOutput;
  // myArray["settings"]["loopDelay"] = loopDelay;
  // myArray["settings"]["canSpeed"] = canSpeed;
  // myArray["settings"]["canInterface"] = canInterface;

  // sending checkboxes
  myArray["checkboxes"]["timer"] = timer;
  // myArray["checkboxes"]["relay2"] = pp2Enabled;

  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state)
{
  ws.textAll(state);
}

void readEEPROMSettings()
{
  valveopen = EEPROM.read(0);
  valveclose = EEPROM.read(1);
  timer = (bool)EEPROM.read(2);
  // serialOutput = 0; // serialOutput = preferences.getInt("serialOutput", 0);
  // autoSwitch = preferences.getBool("autoSwitch", true);
  // canInterface = preferences.getInt("canInterface", 0);
  // canSpeed = preferences.getInt("canSpeed", 500);
  // rearCamMode = preferences.getInt("rearCamMode", 1);
  // loopDelay = preferences.getInt("loopDelay", 10);
}

void writeEEPROMSettings()
{
  EEPROM.put(0, valveopen);
  EEPROM.put(1, valveclose);
  EEPROM.put(2, (byte)timer);
  Serial.print("EEPROM commit ");
  Serial.println(EEPROM.commit());
  // preferences.putInt("frontCamTimeout", frontCamTimeout);
  // preferences.putInt("serialOutput", serialOutput);
  // preferences.putBool("autoSwitch", autoSwitch);
  // preferences.putInt("canInterface", canInterface);
  // preferences.putInt("canSpeed", canSpeed);
  // preferences.putInt("rearCamMode", rearCamMode);
  // preferences.putInt("loopDelay", loopDelay);
}
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    JSONVar webmsg = JSON.parse((char *)data);
    // values
    if (webmsg.hasOwnProperty("valveopen"))
    {
      valveopen = atoi(webmsg["valveopen"]);
    }
    if (webmsg.hasOwnProperty("valveclose"))
    {
      valveclose = atoi(webmsg["valveclose"]);
    }
    // if (webmsg.hasOwnProperty("frontCamTimeout"))
    //   frontCamTimeout = atoi(webmsg["frontCamTimeout"]);
    // if (webmsg.hasOwnProperty("serialOutput"))
    //   serialOutput = atoi(webmsg["serialOutput"]);
    // if (webmsg.hasOwnProperty("rearCamMode"))
    //   rearCamMode = atoi(webmsg["rearCamMode"]);
    // if (webmsg.hasOwnProperty("loopDelay"))
    //   loopDelay = atoi(webmsg["loopDelay"]);
    // if (webmsg.hasOwnProperty("canInterface"))
    //   canInterface = atoi(webmsg["canInterface"]);
    // if (webmsg.hasOwnProperty("canSpeed"))
    //   canSpeed = atoi(webmsg["canSpeed"]);
    // checkboxes
    if (webmsg.hasOwnProperty("timer"))
      timer = webmsg["timer"];
    // if (webmsg.hasOwnProperty("relay2"))
    //   switchRelay(2, webmsg["relay2"]);

    if (webmsg.hasOwnProperty("command"))
    {
      int command = atoi(webmsg["command"]);
      switch (command)
      {
      case 0:
        ESP.restart();
        break;
        // case 1:
        //   timer = timer + checkIncrement * 15;
        //   break;
        // case 2:
        //   CheckInternet();
        //   break;
      }
    }
    writeEEPROMSettings();
    notifyClients(getOutputStates());
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void initWebServer()
{
  Serial.println("Web server initialized.");
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html", false); });

  server.serveStatic("/", LittleFS, "/");

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);

  // Start server
  server.begin();
}

void setup()
{
  Serial.begin(115200);
  globalsync = millis();
  Serial.println();
  EEPROM.begin(4);
  readEEPROMSettings();
/* AP part of config*/
#ifdef ControllerAP
  const char *ssid = "oilvalvecontroller";
  const char *password = "1234567q!";
  IPAddress local_IP(192, 168, 4, 2);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  Serial.print("Setting AP configuration ... ");
  Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");

  Serial.print("Setting soft-AP ... ");
  Serial.println(WiFi.softAP(ssid, password) ? "Ready" : "Failed!");
  // WiFi.softAP(ssid);
  // WiFi.softAP(ssid, password, channel, hidden, max_connection)

  Serial.print("Soft-AP IP address = ");
  Serial.println(WiFi.softAPIP());
#else
  Serial.print("Setting STA configuration ... ");
  WiFi.begin(WIFISSID_2, WIFIPASS_2);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println(F("WiFi connected!"));
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

#endif

  // lastInternetTime = 0;
  // timer = millis() + checkIncrement * 5;
  initLittleFS();
  initWebServer();
  // delay(DefaultOffTime);
}

void loop()
{
  if (timer)
  {
    sync = millis();
    if (valve)
      valveposition = valveopen;
    else
      valveposition = valveclose;
    while (sync + valveposition * 100 > millis())
    {
      delay(200);
      if (globalsync + 500 < millis())
      {
        globalsync = millis();
        ws.cleanupClients();
        notifyClients(getOutputStates());
      }
    }
    valve = !valve;
  }
  else
  {
    ws.cleanupClients();
    notifyClients(getOutputStates());
    delay(500);
  }
}