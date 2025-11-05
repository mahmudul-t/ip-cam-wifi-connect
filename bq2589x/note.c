#define BQ25895_ADDRESS 0x6A << 1  // I2C address of BQ25895

// Input Current Limit




					







//Watchdog Timer
#define WDT_REG							0x07
#define WDT								0x89 //WDT off

//IR THERMAL REGISTER
#define IR_THERMAL_REG					0x08
#define IR_THERMAL						0x03

//SHIP MODE
#define SHIP_MODE_REG					0x09
#define SHIP_MODE_ON					0x64
#define SHIP_MODE_OFF					0x44 //for init

//OTG VOLT CURRENT
#define OTG_VOLT_CURRENT_REG			0x0A
#define OTG_VOLT_CURRENT				0x93

// Charging Mode
#define CHARGING_MODE_REG				0x0B

//INPUT VOLT DPM REGISTER
#define INPUT_VOLT_DPM_REG				0x0D
#define INPUT_VOLT_DPM					0x18

// Charge & Voltage Status
#define ADC_VOLTAGE_REG 				0x0E

// VBUS VOLTAGE
#define VBUS_VOLTAGE_REG 				0x11

// Charge Current
#define ADC_CURRENT_REG 				0x12

#define SHORT_PRESS_THRESHOLD			1000
#define LONG_PRESS_THRESHOLD			1500

#define BATTERY_VOLTAGE_HISTORY_SIZE 5
#define BATTERY_VOLTAGE_TOLERANCE 0.15


void PMIC_BQ25895_Init()
{
	printThreadSafe("PMIC Setting INPUT_CURRENT_LIMIT_REG 0x%x \r\n", INPUT_CURRENT);
	#define INPUT_CURRENT_LIMIT_REG 		0x00
	#define INPUT_CURRENT 					0x3A
	BQ25895_Write(INPUT_CURRENT_LIMIT_REG, INPUT_CURRENT);


	printThreadSafe("PMIC Setting VOLT_DPM_OFFSET_REG 0x%x \r\n", VOLT_DPM_OFFSET);
	// Volt DPM Offset
	#define VOLT_DPM_OFFSET_REG 			0x01
	#define VOLT_DPM_OFFSET 				0x05
	BQ25895_Write(VOLT_DPM_OFFSET_REG, VOLT_DPM_OFFSET);



	printThreadSafe("PMIC Setting ADC_CONTROL_REG 0x%x \r\n", ADC_CONTROL);
	//ADC Control
	#define ADC_CONTROL_REG					0x02
	#define ADC_CONTROL						0xF0
	BQ25895_Write(ADC_CONTROL_REG, ADC_CONTROL);



	printThreadSafe("PMIC Setting OTG_CHARGER_REG 0x%x \r\n", OTG_CHARGER);
	//OTG & Charger
	#define OTG_CHARGER_REG					0x03
	#define OTG_CHARGER						0x10
	BQ25895_Write(OTG_CHARGER_REG, OTG_CHARGER);



	printThreadSafe("PMIC Setting FAST_CHARGE_CURRENT_LIMIT_REG 0x%x \r\n", FAST_CHARGE_CURRENT);
	// Fast Charge Current Limit Register
	#define FAST_CHARGE_CURRENT_LIMIT_REG 	0x04
	#define FAST_CHARGE_CURRENT 			0x10 //1024mA
	BQ25895_Write(FAST_CHARGE_CURRENT_LIMIT_REG, FAST_CHARGE_CURRENT);


	printThreadSafe("PMIC Setting PRE_CHARGE_TERMINATION_REG 0x%x \r\n", PRE_CHARGE_TERMINATION);
	// Pre Charge Termination
	#define PRE_CHARGE_TERMINATION_REG 		0x05
	#define PRE_CHARGE_TERMINATION 			0x30
	BQ25895_Write(PRE_CHARGE_TERMINATION_REG, PRE_CHARGE_TERMINATION);



	printThreadSafe("PMIC Setting CHARGE_VOLT_LIMIT_REG 0x%x \r\n", CHARGE_VOLT_LIMIT);
	// Charge Voltage Regulation Limit
	#define CHARGE_VOLT_LIMIT_REG 			0x06
	#define CHARGE_VOLT_LIMIT 				0x5E
	BQ25895_Write(CHARGE_VOLT_LIMIT_REG, CHARGE_VOLT_LIMIT);



	printThreadSafe("PMIC Setting WDT_REG 0x%x \r\n", WDT);
	BQ25895_Write(WDT_REG, WDT);

	
	printThreadSafe("PMIC Setting IR_THERMAL_REG 0x%x \r\n", IR_THERMAL);
	BQ25895_Write(IR_THERMAL_REG, IR_THERMAL);
	printThreadSafe("PMIC Setting SHIP_MODE_REG 0x%x \r\n", SHIP_MODE_OFF);
	BQ25895_Write(SHIP_MODE_REG, SHIP_MODE_OFF);
	printThreadSafe("PMIC Setting OTG_VOLT_CURRENT_REG 0x%x \r\n", OTG_VOLT_CURRENT);
	BQ25895_Write(OTG_VOLT_CURRENT_REG, OTG_VOLT_CURRENT);
	printThreadSafe("PMIC Setting INPUT_VOLT_DPM_REG 0x%x \r\n", INPUT_VOLT_DPM);
	BQ25895_Write(INPUT_VOLT_DPM_REG, INPUT_VOLT_DPM);
}