#include "Energia.h" 
#include "inc/hw_memmap.h" 
#include "inc/hw_types.h" 
#include "inc/hw_ints.h" 
#include "driverlib/Debug.h" 
#include "driverlib/interrupt.h" 
#include "driverlib/sysctl.h" 
#include "driverlib/adc.h"
#include "driverlib/rom.h"
#include "driverlib/timer.h" //See more at: http://patolin.com/blog/2014/06/29/stellaris-launchpad-energia-pt-2-timers/#sthash.VheM8bk6.dpuf
#include "HX711.h"           //Requires HX711 Library from: https://github.com/bogde/HX711
#include <EEPROM.h>
#include <Servo.h> 

// Configuration options
#define UARTBAUD 460800   // UART Baud rate
#define OVERSAMPLING 32   // Analog oversampling
#define MINTHROTTLE 1000  // Low end of ESC calibrated range
#define MAXTHROTTLE 2000  // High end of ESC calibrated range
#define MINCOMMAND 980    // Value sent to ESC when test isn't running.
#define SENSORRATE 500    // Refresh rate in HZ of load cell and analog read timer.

//IO pins
int ESCPin=36;

// Scale Pins
// HX711.DOUT	- pin #9
// HX711.PD_SCK	- pin #8
HX711 scale(9, 8);	

// RPM pins
volatile int rpmCount = 0;
volatile int rpmCount2 = 0;

// analog value variables
unsigned long ulADC0Value[8];
volatile int voltageValue = 0;
volatile int currentValue = 0;
volatile int thrust = 0;

// Misc Variables
Servo ESC;
char character;
String input;
String calibrationWeight;
float calibration;
int calibrationmass;
float throttle;
unsigned long startTime;
unsigned long loopStart;
int lastRead = 1;
boolean isTestRunning = false;
int currentMicros = 0;

union f_bytes
{
  byte b[4];
  float fval;
}u;

float readFloatFromEEPROM(int address)
{
  for(char i=0;i<4;i++)
  {
    u.b[i]=EEPROM.read(address+i);
  }
  //if the read float is nan, clear the eeprom
  if(u.fval!=u.fval)
  {
    EEPROM.write(address,0);
    EEPROM.write(address+1,0);
    EEPROM.write(address+2,0);
    EEPROM.write(address+3,0);
  }
  return u.fval;
}
void saveFloatToEEPROM(float toSave,int address)
{
  u.fval=toSave;
  for(char i=0;i<4;i++)
  {
    EEPROM.write(address+i,u.b[i]);
  }
  
}
// the setup routine runs once when you press reset:
void setup() {
  
  // initialize serial communication:
  Serial.begin(UARTBAUD);
  
  //attach ESC servo output
  ESC.attach(ESCPin,MINTHROTTLE,MAXTHROTTLE);
  ESC.writeMicroseconds(MINCOMMAND);  // Ensure throttle is at 0
  
  //attach Interupt for RPM sensor
  pinMode(PUSH1, INPUT_PULLUP);
  attachInterrupt(PUSH1, countRpms, FALLING);
  pinMode(PUSH2, INPUT_PULLUP);
  attachInterrupt(PUSH2, countRpms2, FALLING);
  
  calibration=readFloatFromEEPROM(4);  
  
  scale.set_scale(-430);  //Eventually set this via EEPROM
  scale.tare();	//Reset the scale to 0
  
  initTimer0(SENSORRATE);     //Start timer for load cell and analog reads
}

void countRpms () {
  rpmCount++;
}

void countRpms2 () {
  rpmCount2++;
}

