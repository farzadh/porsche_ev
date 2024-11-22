/*
 PORSCHE EV VCU
 Written for Aduino Due.
 This firmware implements following functionalities.
 1- Read temperature sensors for all regions, and report back.
 2- Support display (4*20) for reporting results
*/
// Libraries related to display
#include <LiquidCrystal_I2C.h> //install liquid crystal I2C library by Marco Schwarz
#include <Metro.h> // for timers

Metro sensor_read_timer = Metro(5000); //Read temperature sensors this often
Metro display_timer = Metro(500); //write to display (home page) at this interval
Metro error_display_timer = Metro(9000); //write error message if any at this interval
Metro error_display_duration = Metro(2000); //keep error message on display this long
Metro error_recovery_timer = Metro(15000); //Try to recover from a fault
bool error_showing = false; //indicates that an error message is being displayed at the moment

Metro serial_port_timer = Metro(2000);
//Initialize the liquid crystal library
#define NUM_LCD_ROWS 4
#define NUM_LCD_COLUMNS 20
LiquidCrystal_I2C lcd(0x27, NUM_LCD_COLUMNS, NUM_LCD_ROWS); //(I2C address, num_rows, num_columns)

// Libraries for reading temerature sensors
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 4 // Data wire is plugged into port 4 on the Arduino
#define NUM_TEMP_SENSORS_EXPECTED 5
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature. 
int num_temp_sensors_found; // Number of temperature devices found
//Index mapping for temperatue readings: [0]->water inlet, [1]->B1/2_Hi, [2]->B1/2_Lo, [3]->B3/4_Hi, [4]->B3/4_Lo
float temperatures_C[NUM_TEMP_SENSORS_EXPECTED] = {-100.0};

typedef enum
{
  NO_ERROR = 0,
  MISSING_SENSORS,
  BOGUS_SENSORS,
  GHOST_SENSORS
} error_t;
error_t last_error_code = NO_ERROR;


void loop()
{
  if (sensor_read_timer.check())
  {
    read_temperatures();
  }
  if(!error_showing && display_timer.check())
  {
    update_display();
  }
  /*if(last_error_code && !error_showing && error_display_timer.check())
  {
    display_error();
  }*/
  if (error_display_duration.check())
  {
    error_showing = false;
  }
  if (last_error_code && error_recovery_timer.check())
  {
    error_recovery_routine();
  }
  if (serial_port_timer.check())
  {
    Serial.print("Still alive ... \n");
  }
}

/////////////////// HANDLE SENSORS ////////////////////
void read_temperatures()
{
  // [0] Water Inlet  --> 0x13C05
  // [1] Bank1/2_High --> 0x13C7C
  // [2] Bank1/2_Low  --> 0x13CDE
  // [3] Bank3/4_High --> 0x13C57
  // [4] Bank3/4_Low  --> 0x13CA3
  for (int i = 0; i < NUM_TEMP_SENSORS_EXPECTED; i++)
  {
    temperatures_C[i] = -100.0;
  }

  sensors.requestTemperatures(); // Send the command to get temperatures
  // Loop through each device, and map based on device's address
  for(int i = 0; i < num_temp_sensors_found; i++)
  {
    if (i >= NUM_TEMP_SENSORS_EXPECTED)
    {
      break;
    }
    
    DeviceAddress device_address;
    // Search the wire for address
    if(sensors.getAddress(device_address, i))
    {
      // Output the device ID
      Serial.print("Temperature for device: ");
      Serial.print(i,DEC);
      // Print the data
      float tempC = sensors.getTempC(device_address);
      Serial.print("Temp C: ");
      Serial.println(tempC);
      switch (device_address[7] & 0xFF)
      {
        case 0x05:
          temperatures_C[0] = tempC;
        break;
        case 0x7C:
          temperatures_C[1] = tempC;
        break;
        case 0xDE:
          temperatures_C[2] = tempC;
        break;
        case 0x57:
          temperatures_C[3] = tempC;
        break;
        case 0xA3:
          temperatures_C[4] = tempC;
        break;
        default:
          display_message("Unknown address: %2x", device_address[7]);
        break;
      }
    }
    else
    {
      display_message("Could not find address for device %d", i);
      last_error_code = MISSING_SENSORS;
    }
  }
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
  // Loop through each device, print out address
  for(int i = 0; i < num_temp_sensors_found; i++)
  {
    DeviceAddress device_address; //unit[8]
    // Search the wire for address
    if(sensors.getAddress(device_address, i))
    {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(device_address);
      Serial.println();
    } else {
      display_message("Ghost device at %d, could not detect address. Check power and cabling.", i);
      last_error_code = GHOST_SENSORS;
    }
  }  
}

