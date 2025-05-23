#include <ESP8266WiFi.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
 
// Turn on debug statements to the serial output
#define  DEBUG  0
 
#if  DEBUG
#define PRINT(s, x) { Serial.print(F(s)); Serial.print(x); }
#define PRINTS(x) Serial.print(F(x))
#define PRINTX(x) Serial.println(x, HEX)
#else
#define PRINT(s, x)
#define PRINTS(x)
#define PRINTX(x)
#endif
 
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 8
#define CS_PIN    15 // or D8
 
// HARDWARE SPI
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
 
// WiFi login parameters - network name and password
const char* ssid = "Room-129";
const char* password = "12345678";
 
// WiFi Server object and parameters
WiFiServer server(80);
 
// Scrolling parameters
uint8_t frameDelay = 25;  // default frame delay value
textEffect_t  scrollEffect = PA_SCROLL_LEFT;
 
// Global message buffers shared by Wifi and Scrolling functions
#define BUF_SIZE  512
char curMessage[BUF_SIZE];
char newMessage[BUF_SIZE];
bool newMessageAvailable = false;
 
const char WebResponse[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";
 
const char WebPage[] =
"<!DOCTYPE html>" \
"<html>" \
"<head>" \
"<meta charset=\"UTF-8\">" \
"<title>MajicDesigns Test Page</title>" \
 
"<script>" \
"function SendData(message = '') {" \
"  var strLine = \"\";" \
"  var nocache = \"/&nocache=\" + Math.random() * 1000000;" \
"  var request = new XMLHttpRequest();" \
"  strLine = \"&MSG=\" + encodeURIComponent(message);" \
"  strLine += \"/&SD=\" + document.getElementById(\"data_form\").ScrollType.value;" \
"  strLine += \"/&I=\" + document.getElementById(\"data_form\").Invert.value;" \
"  strLine += \"/&SP=\" + document.getElementById(\"data_form\").Speed.value;" \
"  request.open(\"GET\", strLine + nocache, false);" \
"  request.send(null);" \
"}" \
"</script>" \
"</head>" \
 
"<body>" \
"<p><b>Smart Notice Board</b></p>" \
 
"<form id=\"data_form\" name=\"frmText\">" \
"<label>Message:<br><input type=\"text\" id=\"Message\" name=\"Message\" maxlength=\"255\"></label>" \
"<br><br>" \
"<input type = \"radio\" name = \"Invert\" value = \"0\" checked> Normal" \
"<input type = \"radio\" name = \"Invert\" value = \"1\"> Inverse" \
"<br>" \
"<input type = \"radio\" name = \"ScrollType\" value = \"L\" checked> Left Scroll" \
"<input type = \"radio\" name = \"ScrollType\" value = \"R\"> Right Scroll" \
"<br><br>" \
"<label>Speed:<br>Fast<input type=\"range\" name=\"Speed\" min=\"10\" max=\"200\">Slow</label>"\
"<br>" \
"</form>" \
"<br>" \
"<input type=\"button\" value=\"সময় শেষ\" onclick=\"SendData('!')\">" \
"<input type=\"button\" value=\"সিটি শুরু হবে\" onclick=\"SendData('+')\">" \
"<input type=\"button\" value=\"কাল বন্ধ\" onclick=\"SendData('-')\">" \
"</body>" \
"</html>";

const char *err2Str(wl_status_t code)
{
  switch (code)
  {
  case WL_IDLE_STATUS:    return("IDLE");           break; // WiFi is in process of changing between statuses
  case WL_NO_SSID_AVAIL:  return("NO_SSID_AVAIL");  break; // case configured SSID cannot be reached
  case WL_CONNECTED:      return("CONNECTED");      break; // successful connection is established
  case WL_CONNECT_FAILED: return("CONNECT_FAILED"); break; // password is incorrect
  case WL_DISCONNECTED:   return("CONNECT_FAILED"); break; // module is not configured in station mode
  default: return("??");
  }
}
 
uint8_t htoi(char c)
{
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) return(c - '0');
  if ((c >= 'A') && (c <= 'F')) return(c - 'A' + 0xa);
  return(0);
}
 
