//////////////////////////////////////////////////////////////////////
// Arduino DC Throttle
// (c) John Purbrick 2021 (j_purbrick@yahoo.com)
//////////////////////////////////////////////////////////////////////
/*
  Copyright (C) John Purbrick 2021 (j_purbrick@yahoo.com)

  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

   License summary:
    You are free to:
      Share, copy and redistribute the material in any medium or format
      Adapt, remix, transform, and build upon the material

    The licensor cannot revoke these freedoms as long as you follow the license terms.

    Attribution : You must give appropriate credit, provide a link to the license,
                  and indicate if changes were made. You may do so in any reasonable manner,
                  but not in any way that suggests the licensor endorses you or your use.

    NonCommercial : You may not use the material for commercial purposes. **(see note below)

    ShareAlike : If you remix, transform, or build upon the material, you must distribute
                 your contributions under the same license as the original.

    No additional restrictions : You may not apply legal terms or technological measures that
                                 legally restrict others from doing anything the license permits.

   ** For commercial use, please contact the original copyright holder(s) to agree licensing terms

    This software is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE

*/

typedef unsigned char byte;

#define FAST 0

#define pwmPin 13   // D10, B.5 Pin 16
#define blankPin 12 // D9, B.4 Pin 15
#define relayPin 11 // D8 B.3
#define dir_input 10 // D7 Pin 13 B.2
#define tPin  9
#define framePin 4  // 60Hz frame sync
#define led_bit 5
#define direction_bit 3
#define t_Bit  2
#define RUN_ADC 0b11000111
#define THROTTLE_NULL 255

byte lcount;
byte num_in;
byte direction_relay;
byte pause;
byte adc_high_res;
char inpah[200];

unsigned int pot_setting;
unsigned int volts_out;

union {
  unsigned int rs_w;
  byte rs_b[2];
} raw_speed;

int error;

byte inp;
byte throttle_out;
byte track_occ;
byte overload;

byte use_pot;
byte sixty_Hz;
byte use_algorithm;

//byte the_packet[15];

//void throttle_calculate(void);

