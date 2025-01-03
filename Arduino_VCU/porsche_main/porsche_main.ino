/*
 PORSCHE EV VCU
 Written for Aduino Due.
 This firmware implements following functionalities.
 1- Read temperature sensors for all regions, and report back.
 2- Support display (4*20) for reporting results
*/
#include <due_can.h>

// Libraries related to display
#include <LiquidCrystal_I2C.h> //install liquid crystal I2C library by Marco Schwarz
#include <Metro.h> // for timers

Metro sensor_read_timer = Metro(5000); //Read temperature sensors this often
Metro display_timer = Metro(500); //write to display (home page) at this interval
Metro page_switch_timer = Metro(2000); //page switch timer (for home page)
Metro error_display_timer = Metro(60000); //write error message if any at this interval
Metro error_display_duration = Metro(2000); //keep error message on display this long
Metro error_recovery_timer = Metro(90000); //Try to recover from a fault
bool error_showing = false; //indicates that an error message is being displayed at the moment

Metro serial_port_timer = Metro(10000); //timer for printing alive messages
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
float temperatures_C[NUM_TEMP_SENSORS_EXPECTED] = {-100.0,-100.0,-100.0,-100.0,-100.0};

CAN_FRAME outFrame; //A structured variable according to due_can library for transmitting CAN data.
CAN_FRAME inFrame;  //structure to keep inbound inFrames

// Charger related parameters
const uint16_t HV_MAX_CHG_VOLTAGE = 403; //HV battery is charged up to this voltage (= 96 * 4.2, rounded down)
const uint16_t HV_MAX_CHG_CURRENT = 10; //Set for 3.3 KW TC Elcon Charger
Metro charger_timer = Metro(400);
struct charger_status_report
{
  float HV_Voltage = 0; //Current voltage of HV battery (bytes 0/1)
  float HV_Current = 0; //Charge current of HV battery (bytes 2/3)
  union
  {
    uint8_t byte4_value = 0;
    struct{
      uint8_t HW_protection : 1; //0:Normal, 1:Fault
      uint8_t Temp_protection : 1; //0:Normal, 1:Fault
      uint8_t input_volt_status : 2; //0: Normal, 1: under voltage, 2: over-voltage, 3: no voltage
      uint8_t output_under_voltage : 1; //0:Normal, 1:Fault
      uint8_t output_over_voltage  : 1; //0:Normal, 1:Fault
      uint8_t output_over_current  : 1; //0:Normal, 1:Fault
      uint8_t output_short_circuit : 1; //0:Normal, 1:Fault      
    } protection;
  };
  union
  {
    uint8_t byte5_value = 0;
    struct{
      uint8_t comm_status : 1; //0:Normal, 1:Receiver Timeout
      uint8_t working_status : 2; //0:Undefined, 1:Work, 2: Stop, 3: Stop/Standby
      uint8_t init_completion : 1; //0: not complete, 1: completed
      uint8_t fan_enable: 1; //0: close, 1: open
      uint8_t pump_fan_enable: 1; //0: close, 1: open
      uint8_t unused : 2;
    } work_status;
  };
  union
  {
    uint8_t byte6_value = 0;
    struct{
      uint8_t CC_signal : 2; //0:Not connected, 1:half connected, 2: Normal connected, 3: Resistance detection error
      uint8_t CP_signal : 1; //0: No CP signal, 1: CP signal normal
      uint8_t socket_overheat : 1; //0: Normal, 1: Charge socket overheat protection
      uint8_t electronic_lock: 3; //0: in judgement, 1: Locked, 2:unlocked, 3:unlock fault, 4: lock fault
      uint8_t S2_sw_ctrl: 1; //0: switch off, 1: close up
    } electronic_status;
  };
  int temperature_C = -100;
  bool OK_to_charge = false; //indicates if all is good to enable charging
  bool NMI = false; //New message indicator
  unsigned long last_update = 0;
} charger_status;