void getData(char *szMesg, uint16_t len)
// Message may contain data for:
// New text (/&MSG=)
// Scroll direction (/&SD=)
// Invert (/&I=)
// Speed (/&SP=)
{
  char *pStart, *pEnd;      // pointer to start and end of text
 
  // check text message
  pStart = strstr(szMesg, "/&MSG=");
  if (pStart != NULL)
  {
    char *psz = newMessage;
 
    pStart += 6;  // skip to start of data
    pEnd = strstr(pStart, "/&");
 
    if (pEnd != NULL)
    {
      while (pStart != pEnd)
      {
        if ((*pStart == '%') && isxdigit(*(pStart + 1)))
        {
          // replace %xx hex code with the ASCII character
          char c = 0;
          pStart++;
          c += (htoi(*pStart++) << 4);
          c += htoi(*pStart++);
          *psz++ = c;
        }
        else
          *psz++ = *pStart++;
      }
 
      *psz = '\0'; // terminate the string
      newMessageAvailable = (strlen(newMessage) != 0);
      PRINT("\nNew Msg: ", newMessage);
    }
  }
 
  // check scroll direction
  pStart = strstr(szMesg, "/&SD=");
  if (pStart != NULL)
  {
    pStart += 5;  // skip to start of data
 
    PRINT("\nScroll direction: ", *pStart);
    scrollEffect = (*pStart == 'R' ? PA_SCROLL_RIGHT : PA_SCROLL_LEFT);
    P.setTextEffect(scrollEffect, scrollEffect);
    P.displayReset();
  }
 
  // check invert
  pStart = strstr(szMesg, "/&I=");
  if (pStart != NULL)
  {
    pStart += 4;  // skip to start of data
 
    PRINT("\nInvert mode: ", *pStart);
    P.setInvert(*pStart == '1');
  }
 
  // check speed
  pStart = strstr(szMesg, "/&SP=");
  if (pStart != NULL)
  {
    pStart += 5;  // skip to start of data
 
    int16_t speed = atoi(pStart);
    PRINT("\nSpeed: ", P.getSpeed());
    P.setSpeed(speed);
    frameDelay = speed;
  }
}
 
void handleWiFi(void)
{
  static enum { S_IDLE, S_WAIT_CONN, S_READ, S_EXTRACT, S_RESPONSE, S_DISCONN } state = S_IDLE;
  static char szBuf[1024];
  static uint16_t idxBuf = 0;
  static WiFiClient client;
  static uint32_t timeStart;
 
  switch (state)
  {
  case S_IDLE:   // initialise
    PRINTS("\nS_IDLE");
    idxBuf = 0;
    state = S_WAIT_CONN;
    break;
 
  case S_WAIT_CONN:   // waiting for connection
  {
    client = server.available();
    if (!client) break;
    if (!client.connected()) break;
 
#if DEBUG
    char szTxt[20];
    sprintf(szTxt, "%03d:%03d:%03d:%03d", client.remoteIP()[0], client.remoteIP()[1], client.remoteIP()[2], client.remoteIP()[3]);
    PRINT("\nNew client @ ", szTxt);
#endif
 
    timeStart = millis();
    state = S_READ;
  }
  break;
 
  case S_READ: // get the first line of data
    PRINTS("\nS_READ ");
 
    while (client.available())
    {
      char c = client.read();
 
      if ((c == '\r') || (c == '\n'))
      {
        szBuf[idxBuf] = '\0';
        client.flush();
        PRINT("\nRecv: ", szBuf);
        state = S_EXTRACT;
      }
      else
        szBuf[idxBuf++] = (char)c;
    }
    if (millis() - timeStart > 1000)
    {
      PRINTS("\nWait timeout");
      state = S_DISCONN;
    }
    break;
 
  case S_EXTRACT: // extract data
    PRINTS("\nS_EXTRACT");
    // Extract the string from the message if there is one
    getData(szBuf, BUF_SIZE);
    state = S_RESPONSE;
    break;
 
  case S_RESPONSE: // send the response to the client
    PRINTS("\nS_RESPONSE");
    // Return the response to the client (web page)
    client.print(WebResponse);
    client.print(WebPage);
    state = S_DISCONN;
    break;
 
  case S_DISCONN: // disconnect client
    PRINTS("\nS_DISCONN");
    client.flush();
    client.stop();
    state = S_IDLE;
    break;
 
  default:  state = S_IDLE;
  }
}
 
void setup()
{
  Serial.begin(57600);
  PRINTS("\n[MD_Parola WiFi Message Display]\nType a message for the scrolling display from your internet browser");
 
  P.begin();
  P.setIntensity(0);
  P.displayClear();
  P.displaySuspend(false);
 
  P.displayScroll(curMessage, PA_LEFT, scrollEffect, frameDelay);
 
  curMessage[0] = newMessage[0] = '\0';
 
  // Connect to and initialise WiFi network
  PRINT("\nConnecting to ", ssid);
 
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED)
  {
    PRINT("\n", err2Str(WiFi.status()));
    delay(500);
  }
  PRINTS("\nWiFi connected");
 
  // Start the server
  server.begin();
  PRINTS("\nServer started");
 
  // Set up first message as the IP address
  sprintf(curMessage, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  //Serial.println(curMessage);
  PRINT("\nAssigned IP ", curMessage);
}
 
char ip[100];

void loop()
{
  handleWiFi();
 
  if (P.displayAnimate())
  {
    Serial.println(curMessage);
    delay(10000);
    if (newMessageAvailable)
    {
      strcpy(curMessage, newMessage);
      newMessageAvailable = false;
    }

    P.displayReset();

    // Set up first message as the IP address
  //sprintf(ip, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  //Serial.println(ip);
  //delay(100);
  }
}