void setup()
{
  pinMode(pwmPin, OUTPUT);
  pinMode(blankPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(dir_input, INPUT_PULLUP);
  pinMode(9, OUTPUT);
  pinMode(framePin, OUTPUT);
  // initialize timer1
  noInterrupts();           // disable all interrupts
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
#if FAST
  OCR1A = 445;                  // 30KHz
#else
  OCR1A = 2667;                // compare match register, 16000000/2667 = 6KHz, 60/sec * 100
#endif
  TCCR1B  = 9;//|= (1 << WGM12);   // CTC mode
  TIMSK1 |= (1 << OCIE1A);  // enable timer compare interrupt

  ADCSRA = RUN_ADC - 0x40;  // set the adc, don't use free running mode or interrupt, need division rate to get 16MHz down to 0.2MHz, so need div by 128
  adc_high_res = 0;
  ADMUX = 0x40 + adc_high_res;  // use adc0, and AVref is reference (Vcc is full scale) was 0x40
  DIDR0 = 0x03;             // turn off the digital input for adc0 and adc1
  inp = 0;
  num_in = THROTTLE_NULL;
  direction_relay = 0;
  pause = 0;
  use_pot = 1;
  use_algorithm = 1;

  interrupts();             // enable interrupts
  Serial.begin(115200);
  Serial.print("Hit Enter key for instructions\n\n");
}

ISR(TIMER1_COMPA_vect)          // timer compare interrupt service routine
{ // Come here at 6000/sec rate, 167usec
  static byte a2d_running;

  if (!(ADCSRA & 0x40))         // A/D done?
    digitalWrite(9, 1);         // Pin high if still running

  if ((lcount < throttle_out) && (!(direction_relay & 0x7F)))   // Note throttle_out = 0 will not cause output to go high at all
    digitalWrite(pwmPin, 1);          // output on
  else
    digitalWrite(pwmPin, 0);         // output off
    
  if (lcount == throttle_out)
  {
    ADMUX = 0x40; 
    ADCSRA = RUN_ADC;  //           // A/D measures peak output voltage
    a2d_running = 3;    
  }

  lcount++;

  switch(lcount)
  {
   case 100:                            // At 100%, end blanking, reset count ready for new cycle
    lcount = 0;
    digitalWrite(blankPin, 0);          // End blanking
    digitalWrite(framePin, 0);          // Start of 60Hz frame
    break;
    
   case 99:   
    throttle_calculate();
    sixty_Hz = 1;               // Clock tick
	  digitalWrite(framePin, 1);  // End of 60Hz frame
    break;

  case 94:              // Time 94, start the A/D  
    if ((inp != 0) && (adc_high_res == 0) && (raw_speed.rs_w < 200))
      adc_high_res = 0x80;
    else if (adc_high_res && (raw_speed.rs_w > 1000))
      adc_high_res = 0;

    ADMUX = 0x40 + adc_high_res;
    ADCSRA = RUN_ADC;
    a2d_running = 1;                 // A/D measures speed starting at cycle 94
    break;

  case 90:
    if((inp != 0) && (inp <= 40))  // Only produce blanking pulse if back EMF required
    {
    digitalWrite(blankPin, 1);        // Blanking begins at 90% and lasts for final 10 counts
    }
    break;

  case 60:
    if(inp < 50)
    {
    ADMUX = 0x41; 
    ADCSRA = RUN_ADC;  //           // A/D measures pot input
    a2d_running = 2;
    }
    break;

  case 3: 
    if(inp >=50)
    {
    ADMUX = 0x41; 
    ADCSRA = RUN_ADC;  //           // A/D measures pot input
    a2d_running = 2;
    }
    break;
}

  if (!(ADCSRA & 0x40) && a2d_running)    // A/D has just finished
  {
    switch(a2d_running)
    {
      case 1:         // We were measuring speed    
        raw_speed.rs_w = ADC;
        break;
      
      case 2:    // We were measuring pot setting
        pot_setting = ADC;
        if (use_pot)
        {
          inp = pot_setting / 10;      // Maybe use it to control train. inp will be in range 0 - 102
          if (inp > 2)                // Adjust inp to range 0 - 100.
            inp -= 2;
          else
            inp = 0;
        }
        break;

      case 3:   // We were measuring peak output voltage
        volts_out = ADC;
        break;
      
    }
    a2d_running = 0;              // A/D reading is complete and data stored
  }

  digitalWrite(9, 0);
}

void check_direction(void)
{
  if ((direction_relay & 0x7F) == 0)                      // Nothing to do
    return;

  direction_relay--;
//  if ((direction_relay & 0x7F) == FAST ? 30 : 6)
 // {
    if (direction_relay & 0x80)
      digitalWrite(relayPin, 1);   // Set direction relay
    else
      digitalWrite(relayPin, 0);   // Clear direction relay
//  }
}

void loop()
{
  byte z;  
//  char inpah[200];
  byte dir_sw = 0;
  byte ct;
  byte l_inp;

//  Serial.print("Hit Enter key for instructions\n\n");
  while (1)
  {

    if (Serial.available())
    {
      l_inp = inp;
      z = Serial.read();
      if ((z == '=') || (z == '+') && (l_inp <= 99))                     // Increase speed
      {
        l_inp++;
        num_in = THROTTLE_NULL - 2;
        use_pot = 0;                               // Speed setting is from serial port, not pot
      }
      else if ((z == '-') && (l_inp > 0))          // Decrease speed
      {
        l_inp--;
        num_in = THROTTLE_NULL - 2;
        use_pot = 0;
      }
      else if (isDigit(z) && ((num_in < 10) || (num_in == THROTTLE_NULL)))      // Only accept 2 digits
      {
        if (num_in > 100)                       // First digit causes reset of numerical accumulator
          num_in = z - '0';
        else
          num_in = (num_in * 10) + (z - '0');   // 2nd digit is units and first digit was tens
      }
      else if ((z == '\r') || (z == '\n'))      // Accept numerical input if digits were entered
      {
        if (num_in < 100)
        {
          l_inp = num_in;
          num_in = THROTTLE_NULL;
          use_pot = 0;                         // Speed setting is from serial port, not pot
        }
        else if (num_in == THROTTLE_NULL)               // Bare <cr> has been entered, give info message
        {
          sprintf(inpah, "Type = for speed up, - for speed down, f for forward, r for reverse\n");
          Serial.print(inpah);
          sprintf(inpah, "Or enter any number 0-99 to set speed\n");
          Serial.print(inpah);
          sprintf(inpah, "Displayed data shows current speed setting, output duty cycle, present speed, error, pot reading\n\n");
          //     0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
          Serial.print(inpah);
          num_in = THROTTLE_NULL - 2;
          pause = 1;
        }
        else if (num_in == THROTTLE_NULL - 2)
        {
          pause = 0;
          num_in = THROTTLE_NULL;
        }
      }
      else if ((z == 'f') || (z == 'r'))        // Call for F or R direction
      {
        if (((direction_relay & 0x80) && (z == 'f')) || (!(direction_relay & 0x80) && (z == 'r')))
          direction_relay = 60 + (z == 'r' ? 0x80 : 0);
        num_in = THROTTLE_NULL - 2;                     // Do this so \n doesn't cause informational printout
      }

      else if (z == 'p')
      {
        use_pot = 1;
        num_in = THROTTLE_NULL - 2;                    // Do this so \n doesn't cause informational printout
      }

      else if (z == 'a')                // Use speed feedback
        use_algorithm = 1;
      else if (z == 'b')                // Don't use speed feedback
        use_algorithm = 0;

      if (!use_pot)
        inp = l_inp;
    }

    if (!sixty_Hz)
      continue;

    sixty_Hz = 0;              // Everything here happens at 60Hz rate, or less depending on <ct>
    ct++;

    if (digitalRead(dir_input) == 0)    // Direction switch pulling external pin low?
      dir_sw |= 1;
    else
      dir_sw &= 2;

    if ((dir_sw == 1) || (dir_sw == 2))               // 2 bits don't match, new direction called for
    {
      direction_relay = (FAST ? 60 : 12) + ((dir_sw & 1) << 7);   // Set relay count and new direction
      dir_sw = (dir_sw & 1) * 3;                      // dir_sw is now 00 or 11
      use_pot = 1;
    }

    check_direction();                                // Count down relay cutout time, if it's nonzero

    if (((ct & (FAST ? 0x1F : 0x7)) == 0) && !pause)          // One time in 16, about 4 Hz
    {
      sprintf(inpah, "%d\t%d\t%d %c\t%d\t%d\n", inp, throttle_out, raw_speed.rs_w, (adc_high_res ? 'L' : 'H'), error, pot_setting);
      Serial.print(inpah);
    }
  }
}  // End of loop()

void output_monitor(void)
{
  if (throttle_out == 0) 
  {
    if (volts_out > 1000)
    {
      track_occ = 0;
    }
    else
    {
      track_occ = 1;
    }    
  }

  if (volts_out < (throttle_out - 3))
  {
    overload = 1;
    inp = 0;
  }
  else
  {
    overload = 0;
  }
}

void throttle_calculate(void)
{
  int t_o;
  static byte xby;
  byte i;

  throttle_out = inp;
  if ((inp == 0) || !use_algorithm)
    return;

  if ((inp > 40) || (raw_speed.rs_w > 250))
  {
    xby = raw_speed.rs_b[0];
    return;
  }

  error = (inp * 64) - xby * 8  - raw_speed.rs_b[0] * 8;

  if (error < 0)
    throttle_out = inp;
  else if  (inp + (error / 16) > 99)
    throttle_out = 99;
  else if (inp + (error / 16) >= 0)
    throttle_out = inp + (error / 16);
  else
    throttle_out = inp;

  xby = raw_speed.rs_b[0];
}