// DC-DC related parameters
struct dcdc_status_report
{
  float DCDC_Voltage = 0; //DCDC output voltage (bytes 0/1)
  float DCDC_Current = 0; //DCDC output current (bytes 2/3)
  union
  {
    uint8_t byte4_value = 0;
    struct{
      uint8_t DCDC_ready : 1;      // 0: incomplete, 1: complete
      uint8_t DCDC_status : 1;     // 0: stop, 1: work
      uint8_t hard_fault : 1;      // 0: no error, 1: error
      uint8_t error_CAN : 1;       // 0: no error, 1: error
      uint8_t fan_ctrl : 1;        // 0: off, 1: on
      uint8_t DCDC_stop_error : 1; // 0: no error, 1: error
      uint8_t water_fan : 1;       // 0: fan off, 1: fan on
      uint8_t HVIL_error : 1;      // 0: lock accomplish, 1: non lock
    } work_status;
  };
  union
  {
    uint8_t byte5_value = 0;
    struct{
      uint8_t warm_temperature : 1; // 0: no warm, 1: warm
      uint8_t over_temperature : 1; // 0: no error, 1: error
      uint8_t over_voltage_in : 1;  // 0: no error, 1: error
      uint8_t low_voltage_in : 1;   // 0: no error, 1: error
      uint8_t over_voltage_out : 1; // 0: no error, 1: error
      uint8_t low_voltage_out : 1;  // 0: no error, 1: error
      uint8_t over_current_out : 1; // 0: no error, 1: error
      uint8_t unused : 1;
    } protection;
  };
  int temperature_C = -100;
  unsigned long last_update = 0;
} dcdc_status;


typedef enum
{
  IGNITION_OFF = 0, //ignition switch is off
  IGNITION_ON,      //ignition switch is on
  START_TRIGGERRED, //ignition switch is on, start switch is trigggerred (ready to move)
  CHARGER_PLUGGED,    //charger is plugged in
} run_status_t;
run_status_t vehicle_status = IGNITION_OFF;

typedef enum
{
  NO_ERROR = 0,
  MISSING_SENSORS,
  BOGUS_SENSORS,
  GHOST_SENSORS,
  CHARGER_ERROR
} error_t;
error_t last_error_code = NO_ERROR;

struct relays
{
  bool water_pump = false;
  bool radiator_fan = false;
} control_relays;

void loop()
{
  handle_CAN();

  if ((vehicle_status == CHARGER_PLUGGED) && charger_timer.check() && charger_status.OK_to_charge)
  {
    send_charger_msg();
  }
  
  if (sensor_read_timer.check())
  {
    read_temperatures();
    sensor_read_timer.reset();
  }
  if(!error_showing && display_timer.check())
  {
    update_display();
    display_timer.reset();
  }
  /*if(last_error_code && !error_showing && error_display_timer.check())
  {
    display_error();
  }*/
  if (error_display_duration.check())
  {
    error_showing = false;
    error_display_duration.reset();
  }
  if (last_error_code && error_recovery_timer.check())
  {
    error_recovery_routine();
    error_recovery_timer.reset();
  }
  if (serial_port_timer.check())
  {
    Serial.print("Still alive ... \n");
    serial_port_timer.reset();
  }

  update_vehicle_status();
}

void update_vehicle_status()
{
  unsigned long now = millis();
  if (vehicle_status == CHARGER_PLUGGED)
  { //check if charger is still plugged in
    if(now - charger_status.last_update > 2000)
    {
      vehicle_status = IGNITION_OFF; //FIXME
    }
  }
}

/////////////////// HANDLE CAN and related modules////////////////////////
void handle_CAN()
{
  if(Can0.available())
  {
    Can0.read(inFrame);
    if(inFrame.id == 0x18FF50E5)//Charger status report
    {
      charger_status.HV_Voltage = (float)(inFrame.data.bytes[0] * 256 + inFrame.data.bytes[1]) * 0.1;
      charger_status.HV_Current = (float)(inFrame.data.bytes[2] * 256 + inFrame.data.bytes[3]) * 0.1;
      charger_status.temperature_C = (int)inFrame.data.bytes[7] - 40;
      charger_status.byte4_value = inFrame.data.bytes[4];
      charger_status.byte5_value = inFrame.data.bytes[5];
      charger_status.byte6_value = inFrame.data.bytes[6];
      eval_charger_status();
      vehicle_status = CHARGER_PLUGGED;
      charger_status.NMI = true;
      charger_status.last_update = millis(); //now
      serial_printf("Charger: %.2f V, %.2f A\n", charger_status.HV_Voltage, charger_status.HV_Current);
      charger_timer.reset();
    }
    else if (inFrame.id == 0x1801D08F)//DCDC status report
    {
      dcdc_status.DCDC_Voltage = (float)(inFrame.data.bytes[0] * 256 + inFrame.data.bytes[1]) * 0.1;
      dcdc_status.DCDC_Current = (float)(inFrame.data.bytes[2] * 256 + inFrame.data.bytes[3]) * 0.1;
      dcdc_status.byte4_value = inFrame.data.bytes[4];
      dcdc_status.byte5_value = inFrame.data.bytes[5];
      //byte [6] is unused
      dcdc_status.temperature_C = (int)inFrame.data.bytes[7] - 60;
      dcdc_status.last_update = millis(); //now
    }
    else
    {
      serial_printf("Unknown CAN0 message with ID %8x is received\n", inFrame.id);
    }
  }
}


