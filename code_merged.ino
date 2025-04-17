#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <TimeLib.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>

#define DEBUG 0

#if DEBUG
#define PRINT(s, x) \
  { \
    Serial.print(F(s)); \
    Serial.print(x); \
  }
#define PRINTS(x) Serial.print(F(x))
#define PRINTX(x) Serial.println(x, HEX)
#else
#define PRINT(s, x)
#define PRINTS(x)
#define PRINTX(x)
#endif

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN 14      //D5
#define DATA_PIN 13     //D7
#define CS_PIN_TIME 0   //D3
#define CS_PIN_DATE 15  //D8
#define BUZZER_PIN 12  //D6
#define WIFI_TIMEOUT 10000
#define NTP_TIMEOUT 10000

#define CS_PIN    2 // or D4

// WiFi Server object and parameters
WiFiServer server(80);

// Scrolling parameters
uint8_t frameDelay = 25;  // default frame delay value
textEffect_t scrollEffect = PA_SCROLL_LEFT;

int pinCS = 4;  // Attach CS to D2, DIN to MOSI and CLK to SCK
int numberOfHorizontalDisplays = 4;
int numberOfVerticalDisplays = 1;

Max72xxPanel GD = Max72xxPanel(pinCS, numberOfHorizontalDisplays, numberOfVerticalDisplays);
MD_Parola P_time = MD_Parola(HARDWARE_TYPE, CS_PIN_TIME, MAX_DEVICES);
MD_Parola P_date = MD_Parola(HARDWARE_TYPE, CS_PIN_DATE, MAX_DEVICES);
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Global message buffers shared by Wifi and Scrolling functions
#define BUF_SIZE 512
char curMessage[BUF_SIZE];
char newMessage[BUF_SIZE];
bool newMessageAvailable = false;

// WiFi login parameters - network name and password
const char* ssid = "Room 312";
const char* password = "19041655";

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

const char* err2Str(wl_status_t code) {
  switch (code) {
    case WL_IDLE_STATUS: return ("IDLE"); break;               // WiFi is in process of changing between statuses
    case WL_NO_SSID_AVAIL: return ("NO_SSID_AVAIL"); break;    // case configured SSID cannot be reached
    case WL_CONNECTED: return ("CONNECTED"); break;            // successful connection is established
    case WL_CONNECT_FAILED: return ("CONNECT_FAILED"); break;  // password is incorrect
    case WL_DISCONNECTED: return ("CONNECT_FAILED"); break;    // module is not configured in station mode
    default: return ("??");
  }
}

uint8_t htoi(char c) {
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) return (c - '0');
  if ((c >= 'A') && (c <= 'F')) return (c - 'A' + 0xa);
  return (0);
}

void getData(char* szMesg, uint16_t len)
// Message may contain data for:
// New text (/&MSG=)
// Scroll direction (/&SD=)
// Invert (/&I=)
// Speed (/&SP=)
{
  char *pStart, *pEnd;  // pointer to start and end of text

  // check text message
  pStart = strstr(szMesg, "/&MSG=");
  if (pStart != NULL) {
    char* psz = newMessage;

    pStart += 6;  // skip to start of data
    pEnd = strstr(pStart, "/&");

    if (pEnd != NULL) {
      while (pStart != pEnd) {
        if ((*pStart == '%') && isxdigit(*(pStart + 1))) {
          // replace %xx hex code with the ASCII character
          char c = 0;
          pStart++;
          c += (htoi(*pStart++) << 4);
          c += htoi(*pStart++);
          *psz++ = c;
        } else
          *psz++ = *pStart++;
      }

      *psz = '\0';  // terminate the string
      newMessageAvailable = (strlen(newMessage) != 0);
      PRINT("\nNew Msg: ", newMessage);
    }
  }

  // check scroll direction
  pStart = strstr(szMesg, "/&SD=");
  if (pStart != NULL) {
    pStart += 5;  // skip to start of data

    PRINT("\nScroll direction: ", *pStart);
    scrollEffect = (*pStart == 'R' ? PA_SCROLL_RIGHT : PA_SCROLL_LEFT);
    P_date.setTextEffect(scrollEffect, scrollEffect);
    P_date.displayReset();
  }


  // check invert
  pStart = strstr(szMesg, "/&I=");
  if (pStart != NULL) {
    pStart += 4;  // skip to start of data

    PRINT("\nInvert mode: ", *pStart);
    P_date.setInvert(*pStart == '1');
  }

  // check speed
  pStart = strstr(szMesg, "/&SP=");
  if (pStart != NULL) {
    pStart += 5;  // skip to start of data

    int16_t speed = atoi(pStart);
    PRINT("\nSpeed: ", P.getSpeed());
    P_date.setSpeed(speed);
    frameDelay = speed;
  }
}

