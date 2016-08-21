//CCD bus datalogger
//John Grandle
//Teensy 3.2, intersil CDP68HC68S1, 1mhz hardware crystal. Idle is on pin2, control on pin 5. 1&2 is UART 1. 3&4 reserved for CAN (MCP2551)
//
//process_data() is from www.kolumbus.fi/~ks9292/CCD_bus/ccd_display.htm
//CyclicRedundancyCheck() copied from http://thespeedfreaks.net/showthread.php?12585-CCDuino-A-ZJ-Databus-Project/
//digitalClockDisplay_WriteToSerial() from https://forum.pjrc.com/threads/25920-Teensy-3-1-ADC-Measurement-SD-logging-problem-with-Sample-Rate
//RTC details from teensy clock example
//watchdog from teensy forums documentation

#include <Time.h>
#include <TimeLib.h>

//SD Card
#include <SPI.h>
#include <SD.h>

const int numOfBytes = 10; // receive buffer size
const bool logOnly = true;
bool debug = false;

int idlePin = 2;      //idle on the CCD chip
const int chipSelect = 8; //SD Card shield


byte ccd_buff[numOfBytes]; /* CCD receive buffer / MAX Bytes are in numOfBytes */
unsigned int ccd_buff_ptr; /* CCD receive buffer pointer */
volatile byte IdleOnOffFlag = 0; //variable for the idle pin. Must be volatile due to being part of interrupt


void setup() {
  pinMode(ledPin, OUTPUT);            //prep builtin led pin
  digitalWrite(ledPin, LOW);          //Set LED to low once. May be unnecessary but doesn't hurt anything.
  Serial.begin(38400); //for serial IC and Serial debug print

  //  Datalogger setup
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    digitalWrite(ledPin, HIGH);
    // don't do anything more:
    return;
  }

  setSyncProvider(getTeensy3Time);
  SetTime();
  
  pinMode(idlePin, INPUT);            //set idle pin for input
  Serial1.begin(7812.5); //for serial IC and Serial debug print

  //watch for the end of a packet
  attachInterrupt(idlePin, endofstring, RISING);  // high = BUSY , low = IDLE
  //watchdog rebooter
  WDOG_UNLOCK = WDOG_UNLOCK_SEQ1;
  WDOG_UNLOCK = WDOG_UNLOCK_SEQ2;
  delayMicroseconds(1); // Need to wait a bit..
  WDOG_STCTRLH = 0x0001; // Enable WDG
  WDOG_TOVALL = 200; // The next 2 lines sets the time-out value. This is the value that the watchdog timer compare itself to.
  WDOG_TOVALH = 0;
  WDOG_PRESC = 0; // This sets prescale clock so that the watchdog timer ticks at 1kHZ instead of the default 1kHZ/4 = 200 HZ

}


void endofstring() {
  IdleOnOffFlag = 1; //Sets flag high when idle pin goes high
}


/* ###################### (CRC) function #######################*/
uint8_t CyclicRedundancyCheck()
{
  //Serial.println("CRC Function");
  // Cyclic Redundancy Check (CRC)
  // The checksum byte is calulated by summing all the ID and Data Bytes.
  // 256 is then subtracted from that sum.
  // The subtraction will end once the checksum falls within 0-255 decim
  if (ccd_buff_ptr >= 1) { //do not subtract if 0 ( BUG FIX )
    ccd_buff_ptr = ccd_buff_ptr - 1; //  subtract 1 from byte count [array indices usually start at 0]
  }
  uint16_t _CRC = 0; //was uint8_t _CRC = 0; bug fix
  for (uint16_t CRCptr = 0; CRCptr < ccd_buff_ptr; CRCptr ++) { // uint16_t 0 to 65,535 ~ int16_t -32,768 to 32,767
    _CRC = _CRC + ccd_buff[CRCptr];
  }
  while (_CRC > 255) {
    _CRC = _CRC - 256;
  }
  uint8_t x = _CRC;
  return x;
}


