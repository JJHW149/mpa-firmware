#include <USBHost_t36.h>

USBHost usbHost;
USBHub hubOne( usbHost );
USBHub hubTwo( usbHost );
MIDIDevice midiInput( usbHost );

// structure containing how many milliseconds each function is to be left 'on'. 
// 'start'/'select' are handled slightly differently, but could be added here
struct kitstate {
  int rPad;
  int kick;
  int yPad;
  int yHat;
  int bPad;
  int bHat;
  int gPad;
  int gHat; 
};

// boolean flag indicating whether we need to send the kit state
// out to our connected device - set by noteon events, or an imminent
// timing out of a pad

bool kitDirty;

// some configuration 
//  - the LEDPIN is the default for the T3.6
//  - if INPUTPIN is defined, you can trigger a 'start/select' by shorting it to ground
//  - if CC_MAX is defined, 'start/select' will be triggered by any continous controller (usually a hat pedal) exceeding that value
//  - NOTE_ON_TIME indicates the minimum duration, in milliseconds, notes will be left on: we don't wait for noteoff here
//    this uses elapsed time based on millis() instead of an interrupt - make sure you reboot your adapter at least once every 50 days
//  - BLINKY, if defined, will flash the LEDPIN when any pad is "on"

#define LEDPIN 13
#define INPUTPIN 0
#define CC_MAX 0x5A
#define NOTE_ON_TIME 25
#define BLINKY 1

// the Yamaha DTX 502 seems to assign MIDI numbers to the crash and ride hats that
// are reversed compared with my TD-1 and the original MPA settings - uncomment the below to use them
//#define YAMAHA_DTX_502

// The Alesis Crimson 2 is another kit which also has different midi mapping, the tom rims and
// the high-hat uses different values from the TD-1 and the originl MPA settings - uncomment the below to use them
#define Alesis_Crimson_2

// some program state
struct kitstate currentKitState;
bool inputPinState = false;
bool previousInputPinState = false;
unsigned long lastLoopTime;
#ifdef CC_MAX
bool continuousControllerPressed = false;
#endif


void setup() {
#ifdef INPUTPIN
  pinMode( INPUTPIN, INPUT_PULLUP );
#endif
  pinMode( LEDPIN, OUTPUT );

  currentKitState.rPad = 0;
  currentKitState.kick = 0;
  currentKitState.yPad = 0; 
  currentKitState.yHat = 0;
  currentKitState.bPad = 0;
  currentKitState.bHat = 0;
  currentKitState.gPad = 0;
  currentKitState.gHat = 0;
  kitDirty = false;  
  lastLoopTime = millis();

  // advice in the api examples suggests pausing briefly before starting the userland usb subsystem, 
  // to allow host enumeration to conclude first. we blink the led to show when initialisation delay has
  // completed

  digitalWrite( LEDPIN, HIGH );
  delay( 1500 );
  digitalWrite( LEDPIN, LOW );
  
  usbHost.begin();
  midiInput.setHandleNoteOn( onNoteOn );
#ifdef CC_MAX
  midiInput.setHandleControlChange( controlChange );
#endif
};

// if a kit has a positive time on remaining, reduce it by 'elapsed' milliseconds,
// but clamp the value at zero. set the kit dirty flag if any kit would hit zero.
void ageKitStates( int elapsed ) {
  if( ( currentKitState.rPad && currentKitState.rPad <= elapsed ) ||
      ( currentKitState.kick && currentKitState.kick <= elapsed ) ||
      ( currentKitState.yPad && currentKitState.yPad <= elapsed ) ||
      ( currentKitState.yHat && currentKitState.yHat <= elapsed ) ||
      ( currentKitState.bPad && currentKitState.bPad <= elapsed ) ||
      ( currentKitState.bHat && currentKitState.bHat <= elapsed ) ||
      ( currentKitState.gPad && currentKitState.gPad <= elapsed ) ||
      ( currentKitState.gHat && currentKitState.gHat <= elapsed ) )
      // at least one pad will age out in this cycle
      kitDirty = true;
      
  currentKitState.rPad = max( 0, currentKitState.rPad - elapsed );
  currentKitState.kick = max( 0, currentKitState.kick - elapsed );
  currentKitState.yPad = max( 0, currentKitState.yPad - elapsed );
  currentKitState.yHat = max( 0, currentKitState.yHat - elapsed );
  currentKitState.bPad = max( 0, currentKitState.bPad - elapsed );
  currentKitState.bHat = max( 0, currentKitState.bHat - elapsed );
  currentKitState.gPad = max( 0, currentKitState.gPad - elapsed );
  currentKitState.gHat = max( 0, currentKitState.gHat - elapsed );
}

