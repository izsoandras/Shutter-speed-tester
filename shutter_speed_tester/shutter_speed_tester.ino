#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define LIGHT_PIN A7
#define MEAS_BUFF_SIZE 129
#define MAX_LIGHT 1024
#define MAX_PX 32

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
float pulseWidth = 0; //us

bool OLEDinited = true;

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

int countWholeDigits(float number){
  int cnt = 1;
  while(number >= 10){
    cnt++;
    number /= 10;
  }
  return cnt;
}

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int value = 0;
int values[MEAS_BUFF_SIZE];
int start_guard = 0;

char buf1[20] = "";

void displayMeasured(float t_us){
  display.setCursor(0,0);

  float t = t_us;
  uint8_t cnt = 0;
  while(t >= 1000 && cnt < 2){
    t /= 1000;
    cnt++;
  }
  
  int dnum = countWholeDigits(t);
  dtostrf(t, 8, 10-dnum-3, buf1);
  display.print(buf1);
  float sec_den;
  switch(cnt){
    case 0:
      display.println("us");
      sec_den = 1/(t/1000000);
      break;
    case 1:
      display.println("ms");
      sec_den = 1/(t/1000);
      break;
    case 2:
      display.println("s");
      sec_den = 1/t;
      break;
  }

  dnum = countWholeDigits(sec_den);
  dtostrf(sec_den, 7, 10-dnum-4, buf1);
  display.print("1/");
  display.print(buf1);
  display.print("s");
}

int lightToPx(int light, int max_px, int max_light){
  return (int)((float)light/max_light * max_px);
}

int mod(int x, int y){
  return x < 0 ? ((x+1)%y)+y - 1 : x%y;
}

void updateCurve(){
  int x, y;
  for(int i = 0; i < SCREEN_WIDTH; i++){
    x = SCREEN_WIDTH - 1 - i;
    y = SCREEN_HEIGHT - 1 - lightToPx(values[mod((start_guard - i -1), MEAS_BUFF_SIZE)], MAX_PX, MAX_LIGHT);
    display.drawPixel(x, y, SSD1306_BLACK);
    y = SCREEN_HEIGHT - 1 - lightToPx(values[mod((start_guard - i), MEAS_BUFF_SIZE)], MAX_PX, MAX_LIGHT);
    display.drawPixel(x, y, SSD1306_WHITE);
  }
}


unsigned int cmp_lvl_px = lightToPx(500, MAX_PX, MAX_LIGHT);
void drawCompareLevel(){
  uint8_t dash_length = 12;
  int y = SCREEN_HEIGHT - 1 - cmp_lvl_px;
  for(uint8_t x = 0; x < SCREEN_WIDTH; x++){
    if(x % (dash_length) < 4)
      display.drawPixel(x, y, SSD1306_WHITE);
    else
      display.drawPixel(x, y, SSD1306_BLACK);
  }
}

void setup(){
  Serial.begin(115200);    
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    OLEDinited = false;
  }
  
  // set up for interrupts
  initTC();  

  if(OLEDinited){
    display.clearDisplay();
    
    display.setTextSize(2);             // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    drawCompareLevel();
    display.display();
  }
}

void loop(){
  if(fallingEdgeFlag){
    calculatePulseWidth();
    reArmTC();
    Serial.print ("PulseWidth: ");
    Serial.print (pulseWidth);
    Serial.println (" µs. ");
    display.clearDisplay();
    displayMeasured(pulseWidth);
  } 
  
  value = analogRead(LIGHT_PIN);
  start_guard++;
  values[start_guard % MEAS_BUFF_SIZE] = value;
  
  drawCompareLevel();
  updateCurve();
  display.display();
  Serial.println(value);
  delay(50);
}