void eval_charger_status()
{
  charger_status.OK_to_charge = false;
  if (charger_status.byte4_value != 0)
  {
    Serial.print("Charger fault detected: \n");
    if(charger_status.protection.HW_protection)
      Serial.print("   HW protection is enabled.\n");
      
    if(charger_status.protection.Temp_protection )
      Serial.print("   Temperature protection is enabled.\n");
      
    if(charger_status.protection.input_volt_status == 1)
      Serial.print("   Input under-voltage detected.\n");
    else if(charger_status.protection.input_volt_status == 2)
      Serial.print("   Input over-voltage detected.\n");
    else if(charger_status.protection.input_volt_status == 3)
      Serial.print("   No input voltage detected.\n");
      
    if(charger_status.protection.output_under_voltage)
      Serial.print("   Output under voltage is detected.\n");
      
    if(charger_status.protection.output_over_voltage)
      Serial.print("   Output over voltage is detected.\n");
      
    if(charger_status.protection.output_over_current)
      Serial.print("   Output over current is detected.\n");
      
    if(charger_status.protection.output_short_circuit)
      Serial.print("   Output short-circuit is detected.\n");
  }
  else
  {
    charger_status.OK_to_charge = true; //FIXME: This needs to be checked against BMS status, temperature sensors, etc.
  }

  // Work status
  if(charger_status.work_status.comm_status)
    Serial.print("Charger receiver time-out.\n");
  
  if(charger_status.work_status.working_status == 0)
    Serial.print("Charger work status is undefined.\n");
  else if(charger_status.work_status.working_status == 2)
    Serial.print("Charger is stopped.\n");
  else if(charger_status.work_status.working_status == 3)
    Serial.print("Charger is standby/stopped.\n");

  if(charger_status.work_status.init_completion == 0)
    Serial.print("Charger initializations is not completed.\n");
}

void send_charger_msg()
{
  Serial.print("Sending CAN signal ... \n");
  outFrame.id = 0x1806E5F4;
  outFrame.length = 8;
  outFrame.extended = 1;
  outFrame.rtr=1;
  outFrame.data.bytes[0] = ((HV_MAX_CHG_VOLTAGE) * 10) >> 8;   //high byte of voltage set point
  outFrame.data.bytes[1] = ((HV_MAX_CHG_VOLTAGE) * 10) & 0xFF; //low byte of voltage set point
  outFrame.data.bytes[2] = (HV_MAX_CHG_CURRENT * 10) >> 8;   //high byte of current set point
  outFrame.data.bytes[3] = (HV_MAX_CHG_CURRENT * 10) & 0xFF;   //low byte of current set point
  outFrame.data.bytes[4] = 0x00;
  outFrame.data.bytes[5] = 0x00;
  outFrame.data.bytes[6] = 0x00;
  outFrame.data.bytes[7] = 0x00;
  Can0.sendFrame(outFrame); //send to pcs IPC can 
  charger_status.NMI = false;
  charger_timer.reset();
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

void setup_sensors(bool reset_error)
{
  if (reset_error)
  {
    last_error_code = NO_ERROR;
  }
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
      setup_sensors(true);
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
  setup_sensors(false);

  Can0.begin(CAN_BPS_250K);
  Can0.watchFor();
}

/////////////////// DISPLAY MONITOR HANDLING ////////////////////////
typedef enum
{
  PAGE_HOME_TEMP = 0,
  PAGE_HOME_DCDC,
  PAGE_ERROR,
  PAGE_INFO,
  PAGE_CHARGER
} display_page_t;
// keeps track last page shown on display
display_page_t lcd_page_index = PAGE_HOME_TEMP;

void serial_printf(const char* format, ...)
{
  char str_buffer[100]; //buffer for printf statements
  va_list args;
  va_start(args, format);
  vsnprintf(str_buffer, sizeof(str_buffer), format, args);
  va_end(args);
  Serial.print(str_buffer);
}

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
      serial_printf("No error found!");
    break;
    case MISSING_SENSORS:
      lcd.print("Missing sensors!");
      lcd.setCursor(0, 1);
      lcd.print("Expected: ");
      lcd.print(NUM_TEMP_SENSORS_EXPECTED);
      lcd.setCursor(0, 2);
      lcd.print("Found: ");
      lcd.print(num_temp_sensors_found);
      serial_printf("Missing sensors, Expected (%d), Found (%d)\n", NUM_TEMP_SENSORS_EXPECTED, num_temp_sensors_found);
    break;
    case BOGUS_SENSORS:
      lcd.print("Bogus sensors!");
      lcd.setCursor(0, 1);
      lcd.print("Expected: ");
      lcd.print(NUM_TEMP_SENSORS_EXPECTED);
      lcd.setCursor(0, 2);
      lcd.print("Found: ");
      lcd.print(num_temp_sensors_found);
      serial_printf("Bogus sensors, Expected (%d), Found(%d)\n", NUM_TEMP_SENSORS_EXPECTED, num_temp_sensors_found);
    break;
    default:
      lcd.print("Unknown error");
      serial_printf("Unknown error");
    break;
  }
  lcd_page_index = PAGE_ERROR;
  error_showing = true;
  error_display_duration.reset();
}