void handleWiFi(void) {
  static enum { S_IDLE,
                S_WAIT_CONN,
                S_READ,
                S_EXTRACT,
                S_RESPONSE,
                S_DISCONN } state = S_IDLE;
  static char szBuf[1024];
  static uint16_t idxBuf = 0;
  static WiFiClient client;
  static uint32_t timeStart;

  switch (state) {
    case S_IDLE:  // initialise
      PRINTS("\nS_IDLE");
      idxBuf = 0;
      state = S_WAIT_CONN;
      break;

    case S_WAIT_CONN:  // waiting for connection
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

    case S_READ:  // get the first line of data
      PRINTS("\nS_READ ");

      while (client.available()) {
        char c = client.read();

        if ((c == '\r') || (c == '\n')) {
          szBuf[idxBuf] = '\0';
          client.flush();
          PRINT("\nRecv: ", szBuf);
          state = S_EXTRACT;
        } else
          szBuf[idxBuf++] = (char)c;
      }
      if (millis() - timeStart > 1000) {
        PRINTS("\nWait timeout");
        state = S_DISCONN;
      }
      break;

    case S_EXTRACT:  // extract data
      PRINTS("\nS_EXTRACT");
      // Extract the string from the message if there is one
      getData(szBuf, BUF_SIZE);
      state = S_RESPONSE;
      break;

    case S_RESPONSE:  // send the response to the client
      PRINTS("\nS_RESPONSE");
      // Return the response to the client (web page)
      client.print(WebResponse);
      client.print(WebPage);
      state = S_DISCONN;
      break;

    case S_DISCONN:  // disconnect client
      PRINTS("\nS_DISCONN");
      client.flush();
      client.stop();
      state = S_IDLE;
      break;

    default: state = S_IDLE;
  }
}