void loop() {
  // put your main code here, to run repeatedly:


  while (Serial1.available()) {
    ccd_buff[ccd_buff_ptr] = Serial1.read(); // read & store character
    ccd_buff_ptr++;                   // increment the pointer to the next byte
  }


  if (IdleOnOffFlag == 1) { // check the CDP68HC68S1 IDLE pin interrupt flag, change from Low to High.
    uint8_t CRC = 0;
    CRC = CyclicRedundancyCheck(); // Go get CRC.
    if (ccd_buff[ccd_buff_ptr] == CRC && ccd_buff_ptr != 0) {
      if(debug){
        Serial.println(F("DEBUG PRINT:"));
        for (unsigned long y = 0; y < ccd_buff_ptr + 1; y ++) {
          Serial.print(ccd_buff[y], HEX); // DEBUG PRINT
          Serial.print(" ");
        }
        Serial.println();
    
        ccd_buff_ptr = 0; // RESET buffer pointer to byte 0 for data to be overwritten
      }else{
      File dataFile = SD.open("datalog.csv", FILE_WRITE);
      // if the file is available, write to it:
      if (dataFile) {
        //print one byte at a time, adding a comma
        dataFile.print(now()); //date time stamp
        dataFile.print(millis());
        dataFile.print(", ");
        for (unsigned long y = 0; y < ccd_buff_ptr; y ++) {
          dataFile.print(ccd_buff[y], HEX); // hex value [y]
          dataFile.print(", "); //CSV firnat
        }
        //print last byte, adding CRLF to the end
        dataFile.println(ccd_buff[ccd_buff_ptr], HEX); 
        dataFile.close();
      }
      // if the file isn't open, pop up an error:
      else {
        Serial.println("error opening datalog.csv");
      }
      //send the junk to serial if we arent log-only
      if (!logOnly){
        process_data();
      }
      //clear ccd_buffer
      for (unsigned long z=0; z < numOfBytes; z++){
        ccd_buff[z]=0;
      }
      ccd_buff_ptr = 0;
      }
      
    }
  }else{
    if (debug){
      //digitalClockDisplay_WriteToSerial();
      Serial.print(now());
      Serial.print(" ");
      Serial.println(millis());
      }
    }
  kickDog(); //if you dont it bites
}

void kickDog(){
  noInterrupts();
  WDOG_REFRESH = 0xA602;
  WDOG_REFRESH = 0xB480;
  interrupts();
if(debug){Serial.println(F("dog kicked"));}
// if you don't refresh the watchdog timer before it runs out, the system will be rebooted

delay(1); // the smallest delay needed between each refresh is 1ms. anything faster and it will also reboot.
}

