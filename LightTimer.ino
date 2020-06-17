/*
 *  Light Timer - June 2020
 *  
 *  The purpose of this project is pretty simple. Replace outdoor light timers with clunky user interfaces that make it difficult to setup with something that is plug and play.
 *  The way I accomplish this is actually not too difficult. Find an Arduino with internet capabilities, buy a relay that can handle high wattage AC, and get an lcd display.
 *  After I got all those components I started piecing them together on a breadboard to write the code for. First thing to do was get the lcd working. Following the lcd is the
 *  time code. Once that's done add the relay code. Add in a spice of an override switch and daylight savings time accountability and the code is done. With any piece of code that's
 *  assembled like a lego set, I could not of done this without the resources provided below.
 *  
 *  Resources used:
 *  https://arduinojson.org/
 *  https://timezonedb.com/api
 *  https://sunrise-sunset.org/api
 *  https://www.arduino.cc/en/Tutorial/HelloWorld
 *  https://www.arduino.cc/en/Tutorial/UdpNTPClient
 *  https://www.arduino.cc/en/Tutorial/WiFiWebClient
 *  
 */
#include <LiquidCrystal.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "sensitiveData.h";

//variable keeps track if setup() is currently running or not
bool isSetup; //I need this so that when the arduino is booting up it will still grab the sunrise and sunset times from the server without it being 12:01AM

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(12, 11, 6, 5, 4, 3);

char *ssid = NETWORK_SSID;
char *pass = NETWORK_PASS;
int status = WL_IDLE_STATUS; //wifi radio's status
int retryAuth = 0; //counter for how many times authentication has been attempted with wifi network

unsigned int localPort = 2390;
IPAddress timeServer(208, 67, 75, 242); //NTP server ip
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP Udp;

char dstServer[] = "api.timezonedb.com"; //server for daylight savings time checking
WiFiClient dstClient; //wifi client for daylight savings time checking

char sunRSServer[] = "api.sunrise-sunset.org"; //sunrise sunset server
WiFiClient sunRSClient; //the client used to make api connections to sunrise sunset server

int relayPin = 8; //pin number for the relay trigger
int overridePin = 2;
bool lightStateVal;
bool overrideStateVal = false; //override is not activated by default

struct localTime
{
  String printTime;
  int militaryHour;
  int minute;
} myTime;

enum RSTime
{
  RISE_HOUR,
  RISE_MIN,
  SET_HOUR,
  SET_MIN
};

/*************************************************************
 * Function: setup ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/13/2020
 * Description: function is the entrance point for the arduino. 
 *              This function gets executed first to setup arduino.
 * Input parameters: none
 * Returns: void
 *************************************************************/
void setup() 
{
  isSetup = true; //set the boolean to true since we are in the setup function
  
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  
  // set up network connection
  setupNetwork();

  Udp.begin(localPort);

  pinMode(relayPin, OUTPUT); //setting relay trigger to output
  pinMode(overridePin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(overridePin), overrideRelay, CHANGE);

  getLocalTime(); //get the current time to initialize myTime
  checkSchedule(); //check the schedule in setup to get the sunrise and sunset when booting up
  isSetup = false; //exiting the setup function so set the boolean to false
}

/*************************************************************
 * Function: loop ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/13/2020
 * Description: function is the executing body of the arduino. 
 *              Once setup is performed this function loops until shutdown.
 *              This is where the arduino updates the lcd and checks if the relay
 *              should be on or not.
 * Input parameters: none
 * Returns: void
 *************************************************************/
void loop() 
{
  checkSchedule();
  updateDisplay();
}

/*************************************************************
 * Function: setupNetwork ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/9/2020
 * Description: function is called during setup to connect to the wifi network
 *              for all the internet communication needed during uptime.
 *              This function does all the checking and connecting. If something
 *              goes wrong it will display a message to the lcd and depending on
 *              the severity of the failure it may loop until manually shut down.
 * Input parameters: none
 * Returns: void
 *************************************************************/
void setupNetwork()
{
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) 
  {
    lcd.print("Wifi Module Fail");
    lcd.setCursor(0, 1);
    lcd.print("Reset Device");
    // don't continue
    while(true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) 
  {
    lcd.clear();
    lcd.print("Wifi Firmware");
    lcd.setCursor(0, 1);
    lcd.print("Outdated, update");
    delay(3000);
  }

  //write to display attempting connection message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Attempting");
  lcd.setCursor(0, 1);
  lcd.print("Connection");

  // attempt to connect to Wifi network:
  while (status != WL_CONNECTED) 
  {
    if(retryAuth > 0 && retryAuth < 5) //if attempts is between 1-4
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Attempt Failed");
      lcd.setCursor(0, 1);
      lcd.print("Retrying...");
    }
    else if(retryAuth >= 5) //if attempts exceed 4
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Connect Failed");
      lcd.setCursor(0, 1);
      lcd.print("Reset Device");
      while(true);
    }
    
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
    retryAuth++;
  }

  // Connection established message
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Connect Success");
  delay(3000);
}

