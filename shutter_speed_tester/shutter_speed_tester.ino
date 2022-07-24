#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define LIGHT_PIN A7

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)
// https://gist.github.com/SimenZhor/04b0f4aa3f3fd5e52bfec7202dcac961

volatile boolean risingEdge;
volatile boolean fallingEdgeFlag;
volatile unsigned long overflowCount;
volatile unsigned long pulseStartTime;
volatile unsigned long pulseFinishTime;
float pulseWidth; //us

ISR(TIMER1_OVF_vect){
  // timer overflows (every 65536 counts)
  overflowCount++;
}

ISR(TIMER1_CAPT_vect){
  noInterrupts();
  unsigned int timer1CounterValue = ICR1;
  unsigned long overflowCopy = overflowCount;
  // Check if we have just missed an overflow
  if ((TIFR1 & bit (TOV1)) && timer1CounterValue < 0x7FFF)
    overflowCopy++;
    
  if(risingEdge){
    //Store counting value, reconfigure to trigger on falling edge
    pulseStartTime = (overflowCopy << 16) + timer1CounterValue;
    risingEdge = false;
    // Change to trigger on falling edge
    // ICES1 = 0 means Falling Edge Trigger
    TCCR1B &=  ~bit(ICES1);  // Set ICES1 low, keep all other bits in TCCR1B
    interrupts(); // Re-enable interrupts, so we can catch the falling edge
    return; 
  }else{
    //Read counting value, set flag that values can be calculated, reconfigure to trigger on rising edge
    pulseFinishTime = (overflowCopy << 16) + timer1CounterValue;
    fallingEdgeFlag = true;
    TIMSK1 = 0;    // pause interrupts until pulse width has been calculated
  }
}
 
void initTC(){
  noInterrupts ();  // protected code
  fallingEdgeFlag = false;  // re-arming for new pulse
  risingEdge = true; // This function sets rising edge as the event trigger
  // reset Timer 1 configuration registers
  TCCR1A = 0;
  TCCR1B = 0;
 
  TIFR1 = bit (ICF1) | bit (TOV1);  // clear flags so we don't get a bogus interrupt
  TCNT1 = 0;          // Reset counter
  overflowCount = 0;  // Reset overflow counter
 
  // Configure Timer 1 
  // TOIE1 = Overflow IRQ, ICIE1 = Event IRQ
  TIMSK1 = bit (TOIE1) | bit (ICIE1);   // interrupt on Timer 1 overflow and input capture
  // Select clock, select event trigger and apply filter (filter causes 4 clock cycles on delay but does not alter the pulsewidth measurement)
  // CS10 = no prescaler (TC_CLK = F_CLK), ICES1 = Rising Edge Trigger, ICNC1 = Input Capture Noise Canceller
  TCCR1B =  bit (CS10) | bit (ICES1) | bit(ICNC1); // Set CS10, ICES1 and ICNC1 - clear all other bits in TCCR1B
  interrupts ();
}

void calculatePulseWidth(){
  unsigned long numCounts = pulseFinishTime - pulseStartTime;
  // Period time = 1/F_CPU (= 62.5 ns at 16 MHz)
  pulseWidth = float(numCounts) / (F_CPU*0.000001); // Pulse width = Period time * Num counts
}

void reArmTC(){
  //Function only added for clarity in naming
  initTC(); //re-arm for next pulse
}

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//int value = 0;
//void setup() {
//  // put your setup code here, to run once:
//  Serial.begin(115200);
//  pinMode(LIGHT_PIN, INPUT);
//}
//
//void loop() {
//  // put your main code here, to run repeatedly:
//  value = analogRead(LIGHT_PIN);
//  Serial.println(value);
//  delay(100);
//}
void setup(){
  Serial.begin(115200);       
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  pulseWidth = 0; //us
  // set up for interrupts
  initTC();  
}

void loop(){
  if(fallingEdgeFlag){
    calculatePulseWidth();
    Serial.print ("PulseWidth: ");
    Serial.print (pulseWidth);
    Serial.println (" µs. ");
    reArmTC();
  } 
}