/* Process CCD data and print to LCD or Serial */
/* CCD display */
void process_data( void )
{
  int temp;
  //  lcdclr();     // clear first
  // LEDB = 1;
  //digitalWrite(ledPin, HIGH);   // set the LED on


  switch (ccd_buff[0])          // decide what to do from first byte / ID BYTE
  {
  case 0xD9:                // time
    //set_cur_lcd( LINE_1 );        // Display elapsed time
    Serial.print("Time ");
    Serial.print(ccd_buff[1], DEC);
    Serial.print(":");
    Serial.print(ccd_buff[2]);
    Serial.print(":");
    Serial.print(ccd_buff[3]);
    Serial.println("  ");
    break;


  case 0xD4:                // Volts
    //set_cur_lcd( LINE_1 + 15);    // battery voltage
    Serial.print("V");            // voltage = number / 16,19
    Serial.print((int)ccd_buff[1] * 62 / 100, 1); // system
    Serial.println((int)ccd_buff[2] * 62 / 100, 1); // target
    break;


  case 0x8C:                // Temperatures
    //set_cur_lcd( LINE_1 + 27);    // engine, battery
    Serial.print("Temp ");
    temp = (int)ccd_buff[1] - 128;
    Serial.print( (char)temp );        // engine
    Serial.print("degree");// 0xB2);            // degree sign
    temp = (int)ccd_buff[2] - 128;    // battery
    Serial.print( (char)temp );
    Serial.println("degree");//putch(0xB2);
    break;




  case 0x02:                // Trans bits
    //set_cur_lcd( LINE_2);        //
    Serial.print("Tra ");
    Serial.print(ccd_buff[1], HEX);
    Serial.print(" ");
    break;




  case 0x05:                // Door bits
    //set_cur_lcd( LINE_2 + 8);    //
    Serial.print("Drs ");
    Serial.print(ccd_buff[1], HEX);
    Serial.print(" ");
    Serial.print(ccd_buff[2], HEX);
    Serial.println(" ");
    break;


  case 0xEC:                // Status 11 bits
    //set_cur_lcd( LINE_2 + 18);    //
    Serial.print("BYTE Count:");
//    Serial.println(count);
    Serial.print(" Status 11 bits ");
    Serial.print(ccd_buff[1], BIN); //was HEX
    Serial.print(" ");
    Serial.print(ccd_buff[2], BIN);
    Serial.println(" ");
    break;


  case 0xA4:                // Status 13 bits
    //set_cur_lcd( LINE_2 + 29);    //
    Serial.print("Status 13 bits "); // was S13
    Serial.print(ccd_buff[1], BIN);  // was HEX
    Serial.print(" ");
    Serial.println(ccd_buff[2], BIN);
    break;




  case 0xDC:        // do gear lock state
    //lcdpos( LINE_1 );


    Serial.print("Gear ");


    if (ccd_buff[1] & 0x20)   // evaluate gear
      Serial.print("Fourth ");
    if (ccd_buff[1] & 0x10)
      Serial.print("Third  ");
    if (ccd_buff[1] & 0x08)
      Serial.print("Second ");
    if (ccd_buff[1] & 0x04)
      Serial.print("First  ");
    if (ccd_buff[1] & 0x02)
      Serial.print("Reverse");
    if (ccd_buff[1] & 0x01)
      Serial.print("Neutral");


    Serial.println(" ");


    switch ((ccd_buff[1] >> 6) & 3)   // evaluate lock state
    {
    case 2:
      Serial.println("Full ");
      break;


    case 1:
      Serial.println("Part ");
      break;


    case 0:
    default:
      Serial.println("default     "); // EDIT
    }


    Serial.print (ccd_buff[1], HEX);     // Show also raw data
    Serial.println(" ");
    break;


  case 0x24:        // do  #24 Speed
    //lcdpos( LINE_2 );   // Display = CCDvalue / 16
    Serial.print("Spd");
    Serial.println( ((((int)ccd_buff[2] + 256 * (int)ccd_buff[1])) / 16), 1);
    break;


  case 0x89:        // do  #89 Fuel efficiency
    //lcdpos( LINE_2 + 10);   // test
    Serial.print("Fu");
    Serial.print( (int)ccd_buff[2], 0);  // l/100km
    Serial.print(" ");
    Serial.println(ccd_buff[1]);    // mpg
    break;


  case 53: //0X35 / Ignition Switch - Off/Unlocked
    Serial.print("Ignition Switch - Off/Unlocked ");
    Serial.print((int)ccd_buff[1]);   //
    Serial.print(" ");
    Serial.print((int)ccd_buff[2]);
    Serial.print(" ");
    Serial.print((int)ccd_buff[3]);
    Serial.print(" ");
    Serial.println((int)ccd_buff[4]);//
    break;




  case 0xE4:        // do RPM & MAP
    //lcdpos( LINE_3 );   // Display = RPM = CCD * 8
    Serial.print("RPM ");
    Serial.print((int)(ccd_buff[1]) * 32);// 800 RPM @ idle / 1999 Dodge ram 2500 cummins diesel,5spd m.
    Serial.print("  MAP");
    Serial.println((int)ccd_buff[2]);// MAP/  it doesn't seem to work
    break;




  case 0x42:        // do #42 TPS Cruise ctrl set?
    //lcdpos( LINE_4 );   // TPS value 0 - 0x99 (153) ??
    Serial.print("TPS  ");
    Serial.print(ccd_buff[1], HEX);
    Serial.print("   Cruise KPH");
    temp = 16 * ccd_buff[2];  // convert to kph 1,6 * mph
    Serial.print(temp / 10, 0);
    Serial.print("   Cruise MPH");
    Serial.println(ccd_buff[2] / 10, 0);
    break;




  default:
    Serial.print(ccd_buff[0], HEX);
    Serial.print("  ");
    Serial.print(ccd_buff[1], HEX);
    Serial.print("  ");
    Serial.print(ccd_buff[2], HEX);
    Serial.print("  ");
    Serial.print(ccd_buff[3], HEX);
    Serial.print("  ");
    Serial.print(ccd_buff[4], HEX);
    Serial.print("  ");
    Serial.print(ccd_buff[5], HEX);
    Serial.print("  ");
    Serial.println(ccd_buff[6], HEX);
    //Serial.println("  ");


    Serial.print(ccd_buff[0]);
    Serial.print(" ");
    Serial.print(ccd_buff[1]);
    Serial.print(" ");
    Serial.print(ccd_buff[2]);
    Serial.print(" ");
    Serial.print(ccd_buff[3]);
    Serial.print(" ");
    Serial.print(ccd_buff[4]);
    Serial.print(" ");
    Serial.print(ccd_buff[5]);
    Serial.print(" ");
    Serial.print(ccd_buff[6]);
    Serial.println(" ");
    break;
  }
}

//Setting Time
void SetTime(){
  //Set Time
  if (timeStatus()!= timeSet) {
    Serial.println("Unable to sync with the RTC");
  } else {
    Serial.println("RTC has set the system time");
  }
}



//Initializing RTC
void InitializeClock() {
  if (Serial.available()) {
     time_t t = processSyncMessage();
     if (t != 0) {
     Teensy3Clock.set(t); // set the RTC
     setTime(t);
     }
  }
}  


//Set RTC Time
time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

/*  code to process time sync messages from the serial port   */
#define TIME_HEADER  "T"   // Header tag for serial time sync message

unsigned long processSyncMessage() {
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013 

  if(Serial.find(TIME_HEADER)) {
     pctime = Serial.parseInt();
     return pctime;
     if( pctime < DEFAULT_TIME) { // check the value is a valid time (greater than Jan 1 2013)
       pctime = 0L; // return 0 to indicate that the time is not valid
     }
  }
  return pctime;
}
// END Set RTC Time

//Print digits for Serial
void printDigitsSerial(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

//Digital Clock for Serial
void digitalClockDisplay_WriteToSerial() {
  // digital clock display of the time
  Serial.print(F("No data on "));
  Serial.print(day());
  Serial.print("-");
  Serial.print(month());
  Serial.print("-");
  Serial.print(year()); 
  Serial.print("\t");
  Serial.print(hour());
  printDigitsSerial(minute());
  printDigitsSerial(second());
  Serial.print(",");
  Serial.print(millis());
  Serial.println("\t"); 
}