/*************************************************************
 * Function: updateDisplay ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/13/2020
 * Description: function takes care of the lcd display. This function houses
 *              all the calls to check and update the time, update the state of the light,
 *              and let the user know if the override is on or not.
 * Input parameters: none
 * Returns: void
 *************************************************************/
void updateDisplay()
{
  localTime myTime;

  myTime = getLocalTime();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(lightState());
  lcd.setCursor(0, 1);
  
  if(overrideStateVal) //if the user presses the override button to enable the outlets regardless of the timer, display an override on message
    lcd.print("OVERRIDE ON");
  else
    lcd.print(myTime.printTime);
    
  delay(5000); //update display every ten seconds
}

/*************************************************************
 * Function: sendNTPpacket ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/9/2020
 * Description: function handles creating and sending the NTP packet to the NTP server.
 *              This function was not written by me at all. (Author: Michael Margolis)
 * Input parameters: IPAddress& address
 * Returns: unsigned long
 *************************************************************/
unsigned long sendNTPpacket(IPAddress& address) 
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

/*************************************************************
 * Function: getLocalTime ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/13/2020
 * Description: function handles getting the time from NTP server and 
 *              calling helper function to convert it down to its 24hr format
 *              and 12hr format with a print message for the lcd.
 * Input parameters: none
 * Returns: localTime
 *************************************************************/
localTime getLocalTime()
{
  //this block is used to extract the raw time out of the packet and get it into epoch
  unsigned long highWord;
  unsigned long lowWord;
  unsigned long secsSince1900; //ntp packet returns seconds since jan 1 1900
  const unsigned long seventyYears = 2208988800UL;; //amount of seconds in 70 years
  unsigned long epoch; //epoch is amount of seconds since jan 1 1970

  //this block gets the time into hour and minute format with am/pm
  int militaryHour;
  int hour;
  int minute;
  String dayOrNight; //place to store am/pm result
  
  String timeShort = ""; //working string for combining everything

  //initialize struct in case of packet error
  myTime.printTime = "Time Error";
  myTime.militaryHour = -1;
  myTime.minute = -1; 

  sendNTPpacket(timeServer); //send an NTP packet to a time server
  delay(1000); //wait to see if a reply is available
  
  if(Udp.parsePacket()) //We've received a packet, read the data from it
  {
    Udp.read(packetBuffer, NTP_PACKET_SIZE); //read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:
    highWord = word(packetBuffer[40], packetBuffer[41]);
    lowWord = word(packetBuffer[42], packetBuffer[43]);
    
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    secsSince1900 = highWord << 16 | lowWord;
    
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;

    //Calculate hours and minutes and convert to correct pst time zone and am/pm vs 24 hour
    militaryHour = convertTimeZone(((epoch  % 86400L) / 3600)); //86400 seconds in a day, modulus of epoch with that leaves hours in seconds, divide by how many seconds in an hour to get the gmt hour in military time, minus 7 to get it from gmt to pst
    
    
    
    hour = convertTimeFormat(militaryHour); //convert to am/pm
    minute = (epoch  % 3600) / 60; //remove hours with modulus and leave just minutes in seconds, divide by 60 to get the amount of minutes in the hour
    dayOrNight = getAMPM(militaryHour); //use military time to get am/pm string

    //create hour and minute string with am/pm for printing. We will use the String constructor to concatenate all the different ints and strings into one string
    timeShort += hour;
    timeShort += ":";
    
    if (((epoch % 3600) / 60) < 10) // In the first 10 minutes of each hour, we'll want a leading '0'
    {
      timeShort += "0";
    }
    
    timeShort += minute;
    timeShort += dayOrNight;

    myTime.printTime = timeShort;
    myTime.militaryHour = militaryHour;
    myTime.minute = minute;
  }

  return myTime;
}

/*************************************************************
 * Function: convertTimeZone ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/17/2020
 * Description: function handles taking in the hour given by the NTP
 *              and converting it (with consideration of daylight savings time)
 *              to PST in the 24 hour format.
 * Input parameters: int militaryHour
 * Returns: int 
 *************************************************************/