// the loop routine runs over and over again forever:
void loop() {
  
  isTestRunning = false;  //Stop reads from load cell
  
  ESC.writeMicroseconds(MINCOMMAND);  //Double check throttle is at 0
  
  // Prompt for input and read it
  Serial.println("Type Tare, Calibrate, Start, or Free");
  input="";
  while(!Serial.available());
  while(Serial.available()) {
      character = Serial.read();
      input.concat(character);
      delay(1);
  }
  Serial.print("Input: ");
  Serial.println(input);
  
  //Check input
  if(input.indexOf("Tare") >= 0) {
    input="";
    Serial.println("Taring");
    scale.tare();
  }
  if(input.indexOf("Calibrate") >= 0) {
    input="";
    Serial.println("You must Tare before calibrating.  To exit calibration without saving a new value, type Exit. Otherwise enter the calibration mass in grams, without units");
    while(!Serial.available());
    while(Serial.available()) {
        character = Serial.read();
        input.concat(character);
        delay(1);
    }
    if(input.indexOf("Exit") < 0) {
      calibrationmass=input.toInt();
      Serial.print("Calibration mass: ");
      Serial.println(calibrationmass);
      calibration=(float)calibrationmass/(scale.get_units());
      scale.set_scale(calibration);
      saveFloatToEEPROM(calibration,4);
      Serial.print("New measured mass: ");
      Serial.println(scale.get_units());
    }
  }
  
  if(input.indexOf("Start") >= 0) {
    input="";
    Serial.println("Begining automated test, press any key to exit");
    delay(2000);
    Serial.println("Thrust(g),Voltage,Current,eRPMs,mRPMs,Throttle(uS),Time(uS)");
    startTime=micros();
    isTestRunning = true;
    while(!Serial.available() && (micros()-startTime)<22000000) {  
      loopStart = micros();
      if((micros()-startTime)<2000000)
        throttle=0.25;
      else if((micros()-startTime)<4000000)
        throttle=0.1;
      else if((micros()-startTime)<6000000)
        throttle=0.50;
      else if((micros()-startTime)<8000000)
        throttle=0.1;
      else if((micros()-startTime)<10000000)
        throttle=1.0;
      else if((micros()-startTime)<12000000)
        throttle=0.0;
      else if((micros()-startTime)<18000000)
        throttle=(float)(micros()-startTime-12000000)/6000000.0;
      else if((micros()-startTime)<20000000)
        throttle=1;
      else if((micros()-startTime)<=22000000)
        throttle=0.1;
      else 
        throttle=0.0;
      int escMicros = (throttle*1000) + 1000;
      ESC.writeMicroseconds(escMicros);
      Serial.print(thrust);
      Serial.print(",");
      Serial.print(voltageValue);
      Serial.print(",");
      Serial.print(currentValue);
      Serial.print(",");
      Serial.print(rpmCount);
      Serial.print(",");
      Serial.print(rpmCount2);
      Serial.print(","); 
      Serial.print(escMicros);
      Serial.print(",");
      int diffMicros = micros() - currentMicros;
      currentMicros = micros();
      Serial.print(diffMicros);
      Serial.print(",");
      Serial.println(micros()-startTime);
   
      // Delay here adjusts the sample rate for the RPM sensors, as they are updated asynchronously via the interrupts.
      // Note that cycle times are limited by serial baud rates as well. You can change delay here to just higher than
      // the serial delay to get more stable cycle times.
      // 115200 = ~2.4ms cycle
      // 230400 = ~1.2ms cycle
      // 460800 = ~600us cycle
      
      unsigned int loopTime = micros() - loopStart;
      int thisDelay;
      switch(UARTBAUD) {
        case 115200:
          thisDelay = (2498 - loopTime);
          break;
          
        case 230400:
          thisDelay = (1998 - loopTime);
          break;
          
        case 460800:
          thisDelay = (1098 - loopTime);
          break;
          
      }  
      if (thisDelay < 0) { thisDelay = 0; }
      delayMicroseconds(thisDelay);
    }
    while(Serial.available()) {
        character = Serial.read();
        delay(1);
    }
  } 
  delay(1);
}

void initTimer0 (unsigned Hz) { 
  SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
  ADCHardwareOversampleConfigure(ADC0_BASE, OVERSAMPLING);
  ADCSequenceDisable(ADC0_BASE, 0);
  
  ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH0);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH0);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH0);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_CH0);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 4, ADC_CTL_CH1);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 5, ADC_CTL_CH1);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 6, ADC_CTL_CH1);
  ADCSequenceStepConfigure(ADC0_BASE, 0, 7, ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);
  ADCSequenceEnable(ADC0_BASE, 0);
  ADCIntClear(ADC0_BASE, 0);
  
  SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0); 
  //TimerConfigure(TIMER0_BASE, TIMER_CFG_32_BIT_PER); 
  TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC); 
  unsigned long ulPeriod = (SysCtlClockGet () / Hz);
  TimerLoadSet(TIMER0_BASE, TIMER_A, ulPeriod -1); 
  IntEnable(INT_TIMER0A); 
  TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT); 
  TimerIntRegister(TIMER0_BASE, TIMER_A, Timer0IntHandler); 
  TimerEnable(TIMER0_BASE, TIMER_A); 
}

void Timer0IntHandler() {
  TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
  if(isTestRunning) {
    if(ADCIntStatus(ADC0_BASE, 0, false)){
      ADCIntClear(ADC0_BASE, 0);
      ADCSequenceDataGet(ADC0_BASE, 0, ulADC0Value);
      voltageValue = (ulADC0Value[0] + ulADC0Value[1] + ulADC0Value[2] + ulADC0Value[3] + 2)/4;
      currentValue = (ulADC0Value[4] + ulADC0Value[5] + ulADC0Value[6] + ulADC0Value[7] + 2)/4;
    } else {
      ADCIntClear(ADC0_BASE, 0);
      ADCProcessorTrigger(ADC0_BASE, 0);
    }
    if(scale.is_ready()){
      thrust = scale.get_units();
    }
  } 

}

void initTimer1 (unsigned Hz) { 
 
  SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
  //TimerConfigure(TIMER1_BASE, TIMER_CFG_32_BIT_PER); 
  TimerConfigure(TIMER1_BASE, TIMER_CFG_PERIODIC); 
  unsigned long ulPeriod = (SysCtlClockGet () / Hz);
  TimerLoadSet(TIMER1_BASE, TIMER_A, ulPeriod -1); 
  IntEnable(INT_TIMER1A); 
  TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT); 
  TimerIntRegister(TIMER1_BASE, TIMER_A, Timer1IntHandler); 
  TimerEnable(TIMER1_BASE, TIMER_A); 
}

void Timer1IntHandler() {
   TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
}
