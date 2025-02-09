#include <Arduino.h>
#include <WiFi.h>

#include <WiFiManager.h>
#include <ElegantOTA.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <time.h>

// Start user config
// --------------------------------------------------------

// Define your ntp server here
const char* ntpServer = "pool.ntp.org";

// Define your timezone here (offset from Greenwich Mean Time)
const long gmtOffset_sec = 3600;

// Define your Daylight saving time offset here
const int daylightOffset_sec = 3600;

#define BUILD_IN_LED 2

// Set your PWM pin here
#define LIGHT_PWM_GPIO 4

// Light will start dimming up at this time
#define START_HOUR   6
#define START_MINUTE 45

// Set ramp up, constant and ramp down times here in ms
#define RAMP_UP_TIME     ( 20 * 60 * 1000 ) // ramp from 0 to full within 20 minutes
#define CONSTANT_ON_TIME ( 40 * 60 * 1000 ) // stay on at full brightness for 40 more minutes --> turn of afer 60 minutes
#define RAMP_DOWN_TIME   ( 4 * 1000 )       // Ramp down in four seconds

// End user config
// --------------------------------------------------------

#define PWM_FREQ   1000 // in Hz
#define RESOLUTION 16   // in bit

#define SUNDAY    0
#define MONDAY    1
#define TUESDAY   2
#define WEDNESDAY 3
#define THURSDAY  4
#define FRIDAY    5
#define SATURDAY  6

#define PIN_TOGGLE( p ) digitalWrite( p, !digitalRead( p ) );

AsyncWebServer server( 80 );
struct tm      timeinfo;

// clang-format off
#define DIM_STEPS 256
// Table to adjust the dim steps into a linear brightness change for the human eye
const uint16_t pwmtable_16[DIM_STEPS] =
{
       0,     1,     1,     1,     1,     1,     1,     1,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     3,     3,     3,     3,     3,     3,     3,     4,     4,     4,     4,
       4,     5,     5,     5,     5,     5,     6,     6,     6,     6,     7,     7,     7,     8,     8,     8,
       9,     9,     9,    10,    10,    10,    11,    11,    12,    12,    13,    13,    14,    15,    15,    16,
      17,    17,    18,    19,    20,    21,    22,    23,    24,    25,    26,    27,    28,    29,    31,    32,
      33,    35,    36,    38,    40,    41,    43,    45,    47,    49,    52,    54,    56,    59,    61,    64,
      67,    70,    73,    76,    79,    83,    87,    91,    95,    99,   103,   108,   112,   117,   123,   128,
     134,   140,   146,   152,   159,   166,   173,   181,   189,   197,   206,   215,   225,   235,   245,   256,
     267,   279,   292,   304,   318,   332,   347,   362,   378,   395,   412,   431,   450,   470,   490,   512,
     535,   558,   583,   609,   636,   664,   693,   724,   756,   790,   825,   861,   899,   939,   981,  1024,
    1069,  1117,  1166,  1218,  1272,  1328,  1387,  1448,  1512,  1579,  1649,  1722,  1798,  1878,  1961,  2048,
    2139,  2233,  2332,  2435,  2543,  2656,  2773,  2896,  3025,  3158,  3298,  3444,  3597,  3756,  3922,  4096,
    4277,  4467,  4664,  4871,  5087,  5312,  5547,  5793,  6049,  6317,  6596,  6889,  7194,  7512,  7845,  8192,
    8555,  8933,  9329,  9742, 10173, 10624, 11094, 11585, 12098, 12634, 13193, 13777, 14387, 15024, 15689, 16384,
   17109, 17867, 18658, 19484, 20346, 21247, 22188, 23170, 24196, 25267, 26386, 27554, 28774, 30048, 31378, 32768,
   34218, 35733, 37315, 38967, 40693, 42494, 44376, 46340, 48392, 50534, 52772, 55108, 57548, 60096, 62757, 65535 
};
// clang-format on

enum class DIM_STATE
{
   Idle,
   DimUp,
   Constant,
   DimDown
};

uint16_t    u16CurrentIdx      = 0;
DIM_STATE   DimState           = DIM_STATE::Idle;
static long NextPwmUpdateTime  = 0;
static long TimeBetweenUpdates = 0;

