/*
 PORSCHE EV VCU
 Written for Aduino Due.
 This firmware implements following functionalities.
 1- Read temperature sensors for all regions, and report back.
 2- Support display (4*20) for reporting results
 3- 
*/
// Libraries related to display
#include <LiquidCrystal_I2C.h> //install liquid crystal I2C library by Marco Schwarz
#include <Metro.h> // for timers

Metro display_timer = Metro(500); //write to display (home page) at this interval
Metro error_display_timer = Metro(9000); //write error message if any at this interval
Metro error_display_duration = Metro(2000); //keep error message on display this long
bool error_showing = false; //indicates that an error message is being displayed at the moment

Metro serial_port_timer = Metro(2000);
//Initialize the liquid crystal library
LiquidCrystal_I2C lcd(0x27, 20, 4); //(I2C address, num_rows, num_columns)

// Libraries for reading temerature sensors
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 4 // Data wire is plugged into port 4 on the Arduino
#define NUM_TEMP_SENSORS_EXPECTED 5
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature. 
int num_temp_sensors_found; // Number of temperature devices found
DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address

typedef enum
{
  NO_ERROR = 0,
  MISSING_SENSORS,
  BOGUS_SENSORS
} error_t;
error_t last_error_code = NO_ERROR;

void setup_lcd(void) {
  lcd.init(); //initialize lcd screen
  lcd.backlight(); // turn on the backlight
}

void setup_sensors()
{
  sensors.begin();
  num_temp_sensors_found = sensors.getDeviceCount();  // Grab a count of devices on the wire
  if (num_temp_sensors_found != NUM_TEMP_SENSORS_EXPECTED)
  {
    last_error_code = num_temp_sensors_found < NUM_TEMP_SENSORS_EXPECTED ? MISSING_SENSORS : BOGUS_SENSORS;
    display_error();
  }
}

void setup(void) {
  setup_lcd();
  Serial.begin(9600);
  setup_sensors();
  // Loop through each device, print out address
  /*for(int i=0;i<num_temp_sensors_found; i++) {
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i)) {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
  }*/
}
/*
void loop(void) { 
  sensors.requestTemperatures(); // Send the command to get temperatures
  
  // Loop through each device, print out temperature data
  for(int i=0;i<numberOfDevices; i++) {
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i)){
    
    // Output the device ID
    Serial.print("Temperature for device: ");
    Serial.println(i,DEC);

    // Print the data
    float tempC = sensors.getTempC(tempDeviceAddress);
    Serial.print("Temp C: ");
    Serial.print(tempC);
    Serial.print(" Temp F: ");
    Serial.println(DallasTemperature::toFahrenheit(tempC)); // Converts tempC to Fahrenheit
    }   
  }
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
      Serial.print(deviceAddress[i], HEX);
  }
}

*/

void loop()
{
  if(!error_showing & display_timer.check())
  {
    update_display();
  }
  if(last_error_code && error_display_timer.check())
  {
    display_error();
  }
  if (error_display_duration.check())
  {
    error_showing = false;
  }
  if (serial_port_timer.check())
  {
    Serial.print("Still alive ... \n");
  }
}

/////////////////// DISPLAY MONITOR HANDLING ////////////////////////
typedef enum
{
  PAGE_HOME = 0,
  PAGE_ERROR
} display_page_t;
// keeps track last page shown on display
display_page_t lcd_page_index = PAGE_HOME;

void display_error()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  switch (last_error_code)
  {
    case NO_ERROR:
      lcd.print("No error found!");
    break;
    case MISSING_SENSORS:
      lcd.print("Missing sensors!");
      lcd.setCursor(0, 1);
      lcd.print("Expected: ");
      lcd.print(NUM_TEMP_SENSORS_EXPECTED);
      lcd.setCursor(0, 2);
      lcd.print("Found: ");
      lcd.print(num_temp_sensors_found);
    break;
    case BOGUS_SENSORS:
      lcd.print("Bogus sensors!");
      lcd.setCursor(0, 1);
      lcd.print("Expected: ");
      lcd.print(NUM_TEMP_SENSORS_EXPECTED);
      lcd.setCursor(0, 2);
      lcd.print("Found: ");
      lcd.print(num_temp_sensors_found);
    break;
    default:
      lcd.print("Unknown error");
    break;
  }
  lcd_page_index = PAGE_ERROR;
  error_showing = true;
  error_display_duration.reset();
}

void update_display()
{
  static bool display_flag = 0;
  display_flag = !display_flag;
  if (lcd_page_index != PAGE_HOME)
  {
    lcd.clear();
  }
  lcd_page_index = PAGE_HOME;
  lcd.setCursor(0, 0);
  lcd.print("Hello!");
  lcd.setCursor(0, 3);
  lcd.print("Page: ");
  lcd.print(lcd_page_index);
  lcd.setCursor(19, 0);
  if (display_flag)
  {
    lcd.print("*");
  }
  else
  {
    lcd.print(" ");
  }
  lcd.setCursor(16, 3);
  if (last_error_code != NO_ERROR)
  {
    lcd.print("ERR!");      
  }
  else
  {   
    lcd.print("    ");
  }
}
