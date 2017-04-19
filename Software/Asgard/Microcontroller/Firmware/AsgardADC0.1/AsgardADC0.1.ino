/* Work in progress Asgard ADC Firmware 19/04/2017
 * This is not a release. Probably it does not compile.
   AsgardADC0.1.ino - Air Data Computer Firmware
   Firmware for Teensy 3.6 MCU.
   Please specify if the BT module is present. If BT is present uncomment the line "#define BT_PRESENT true".

   Created by JLJ and G.C.
   BasicAirData Team.
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

//#define BT_PRESENT true;
#include <AirDC.h>
#include <SD.h>
#include <SD_t3.h>
#include <i2c_t3.h> //Library for second I2C 
#include <SSC.h>  //Library for SSC series sensors, support two bus I2C
#define INPUT_SIZE 1024
#define DELIMITER '\n'      // Message delimiter. It must match with Android class one;

AirDC AirDataComputer(1);
const int chipSelect = BUILTIN_SDCARD; //HW pin for micro SD adaptor CS
int counter = 0;
int value = 0;
char input[INPUT_SIZE + 1];
char *ch;
bool endmsg = false;
int TsensorPin = A0;       // select the input pin for the Temperature sensor
double temperature = 0.0;
double dp = 0.0;
double pstatic = 0.0;
SSC diffp(0x28, 0);
//  create an SSC sensor with I2C address 0x28 on I2C bus 1
SSC absp(0x28, 1);
boolean sd_present = false;
//Accessory variables for Air Data calculations
double iTAS, ip1TAS, res, iof;
void setup()
{
  pinMode(TsensorPin, INPUT);                       // and set pins to input.
  ch = &input[0]; //Var init
#ifdef BT_PRESENT
  Serial1.begin(9600);// Begin the serial monitor at 9600 bps over BT module
#endif
#ifndef BT_PRESENT
  Serial.begin(57600);// Begin the serial monitor at 57600 bps over the USB
#endif
  Wire.begin(); // I2C Bus 0
  Wire1.begin(); //I2C Bus 1
  //Setup sensors parameters
  diffp.setMinRaw(0);
  diffp.setMaxRaw(16383);
  diffp.setMinPressure(-6984.760);
  diffp.setMaxPressure(6984.760);
  absp.setMinRaw(0);
  absp.setMaxRaw(16383);
  absp.setMinPressure(0.0);
  absp.setMaxPressure(160000.0);
}
void loop()
{
  delay(500);
#ifdef BT_PRESENT
  capcom();
#endif
#ifndef BT_PRESENT
  acquisition();
  computation();
  sendout();
#endif
}
void sendout(){
  Serial.println(AirDataComputer.OutputSerial(50)); //Send out formatted data
}
void computation() {
  // put your main code here, to run repeatedly:
  AirDataComputer._p = pstatic;
  AirDataComputer._qc = dp;
  AirDataComputer._RH = 0; //No sensor for RH, we are selecting dry air but the library will handle moist air if required
  AirDataComputer._TAT = temperature+273.15;//Total Air Temperature
  //Computation
  //Init
  AirDataComputer._T = temperature+273.15;
  AirDataComputer.RhoAir(1);// Calculates the air density
  AirDataComputer.Viscosity(2);// Calculates the dynamic viscosity, Algorithm 2 (UOM Pas1e-6)
  AirDataComputer.CalibrationFactor(1); //Calibration factor set to 1
  AirDataComputer.IAS(1); //Calculates IAS method 1
  AirDataComputer._TAS = AirDataComputer._IAS;
  AirDataComputer.Mach(1); //Calculates Mach No
  AirDataComputer.OAT(1); //Outside Air Temperature

  //Wild iteration
  iof = 1;
  while ((res > 0.005) || (iof < 100)) {
    AirDataComputer.RhoAir(1);// Calculates the air density
    AirDataComputer.Viscosity(2);// Calculates the dynamic viscosity, Algorithm 2 (UOM Pas1e-6)
    AirDataComputer.CalibrationFactor(2); //Update calibration fator vat at TAS
    AirDataComputer.IAS(1); //IAS
    AirDataComputer.CAS(1); //CAS
    AirDataComputer.TAS(1); //True Air Speed
    AirDataComputer.Mach(1); //Update Mach No
    iTAS = AirDataComputer._TAS; //Store TAS value
    AirDataComputer.OAT(1); //Update outside Air Temperature
    AirDataComputer.RhoAir(1);// Calculates the air density
    AirDataComputer.Viscosity(2);// Calculates the dynamic viscosity, Algorithm 2 (UOM Pas1e-6)
    AirDataComputer.CalibrationFactor(2); //Update calibration fator vat at TAS
    AirDataComputer.TAS(1); //Update TAS
    AirDataComputer.Mach(1); //Update Mach No
    AirDataComputer.OAT(1); //Update outside Air Temperature
    ip1TAS = AirDataComputer._TAS;
    res = abs(ip1TAS - iTAS) / iTAS;
    iof++;
  }
  //Uncorrected ISA Altitude _h
  AirDataComputer.ISAAltitude(1);
  AirDataComputer._d=8e-3;
  AirDataComputer.Red(1);
}
void capcom()
{
  if (Serial1.available()) // If the bluetooth has received any character
  {
    while (Serial1.available() && (!endmsg)) { // until (end of buffer) or (newline)
      *ch = Serial1.read();                    // read char from serial
      if (*ch == DELIMITER) {
        endmsg = true;                        // found DELIMITER
        *ch = 0;
      }
      else ++ch;                              // increment index
    }

    if ((endmsg) && (ch != &input[0]))        // end of (non empty) message !!!
    {
      char *command = strtok(input, ",");

      if (!strcmp(command, "$STR"))           // Received a command, for example "$STR,1"
      {
        command = strtok (NULL, ",");
        value = atoi(command);                // Read the value after the comma, for example "1". Integer conversion

        // If we receive "$STR,1" the ADC switch to sensor test mode. Starts tests and reports.
        if (value == 1) testme();

        counter = counter + value;
        if (counter > 1000) counter = 0;      // A reset, for the serial gamers :)
        //--------------------------------------------------------------------------

        // Answer with a string
        Serial1.print("$ANS,");
        Serial1.print(counter);
        Serial1.write(DELIMITER);
      }
    }
    if (endmsg) {
      endmsg = false;
      *ch = 0;
      ch = &input[0];                         // Return to first index, ready for the new message;
    }
  }
}
void testme()
{
  //Version with BT card, send the output to serial 1
  //Is SDCard present and working?
  Serial1.print("Checking for SD Card: ");
  sd_present = true;
  if (!SD.begin(chipSelect)) {
    Serial1.println("Card failed to init, or not present");
    sd_present = false;
  }
  if (sd_present == true) {
    Serial1.println("card initialized");
  }

  //Outside Temperature Sensor
  temperature = TMP36GT_AI_value_to_Celsius(analogRead(TsensorPin)); // read temperature
  Serial1.println("Outside Temperature Measurement");
  Serial1.print(temperature, 1);             // write temperature to Serial
  Serial1.println(" °C");
  Serial1.print("Raw sensor reading ");
  Serial1.print(analogRead(TsensorPin) * (3300.0 / 1024), 1);
  Serial1.println(" mV");
  //Is Differential Pressure sensor present and working? (First I2C bus)
  //Setup of the sensor parameters
  Serial1.println("Differential pressure sensor measurements");
  //  update pressure / temperature
  Serial1.print("update()\t");
  Serial1.println(diffp.update());
  // print pressure
  Serial1.print("pressure()\t");
  Serial1.println(diffp.pressure());
  // print raw pressure
  Serial1.print("pressure_Raw()\t");
  Serial1.println(diffp.pressure_Raw());
  // print temperature
  Serial1.print("temperature()\t");
  Serial1.println(diffp.temperature());
  delay(500);
  //Is Absolute Pressure sensor present and working?(Second I2C bus)
  Serial1.println("Absolute pressure sensor measurements");
  //  update pressure / temperature
  Serial1.print("update()\t");
  Serial1.println(absp.update());
  // print pressure
  Serial1.print("pressure()\t");
  Serial1.println(absp.pressure());
  // print raw pressure
  Serial1.print("pressure_Raw()\t");
  Serial1.println(absp.pressure_Raw());
  // print temperature
  Serial1.print("temperature()\t");
  Serial1.println(absp.temperature());
  delay(500);
}
void acquisition()
{
  //Outside Temperature Sensor
  temperature = TMP36GT_AI_value_to_Celsius(analogRead(TsensorPin)); // read temperature
 //Differential Pressure sensor
  diffp.update();
  dp=diffp.pressure();
  delay(10);
  //Absolute Pressure 
  absp.update();
  pstatic=absp.pressure();
}
double TMP36GT_AI_value_to_Celsius(int AI_value)
{ // Convert Analog-input value to temperature
  float Voltage;
  Voltage = AI_value * (3300.0 / 1024);         // Sensor value in mV:
  return ((Voltage - 750) / 10) + 25;           // Temperature according to datasheet: 750 mV @ 25 °C
  // 10 mV / °C
}