int convertTimeZone(int militaryHour)
{
  String serverResponse = "";
  char c;
  char endOfHeaders[] = "\r\n\r\n"; //string that marks the end of the http request header
  int gmtOffset = 0;
  
  const size_t capacity = JSON_OBJECT_SIZE(13) + 230; //json size for our specific request. I used https://arduinojson.org/v6/assistant/ to determine what the expression should be
  DynamicJsonDocument doc(capacity); //preallocate the json document

  if(dstClient.connect(dstServer, 80)) //do all of this work IF the connection is successful
  {
    dstClient.println(DST_GET_REQUEST);
    dstClient.println("Host: api.timezonedb.com"); 
    dstClient.println("Connection: close");
    dstClient.println();
  
    delay(250); //put a delay so the server can respond to the get request
  
    dstClient.find(endOfHeaders); //by finding where the header is it skips the header
  
    while(dstClient.available())
    {
      c = dstClient.read();
      serverResponse += c;
    }
    
    serverResponse = serverResponse.substring(serverResponse.indexOf("{"), serverResponse.lastIndexOf("}") + 1); //clean up the response to only capture the json request
    
    deserializeJson(doc, serverResponse); //decode the json into the individual objects

    if(String(doc["dst"].as<char*>()).equals("1")) 
      gmtOffset = 7;
    else if(String(doc["dst"].as<char*>()).equals("0"))
      gmtOffset = 8;
    else
      gmtOffset = 0;
      
  }

  if(!dstClient.connected())
    dstClient.stop();

  if(gmtOffset == 0) //if the body of the server request fails then the gmtOffset will not occur so we default the offset to seven hours.
    gmtOffset = 7;
  
  militaryHour -= gmtOffset;
  
  if(militaryHour < 0) //the gmt to pst conversion will cause a negative number from 5:00pm to 11:59pm so we need to convert those into positives
      militaryHour += 24; //our very own int overflow

  return militaryHour;
}

/*************************************************************
 * Function: convertTimeFormat ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/13/2020
 * Description: function takes in a 24hr formatted hour and converts
 *              it to a 12hr format.
 * Input parameters: int militaryHour
 * Returns: int
 *************************************************************/
int convertTimeFormat(int militaryHour)
{
  int hour;

  if(militaryHour == 0) //if it's midnight
  {
    hour = militaryHour + 12; //add 12 to make it midnight for standard am/pm format
  }
  else if(militaryHour > 12) //if it's passed 12 military time subtract 12 to convert it to am/pm format
  {
    hour = militaryHour - 12;
  }
  else //all other times from 1-12 are normal
  {
    hour = militaryHour;
  }

  return hour;
}

/*************************************************************
 * Function: getAMPM ()                    
 * Date Created: 6/9/2020
 * Date Last Modified: 6/13/2020
 * Description: function takes in the 24hr format and returns a 
 *              String representation of if it's AM or PM.
 * Input parameters: int militaryHour 
 * Returns: String
 *************************************************************/
String getAMPM(int militaryHour)
{
  if(militaryHour > 11) //determine if it's day or night based off the military time
  {
      return " PM";
  }
  else
  {
    return " AM";
  }
}

/*************************************************************
 * Function: checkSchedule ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/14/2020
 * Description: function checks if the current time is after sunset 
 *              and before sunrise to enable the relay. Function
 *              also once a day checks when sunrise and sunset is.
 *              (also at bootup)
 * Input parameters: none
 * Returns: void
 *************************************************************/
void checkSchedule()
{ 
  static int sunRSTime[4];
  if((myTime.militaryHour == 0 && myTime.minute == 1) || isSetup) // if the time is 12:01 AM. The reason its outside of the override is because the sunrise and sunset times should be independent of the override
      getSunRSTime(sunRSTime); //get the sunrise and sunset time. We do this once a day to reduce how many requests go to the sunrise-sunset.org server
      
  if(overrideStateVal == false) //check schedules functionality will only occur if the override is not enabled
  {
    if(((myTime.militaryHour == sunRSTime[RISE_HOUR] && myTime.minute <= sunRSTime[RISE_MIN]) || myTime.militaryHour < sunRSTime[RISE_HOUR]) || ((myTime.militaryHour == sunRSTime[SET_HOUR] && myTime.minute >= sunRSTime[SET_MIN]) || (myTime.militaryHour > sunRSTime[SET_HOUR]))) //if the time is after sunset or before sunrise, the relay should be enabled
    {
      enableRelay();
    }
    else
    {
      disableRelay();
    }
  }
}