/* function to print a device address */
void printAddress(DeviceAddress deviceAddress) 
{
  for (uint8_t i = 0; i < 8; i++) 
  {
    if (deviceAddress[i] < 16)
    {
      Serial.print("0");
    }
    Serial.print(deviceAddress[i], HEX);
  }
}


/////////////////// ERROR HANDLING ////////////////////
void error_recovery_routine()
{
  switch (last_error_code)
  {
    case MISSING_SENSORS:
    case BOGUS_SENSORS:
    case GHOST_SENSORS:
      display_message("Recovery routine for error number: %d",last_error_code);
      delay(1500);
      setup_sensors();
      break;
    default:
      display_message("Unkown recovery routine for error: %d", last_error_code);
      break;
  }
}

/////////////////// SETUP FUNCTIONS ////////////////////
void setup_lcd(void) {
  lcd.init(); //initialize lcd screen
  lcd.backlight(); // turn on the backlight
}

void setup(void) {
  setup_lcd();
  Serial.begin(9600);
  setup_sensors();
}

/////////////////// DISPLAY MONITOR HANDLING ////////////////////////
typedef enum
{
  PAGE_HOME = 0,
  PAGE_ERROR,
  PAGE_INFO
} display_page_t;
// keeps track last page shown on display
display_page_t lcd_page_index = PAGE_HOME;

/* Display formatted messages to the LCD display and also handle text wrapping */
void display_message(const char* message, ...)
{
  char buffer[NUM_LCD_ROWS * NUM_LCD_COLUMNS + 20];
  char line_buffer[NUM_LCD_COLUMNS + 1];
  va_list args;
  va_start(args, message);
  vsnprintf(buffer, sizeof(buffer), message, args);
  va_end(args);
  
  Serial.println(buffer);
  
  lcd.clear();
  int message_length = strlen(buffer);
  int start_index = 0;
  for (int row = 0; row < NUM_LCD_ROWS; row++)
  {
    int remaining_length = message_length - start_index;
    if (remaining_length <= 0)
    {
      break;
    }
    int line_length = remaining_length > NUM_LCD_COLUMNS ? NUM_LCD_COLUMNS : remaining_length;
    strncpy(line_buffer, buffer + start_index, line_length);
    line_buffer[line_length] = '\0';

    lcd.setCursor(0, row);
    lcd.print(line_buffer);
    start_index += NUM_LCD_COLUMNS;
  }
  lcd_page_index = PAGE_INFO;
  error_showing = true;
  error_display_duration.reset();
}

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
  
  char line_buffer[30];
  // [0] Water Inlet  --> 0x13C05
  // [1] Bank1/2_High --> 0x13C7C
  // [2] Bank1/2_Low  --> 0x13CDE
  // [3] Bank3/4_High --> 0x13C57
  // [4] Bank3/4_Low  --> 0x13CA3
  for (int i = 0; i < NUM_TEMP_SENSORS_EXPECTED; i++)
  {
    char sensor_name[10];
    uint8_t row = 0, col = 0;
    switch (i)
    {
      case 0:
        sprintf(sensor_name, "WtrIn");
        row = 0;
        col = 0;
        break;
      case 1:
        sprintf(sensor_name, "B12H");
        row = 1;
        col = 0;
        break;
      case 2:
        sprintf(sensor_name, "B12L");
        row = 1;
        col = 10;
        break;
      case 3:
        sprintf(sensor_name, "B34H");
        row = 2;
        col = 0;
        break;
      case 4:
        sprintf(sensor_name, "B34L");
        row = 2;
        col = 10;
        break;
    }
    if (temperatures_C[i] > -100){
      sprintf(line_buffer, "%s:%2.0f", sensor_name, temperatures_C[i]);
    } else {
      sprintf(line_buffer, "%s:??   ", sensor_name);
    }
    lcd.setCursor(col, row);
    lcd.print(line_buffer);
  }
  
  //blinking star
  lcd.setCursor(19, 0);
  lcd.print(display_flag ? "*": " ");
  
  // Status line
  lcd.setCursor(0, 3);
  lcd.print("STATUS: ");
  if (!last_error_code)
  {
    lcd.print("OK          ");
  }
  else
  {
    sprintf(line_buffer, "ERROR(%d)", last_error_code);
    lcd.print(line_buffer);
  }
}