const char* ntpServerName = "pool.ntp.org";
const long timezoneOffset = 6 * 3600;  // 6 hours converted to seconds
const char* gMonthNames[] = { "", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
const char* monthNames[] = { "", "b", "j", "a", "g", "d", "e", "k", "o", "p", "m", "f", "c" };
const char* weekNames[] = { "", "$", "%", "&", "(", ")", "*", "#" };
const char* weeks[] = { "", " SUN", " MON", " TUE", " WED", " THU", " FRI", " SAT" };


int conv2bangla(int date, int mash, int bochor, int konta) {

  if (mash >= 4 && mash <= 12) {
    bochor = bochor - 593;
  } else {
    bochor = bochor - 1 - 593;
  }

  if (mash == 4) {

    if (bochor < 14) {
      mash = 12;
      //name="Chaitra";
      date = date + 14 + 3;
    } else if (date >= 14) {
      mash = 1;
      //name="Boishakh";
      date = date - 14 + 1;
    }

  } else if (mash == 5) {

    if (date < 15) {
      mash = 1;
      //name="Boishakh";
      date = date + 15 + 2;
    } else if (date >= 15) {
      mash = 2;
      //name="Joistha";
      date = date - 15 + 1;
    }

  } else if (mash == 6) {

    if (date < 15) {
      mash = 2;
      //name="Joistha";
      date = date + 15 + 2;
    } else if (date >= 15) {
      mash = 3;
      //name="Ashar";
      date = date - 15 + 1;
    }
  } else if (mash == 7) {

    if (date < 16) {
      mash = 3;
      //name="Ashar";
      date = date + 16;
    } else if (date >= 16) {
      mash = 4;
      //name="Srabon";
      date = date - 16 + 1;
    }
  } else if (mash == 8) {

    if (date < 16) {
      mash = 4;
      //name="Srabon";
      date = date + 16;
    } else if (date >= 16) {
      mash = 5;
      //name="Vadro";
      date = date - 16 + 1;
    }
  } else if (mash == 9) {

    if (date < 16) {
      mash = 5;
      //name="Vadro";
      date = date + 16;
    } else if (date >= 16) {
      mash = 6;
      //name="Ashwin";
      date = date - 16 + 1;
    }
  } else if (mash == 10) {

    if (date < 16) {
      mash = 6;
      //name="Aswin";
      date = date + 15;
    } else if (date >= 16) {
      mash = 7;
      //name="Kartik";
      date = date - 16 + 1;
    }
  } else if (mash == 11) {

    if (date < 15) {
      mash = 7;
      //name="Kartik";
      date = date + 15 + 1;
    } else if (date >= 15) {
      mash = 8;
      //name="Agrahoyon";
      date = date - 15 + 1;
    }

  } else if (mash == 12) {

    if (date < 15) {
      mash = 8;
      //name="Agrahoyon";
      date = date + 15 + 1;
    } else if (date >= 15) {
      mash = 9;
      //name="Poush";
      date = date - 15 + 1;
    }
  } else if (mash == 1) {

    if (date < 14) {
      mash = 9;
      //name="Poush";
      date = date + 14 + 3;
    } else if (date >= 14) {
      mash = 10;
      //name="Magh";
      date = date - 14 + 1;
    }

  } else if (mash == 2) {

    if (date < 13) {
      mash = 10;
      //name="Magh";
      date = date + 13 + 5;
    } else if (date >= 13) {
      mash = 11;
      //name="Falgun";
      date = date - 13 + 1;
    }
  } else if (mash == 3) {

    if (date < 15) {
      mash = 11;
      //name="Falgun";
      date = date + 15 + 2;
    } else if (date >= 15) {
      mash = 12;
      //name="Choitra";
      date = date - 15 + 1;
    }
  }
  /*Serial.println("Function er vitore");
    Serial.println(date);
    Serial.println(mash);
    Serial.println(bochor);*/
  if (!konta) return date;
  else if (konta == 1) return mash;
  else return bochor;
}

void setup() {
  Serial.begin(57600);
  PRINTS("\n[MD_Parola WiFi Message Display]\nType a message for the scrolling display from your internet browser");
 
  GD.setIntensity(0);  // Set brightness between 0 and 15
  for (int i = 0; i < numberOfHorizontalDisplays * numberOfVerticalDisplays; i++) {
    GD.setRotation(i, 1);  // 1 stands for 90 degrees clockwise
  }
 

  // Connect to WiFi with timeout
  /*WiFi.begin(ssid, password);
  unsigned long wifiStartTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < WIFI_TIMEOUT) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
*/
// for showing notice
  P.begin();
  P.setIntensity(0);
  P.displayClear();
  P.displaySuspend(false);
  P.displayScroll(curMessage, PA_LEFT, scrollEffect, frameDelay);

  curMessage[0] = newMessage[0] = '\0';

  WiFiManager wifiManager;
  //wifiManager.autoConnect("BanglaClock 2.0");

  // Connect to and initialise WiFi network
  PRINT("\nConnecting to ", ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    PRINT("\n", err2Str(WiFi.status()));
    delay(500);
  }
  PRINTS("\nWiFi connected");

  // Start the server
  server.begin();
  PRINTS("\nServer started");

  // Set up first message as the IP address
  sprintf(curMessage, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  PRINT("\nAssigned IP ", curMessage);
  //ip
  //Serial.println(curMessage);

  configTime(timezoneOffset, 0, ntpServerName);//change

  unsigned long ntpStartTime = millis();
  while (!time(nullptr) && millis() - ntpStartTime < NTP_TIMEOUT) {
    Serial.print(".");
    delay(1000);
  }
  if (time(nullptr)) {
    Serial.println("\nTime set!");
    delay(1000);
  } else {
    Serial.println("\nFailed to get NTP time. Using last known time.");
    delay(1000);
  }

  P_time.begin();
P_date.begin();

P_time.setIntensity(0);  // Set the brightness to a visible level
P_date.setIntensity(0);  // Set the brightness to a visible level

pinMode(BUZZER_PIN, OUTPUT);


}

char ip[100];

void loop() 
{

  handleWiFi();

  if (P.displayAnimate())
  {
    Serial.println(curMessage);
    delay(1000);
    if (newMessageAvailable)
    {
      strcpy(curMessage, newMessage);
      newMessageAvailable = false;
    }

    P.displayReset();

    // Set up first message as the IP address
  sprintf(ip, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  Serial.println(ip);
  delay(1000);
  }

  time_t now = time(nullptr) + timezoneOffset;
  int hours = hour(now);
  if (hours > 12) {
    hours = hours - 12;
  }

  if (hours == 0)
    hours = 12;
  int minutes = minute(now);
  int seconds = second(now);

  if (minutes==0&&hours==0)
    digitalWrite(BUZZER_PIN, HIGH);
  else
    digitalWrite(BUZZER_PIN, LOW);

  char timeStr[9];
  if (seconds % 2 == 1) {
    if ((minutes % 10 == 1 || minutes % 10 == 2 || minutes % 10 == 0 || minutes / 10 == 1 || minutes / 10 == 2 || minutes / 10 == 0) && hours > 2)
      sprintf(timeStr, "%d:%02d", hours, minutes);

    else
      sprintf(timeStr, " %d:%02d", hours, minutes);
  } else {
    if (minutes % 10 == 1 || minutes % 10 == 2 || minutes % 10 == 0 || minutes / 10 == 1 || minutes / 10 == 2 || minutes / 10 == 0)
      sprintf(timeStr, "%d %02d", hours, minutes);

    else
      sprintf(timeStr, " %d %02d", hours, minutes);
  }

  Serial.println(timeStr);
  delay(10000);

  P_time.displayClear();
  P_time.displayZoneText(0, timeStr, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  P_time.displayAnimate();

  int gDay = day(now);
  int gMonth = month(now);
  int gYear = year(now);
  int weekDay = weekday(now);



  char gDateStr[15];  // Enough space for day, month abbreviation, and year
  sprintf(gDateStr, "%d%s", gDay, gMonthNames[gMonth]);

  Serial.println(gDateStr);
  delay(10000);

  Serial.println(gYear);
  delay(10000);

  Serial.println(weekDay);
  delay(10000);

  if (seconds % 4 == 0 || seconds % 4 == 1) {
      GD.fillScreen(LOW);
      GD.setCursor(0, 1);
      GD.print(gDateStr);
      GD.write();
    }

    else {
      GD.fillScreen(LOW);
      GD.setCursor(0, 1);
      GD.print(weeks[weekDay]);
      GD.write();
    }



  int bDay = conv2bangla(gDay, gMonth, gYear, 0);
  int bMonth = conv2bangla(gDay, gMonth, gYear, 1);
  int bYear = conv2bangla(gDay, gMonth, gYear, 2);

  char dateStr[15];  // Enough space for day, month abbreviation, and year
  sprintf(dateStr, "%d%s", bDay, monthNames[bMonth]);


  Serial.println(dateStr);
  delay(10000);

  Serial.println(bYear);
  delay(10000);

  if (P_date.displayAnimate()) {
    if (newMessageAvailable) {
      strcpy(curMessage, newMessage);
      newMessageAvailable = false;
    }
    P_date.displayReset();
  }

  else {

    if (seconds % 4 == 2 || seconds % 4 == 3) {
    P_date.displayClear();
    P_date.displayZoneText(0, weekNames[weekDay], PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    P_date.displayAnimate();
  }

  else {
    P_date.displayClear();
    P_date.displayZoneText(0, dateStr, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    P_date.displayAnimate();
  }
    
}

  
}
