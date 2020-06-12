 /*
  LiquidCrystal Library - Hello World

 Demonstrates the use a 16x2 LCD display.  The LiquidCrystal
 library works with all LCD displays that are compatible with the
 Hitachi HD44780 driver. There are many of them out there, and you
 can usually tell them by the 16-pin interface.

 This sketch prints "Hello World!" to the LCD
 and shows the time.

  The circuit:
 * LCD RS pin to digital pin 12
 * LCD Enable pin to digital pin 11
 * LCD D4 pin to digital pin 5
 * LCD D5 pin to digital pin 4
 * LCD D6 pin to digital pin 3
 * LCD D7 pin to digital pin 2
 * LCD R/W pin to ground
 * LCD VSS pin to ground
 * LCD VCC pin to 5V
 * 10K resistor:
 * ends to +5V and ground
 * wiper to LCD VO pin (pin 3)

 Library originally added 18 Apr 2008
 by David A. Mellis
 library modified 5 Jul 2009
 by Limor Fried (http://www.ladyada.net)
 example added 9 Jul 2009
 by Tom Igoe
 modified 22 Nov 2010
 by Tom Igoe

 This example code is in the public domain.

 http://www.arduino.cc/en/Tutorial/LiquidCrystal
 */

// include the library code:
#include <LiquidCrystal.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include "sensitiveData.h";

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

char *ssid = NETWORK_SSID;
char *pass = NETWORK_PASS;
int status = WL_IDLE_STATUS; //wifi radio's status
int retryAuth = 0; //counter for how many times authentication has been attempted with wifi network

unsigned int localPort = 2390;
IPAddress timeServer(208, 67, 75, 242); //NTP server ip
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP Udp;

int relay = 8; //pin number for the relay trigger
bool lightState;
bool overrideState = false; //override is not activated by default
struct localTime
{
  String printTime;
  int militaryHour;
  int minute;
};

void setup() 
{
  // set up the LCD's number of columns and rows:
  lc d.begin(16, 2);
  
  // set up network connection
  setupNetwork();

  Udp.begin(localPort);

  pinMode(relay, OUTPUT); //setting relay trigger to output
}

void loop() 
{
  updateDisplay();
}

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

void updateDisplay()
{
  localTime myTime;

  myTime = getLocalTime();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(lightState());
  lcd.setCursor(0, 1);
  
  if(overrideState) //if the user presses the override button to enable the outlets regardless of the timer, display an override on message
    lcd.print("OVERRIDE ON");
  else
    lcd.print(myTime.printTime);
    
  delay(5000); //update display every ten seconds
}

// send an NTP request to the time server at the given address
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
  
  String timeShort; //working string for combining everything
  localTime myTime; //time struct that will be returned containing all the results of this function call

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
    militaryHour = ((epoch  % 86400L) / 3600) - 7; //86400 seconds in a day, modulus of epoch with that leaves hours in seconds, divide by how many seconds in an hour to get the gmt hour in military time, minus 7 to get it from gmt to pst
    hour = convertTimeFormat(militaryHour); //convert to am/pm
    minute = (epoch  % 3600) / 60; //remove hours with modulus and leave just minutes in seconds, divide by 60 to get the amount of minutes in the hour
    dayOrNight = getAMPM(militaryHour); //use military time to get am/pm string

    //create hour and minute string with am/pm for printing. We will use the String constructor to concatenate all the different ints and strings into one string
    timeShort = String(hour);
    timeShort = String(timeShort + ":");
    
    if (((epoch % 3600) / 60) < 10) // In the first 10 minutes of each hour, we'll want a leading '0'
    {
      timeShort = String(timeShort + "0");
    }
    
    timeShort = String(timeShort + minute);
    timeShort = String(timeShort + dayOrNight);

    myTime.printTime = timeShort;
    myTime.militaryHour = militaryHour;
    myTime.minute = minute;
  }

  return myTime;
}

int convertTimeFormat(int militaryHour)
{
  int hour;
  
  if(militaryHour < 0) //the gmt to pst conversion will cause a negative number from 5:00pm to 11:59pm so we need to convert those into positives
  {
    militaryHour += 23; //our very own int overflow
  }
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

void checkSchedule()
{
  if(overrideState == false) //check schedules functionality will only occur if the override is not enabled
  {
    
  }
}

void enableRelay()
{
  digitalWrite(relay, HIGH);
  lightState = true;
}

void disableRelay()
{
  digitalWrite(relay, LOW);
  lightState = false;
}

void overrideRelay()
{
  if(overrideState)
  {
    digitalWrite(relay, LOW)
    overrideState = false;
    checkSchedule(); //since we are now disabling the override we need to put the relay back onto its sunrise sunset schedule
  }
  else
  {
    digitalWrite(relay, HIGH);
    overrideState = true;
  }
}

String lightState()
{
  if(lightState)
    return "Light State: ON";
  else
    return "Light State: OFF";
}