void loop() {
  int elapsed;
  
  usbHost.Task();
  midiInput.read();

  // age kit states
  elapsed = millis() - lastLoopTime;
  ageKitStates( elapsed );  

  // manage the "input pin", which sends a 'start/select' message
  inputPinState = false;
#ifdef INPUTPIN
  // if INPUTPIN is defined and the pin is pulled low,
  inputPinState |= ( digitalRead( INPUTPIN ) != HIGH );
#endif
#ifdef CC_MAX
  inputPinState |= continuousControllerPressed;
#endif
  
  if( inputPinState != previousInputPinState ) {
    kitDirty = true;
    previousInputPinState = inputPinState;
  }

  // if the kit is dirty, send the necessary reports
  if( kitDirty ) {
    usb_mpa_reset_packet();
    
    if( inputPinState ) usb_mpa_set_button( 9 );
    if( currentKitState.kick ) usb_mpa_set_button( 4 );
    if( currentKitState.rPad ) usb_mpa_set_button( 2 );
    if( currentKitState.yPad || currentKitState.yHat ) usb_mpa_set_button( 3 );
    if( currentKitState.bPad || currentKitState.bHat ) usb_mpa_set_button( 0 );
    if( currentKitState.gPad || currentKitState.gHat ) usb_mpa_set_button( 1 );
    if( currentKitState.rPad || currentKitState.yPad || currentKitState.bPad || currentKitState.gPad ) usb_mpa_set_button( 10 );
    if( currentKitState.yHat ) usb_mpa_set_hat( MPA_HAT_UP );
    if( currentKitState.bHat ) usb_mpa_set_hat( MPA_HAT_DOWN );
    if( currentKitState.yHat || currentKitState.bHat || currentKitState.gHat ) usb_mpa_set_button( 11 );
    
    usb_mpa_send();
    kitDirty = false;
  }

#ifdef BLINKY
  // if we are doing input blinks, test if any pad has time remaining on it
  if( currentKitState.rPad || currentKitState.kick || 
      currentKitState.yPad || currentKitState.yHat || 
      currentKitState.bPad || currentKitState.bHat || 
      currentKitState.gPad || currentKitState.gHat || inputPinState )
    digitalWrite( LEDPIN, HIGH );
  else
    digitalWrite( LEDPIN, LOW );
#endif 

  lastLoopTime = millis();
}

// the constants used in the switch statement should support, at least,
// roland v-drums and the yamaha dtx 502

void onNoteOn( byte channel, byte note, byte velocity ) {
  kitDirty = true;
  switch( note ) {

    case 27: case 31: case 34: case 37: case 38: case 39: case 40:
      currentKitState.rPad += NOTE_ON_TIME;
      break;

    case 35: case 36:
      currentKitState.kick += NOTE_ON_TIME;
      break;
      
    case 48: case 50:
#ifdef Alesis_Crimson_2
    case 82:
#endif
      currentKitState.yPad += NOTE_ON_TIME;
      break;


    case 22: case 26: case 42: case 44: case 46: case 54: case 78: case 79: 
    case 83: case 85: case 86: case 23: 
#ifdef Alesis_Crimson_2
    case 8:
#endif
      currentKitState.yHat += NOTE_ON_TIME;
      break;
      
    case 45: case 47:
#ifdef Alesis_Crimson_2
    case 80:
#endif
      currentKitState.bPad += NOTE_ON_TIME;
      break;

#ifdef YAMAHA_DTX_502
    case 59: case 49: case 55:    
#else
    case 51: case 53: case 58: case 59:
#endif
      currentKitState.bHat += NOTE_ON_TIME;
      break;
      
    case 43: case 41:
#ifdef Alesis_Crimson_2
    case 75:
#endif
      currentKitState.gPad += NOTE_ON_TIME;
      break;

#ifdef YAMAHA_DTX_502
    case 51: case 52: case 53:
#else
    case 49: case 52: case 55: case 57:
#endif
      currentKitState.gHat += NOTE_ON_TIME;
      break;
      
  }
}

#ifdef CC_MAX
// this function reads all continous controllers at once - not ideal, but
// I didn't want to have to deal with different kits defining them differently
void controlChange(byte channel, byte control, byte value) {
  if( value >= CC_MAX ) continuousControllerPressed = true;
  else continuousControllerPressed = false;
}
#endif