/*************************************************************
 * Function: getSunRSTime ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/17/2020
 * Description: function asks a sunrise sunset server what time 
 *              sunrise and sunset are and return it in PST.
 * Input parameters: int sunRSTime[4]
 * Returns: void
 *************************************************************/
void getSunRSTime(int sunRSTime[4])
{
  String serverResponse = "";
  char c;
  char endOfHeaders[] = "\r\n\r\n"; //string that marks the end of the http request header
  
  const size_t capacity = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(10) + 480; //json size for our specific request. I used https://arduinojson.org/v6/assistant/ to determine what the expression should be
  DynamicJsonDocument doc(capacity); //preallocate the json document
  JsonObject results; //the doc stores everything useful under results

  sunRSTime[RISE_HOUR] = 5; //default sunrise and sunset times in case of server failure, sunrise at 5:45 am and sunset at 7:00pm
  sunRSTime[RISE_MIN] = 45;
  sunRSTime[SET_HOUR] = 19;
  sunRSTime[SET_MIN] = 0;

  
  String sunrise; //string that holds the json response for sunrise, will use it to extract the times we need
  String sunset; //string that holds the json response for sunset, will use it to extract the times we need
  
  if(sunRSClient.connect(sunRSServer, 80)) //do all of this work IF the connection is successful
  {
    sunRSClient.println(RS_GET_REQUEST);
    sunRSClient.println("Host: api.sunrise-sunset.org"); 
    sunRSClient.println("Connection: close");
    sunRSClient.println();
  
  
    delay(250); //put a delay so the server can respond to the get request
  
    sunRSClient.find(endOfHeaders); //by finding where the header is it skips the header
  
    while(sunRSClient.available())
    {
      c = sunRSClient.read();
      serverResponse += c;
    }
    
    serverResponse = serverResponse.substring(serverResponse.indexOf("{"), serverResponse.lastIndexOf("}") + 1); //clean up the response to only capture the json request
    
    deserializeJson(doc, serverResponse); //decode the json into the individual objects
    results = doc["results"]; //assign the results object to its own object so we can easily access the data thats a child of result
    
    sunrise = String(results["sunrise"].as<char*>());
    sunset = String(results["sunset"].as<char*>());
  
    sunRSTime[RISE_HOUR] = convertTimeZone(sunrise.substring(11, 13).toInt()); //untouched response looks like 2020-06-13T02:57:58+00:00, this grabs the hour and converts it to an int and then changes its time zone from UTC to PST
    sunRSTime[RISE_MIN] = sunrise.substring(14, 16).toInt(); //grabs just the minute and converts it
    sunRSTime[SET_HOUR] = convertTimeZone(sunset.substring(11, 13).toInt());
    sunRSTime[SET_MIN] = sunset.substring(14, 16).toInt();
  }
  
  if(!sunRSClient.connected())
    sunRSClient.stop();
}

/*************************************************************
 * Function: enableRelay ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/13/2020
 * Description: function enables the relay.
 * Input parameters: none
 * Returns:  void
 *************************************************************/
void enableRelay()
{
  digitalWrite(relayPin, HIGH);
  lightStateVal = true;
}

/*************************************************************
 * Function: disableRelay ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/13/2020
 * Description: function disables the relay.
 * Input parameters: none
 * Returns: void
 *************************************************************/
void disableRelay()
{
  digitalWrite(relayPin, LOW);
  lightStateVal = false;
}

/*************************************************************
 * Function: overrideRelay ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/13/2020
 * Description: function handles the override button logic.
 * Input parameters: none
 * Returns: void
 *************************************************************/
void overrideRelay()
{
  delayMicroseconds(500000); //delayMicroseconds is the only delay function that will work during interrupts according to the interrupt docs
  if(overrideStateVal)
  {
    digitalWrite(relayPin, LOW);
    overrideStateVal = false;
    checkSchedule(); //since we are now disabling the override we need to put the relay back onto its sunrise sunset schedule
  }
  else
  {
    digitalWrite(relayPin, HIGH);
    overrideStateVal = true;
    lightStateVal = true;
  }
}

/*************************************************************
 * Function: lightState ()                    
 * Date Created: 6/13/2020
 * Date Last Modified: 6/13/2020
 * Description: function returns a String representation of the 
 *              current state of the light relay.
 * Input parameters: none
 * Returns: void
 *************************************************************/
String lightState()
{
  if(lightStateVal)
    return "Light State: ON";
  else
    return "Light State: OFF";
}