void DimLed( bool bStart )
{
   static long CurrentTime = 0;
   CurrentTime             = millis();

   if( bStart ) // reset function if bStart is true
   {
      u16CurrentIdx      = 0;
      DimState           = DIM_STATE::DimUp;
      TimeBetweenUpdates = RAMP_UP_TIME / DIM_STEPS;
      NextPwmUpdateTime  = CurrentTime + TimeBetweenUpdates;
   }

   switch( DimState )
   {
   case DIM_STATE::Idle:
      break;

   case DIM_STATE::DimUp:
      if( CurrentTime > NextPwmUpdateTime )
      {
         NextPwmUpdateTime = CurrentTime + TimeBetweenUpdates;
         u16CurrentIdx++;
         if( u16CurrentIdx >= DIM_STEPS )
         {
            u16CurrentIdx     = DIM_STEPS - 1;
            NextPwmUpdateTime = CurrentTime + CONSTANT_ON_TIME;
            DimState          = DIM_STATE::Constant;
         }
      }
      break;

   case DIM_STATE::Constant:
      if( CurrentTime > NextPwmUpdateTime )
      {
         TimeBetweenUpdates = RAMP_DOWN_TIME / DIM_STEPS;
         NextPwmUpdateTime  = CurrentTime + TimeBetweenUpdates;
         DimState           = DIM_STATE::DimDown; // End --> Ramp down fast
      }
      break;

   case DIM_STATE::DimDown:
      if( CurrentTime > NextPwmUpdateTime )
      {
         NextPwmUpdateTime = CurrentTime + TimeBetweenUpdates;
         u16CurrentIdx--;
         if( u16CurrentIdx == 0 )
         {
            DimState = DIM_STATE::Idle; // End --> Idle until tomorrow
         }
      }
      break;
   }

   analogWrite( LIGHT_PWM_GPIO, pwmtable_16[u16CurrentIdx] );
   delay( 2 ); // why is this necessary? Without the pwm value will not be active on the pin
}

void PrintCurrentTime( void )
{
   Serial.println( &timeinfo, "Time: %A - %H:%M:%S" );
}

// Dim up and down after power on
void StartUpLedTest( void )
{
   int i;

   // Dim up (256*10ms = 2.56s)
   for( i = 0; i < DIM_STEPS; i++ )
   {
      analogWrite( LIGHT_PWM_GPIO, pwmtable_16[i] );
      delay( 10 );
   }

   // Dim down (256*10ms = 2.56s)
   for( i = DIM_STEPS - 1; i > 0; i-- )
   {
      analogWrite( LIGHT_PWM_GPIO, pwmtable_16[i] );
      delay( 10 );
   }
}

void setup()
{
   WiFiManager wm;
   bool        res;

   // Set pin directions
   pinMode( BUILD_IN_LED, OUTPUT );
   pinMode( LIGHT_PWM_GPIO, OUTPUT );

   // PWM Pin
   analogWriteFrequency( PWM_FREQ );
   analogWriteResolution( RESOLUTION ); // 16 bit

   // Serial communication
   Serial.begin( 115200 );
   delay( 1000 );
   Serial.println( '\n' );

   StartUpLedTest();

   wm.setHostname( "LightAlarmClock" );
   res = wm.autoConnect( "LightAlarmClock ConfigAP" );

   if( !res )
      Serial.println( "Failed to connect" );
   else
      Serial.println( "connected...yeey :)" );

   Serial.print( "IP address:\t" );
   Serial.println( WiFi.localIP() ); // Send the IP address via uart

   // Config time server
   configTime( gmtOffset_sec, daylightOffset_sec, ntpServer );

   ElegantOTA.begin( &server ); // Start ElegantOTA
   server.begin();
}

// Check if we should start the dim ramp
void IsItTimeForWakeUp( void )
{
   static bool bHasTriggered = false;

   if( ( timeinfo.tm_wday >= MONDAY ) && ( timeinfo.tm_wday <= FRIDAY ) )
   {
      if( ( timeinfo.tm_hour == START_HOUR ) && ( timeinfo.tm_min == START_MINUTE ) && !bHasTriggered )
      {
         DimLed( true );
         bHasTriggered = true;
      }
      else if( timeinfo.tm_min != START_MINUTE )
      {
         bHasTriggered = false;
      }
   }
}

// Will be called every second
void EverySecond( void )
{
   getLocalTime( &timeinfo );
   IsItTimeForWakeUp();
   PrintCurrentTime();
   PIN_TOGGLE( BUILD_IN_LED )
}

long Timer1s = 0;
void loop()
{
   long CurrentTime;

   CurrentTime = millis();

   if( CurrentTime > Timer1s )
   {
      Timer1s = CurrentTime + 1000;
      EverySecond();
   }

   ElegantOTA.loop();
   DimLed( false );
}