void update_display()
{
  char line_buffer[30];
  static bool display_flag = 0;
  static display_page_t which_home_page = PAGE_HOME_TEMP;
  if (page_switch_timer.check())
  {
    switch (which_home_page)
    {
      case PAGE_HOME_TEMP:
        //switch to DCDC only if there is a recent update
        which_home_page = (millis() - dcdc_status.last_update < 2000) ? PAGE_HOME_DCDC : PAGE_HOME_TEMP;
        page_switch_timer.interval(4500);
        break;
      case PAGE_HOME_DCDC:
        which_home_page = PAGE_HOME_TEMP;
        page_switch_timer.interval(1500);
        break;
    }
    page_switch_timer.reset();
  }
    
  if (vehicle_status != CHARGER_PLUGGED)
  {
    if (which_home_page == PAGE_HOME_TEMP)
      display_home_page_temperatures();
    else
      display_home_page_dcdc();
  }
  else 
  {
    display_charger_page();
  }
  
  //blinking star
  display_flag = !display_flag;
  lcd.setCursor(19, 0);
  lcd.print(display_flag ? "*": " ");
}

void display_charger_page()
{
  if (lcd_page_index != PAGE_CHARGER)
  {
    lcd.clear();
  }
  lcd_page_index = PAGE_CHARGER;
  char line_buffer[30];
  uint8_t row = 0, col = 0;
  lcd.setCursor(col, row);
  lcd.print("Charger plugged:");
  sprintf(line_buffer, "Vout: %.2f Volt", charger_status.HV_Voltage);
  row = 1; lcd.setCursor(col, row);
  lcd.print(line_buffer);
  sprintf(line_buffer, "Iout: %.2f Amp", charger_status.HV_Current);
  row = 2; lcd.setCursor(col, row);
  lcd.print(line_buffer);
  //Status line
  row = 3; lcd.setCursor(col, row);
  switch (charger_status.work_status.working_status){
    case 0:
      sprintf(line_buffer, "Status: Undefined");
      break;
    case 1:
      sprintf(line_buffer, "Status: Working");
      break;
    case 2:
      sprintf(line_buffer, "Status: Stopped");
      break;
    case 3:
      sprintf(line_buffer, "Status: Standby");
      break;
    default:
      sprintf(line_buffer, "Status: ???");
  }
  lcd.print(line_buffer);
}

void display_home_page_dcdc()
{
  if (lcd_page_index != PAGE_HOME_DCDC)
  {
    lcd.clear();
  }
  lcd_page_index = PAGE_HOME_DCDC;
  char line_buffer[30];
  uint8_t row = 0, col = 0;
  lcd.setCursor(0, 0);
  lcd.print("DCDC:");
  lcd.setCursor(0, 1);
  sprintf(line_buffer, "Vout: %.2f Volts", dcdc_status.DCDC_Voltage);
  lcd.print(line_buffer);
  lcd.setCursor(0, 2);
  sprintf(line_buffer, "Iout: %.2f Amps", dcdc_status.DCDC_Current);
  lcd.print(line_buffer);
  lcd.setCursor(0, 3);
  sprintf(line_buffer, "Temp: %d C", dcdc_status.temperature_C);
  lcd.print(line_buffer);  
}

void display_home_page_temperatures()
{
  if (lcd_page_index != PAGE_HOME_TEMP)
  {
    lcd.clear();
  }
  lcd_page_index = PAGE_HOME_TEMP;
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
