////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// L I B R A R I E S . //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <U8g2lib.h> //libreria universal de pantallas, con controlador SSH1106 para usar nuestra OLED
#include <Adafruit_BME280.h> //sensor de T/HR/PA
#include "SparkFun_SCD30_Arduino_Library.h" //sensor de CO2
#include <SDS011.h> //sensor de material particulado [pm2.5][pm10]
#include <string.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////// S T A T E M E N T S . //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//objeto de la OLED. Direccion I2C por defecto 0x3C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
bool display = false; //existe una pantalla para visualizar datos?
String calibrando = "  calibrando "; //texto mostrado por pantalla cuando no está disponible una medición

Adafruit_BME280 bme; // Direccion I2C por defecto 0x76
float bme_temperature;
float bme_humidity;
float bme_pressure;

SCD30 scd30;
float scd30_co2;
float scd30_temperature;
float scd30_humidity;

SDS011 sds011;
float sds011_pm25;
float sds011_pm10;

//declaración de los PINES para los sensores electrolíticos de alphasense.
const int Analog_channel_pin_OP1 = 2; //SN1-NO2 TRABAJO
const int Analog_channel_pin_OP2 = 25; //SN1-NO2 AUXILIAR
const int Analog_channel_pin_OP3 = 13; //SN2-O3 TRABAJO
const int Analog_channel_pin_OP4 = 33; //SN2-O3 AUXILIAR
const int Analog_channel_pin_OP5 = 36; //SN3-CO TRABAJO
const int Analog_channel_pin_OP6 = 32; //SN3-CO AUXILIAR
const int Analog_channel_pin_OP7 = 39; //SN4-SO2 TRABAJO
const int Analog_channel_pin_OP8 = 15; //SN4-SO2 AUXILIAR

/////////////////////////////////////////////////
//////VARIABLES:DE:LOS:SENSORES:ALPHASENSE://////
/////////////////////////////////////////////////

int ADC_VALUE_OP1 = 0, ADC_VALUE_OP2 = 0, ADC_VALUE_OP3 = 0, ADC_VALUE_OP4 = 0;
int ADC_VALUE_OP5 = 0, ADC_VALUE_OP6 = 0, ADC_VALUE_OP7 = 0, ADC_VALUE_OP8 = 0;
float voltage_value_OP1 = 0, voltage_value_OP2 = 0;
float voltage_value_OP3 = 0, voltage_value_OP4 = 0;
float voltage_value_OP5 = 0, voltage_value_OP6 = 0;
float voltage_value_OP7 = 0, voltage_value_OP8 = 0;
//constantes de calibración for SN1 o NO2
float SN1_TOTAL_WE = 295, SN1_TOTAL_AE = 300, SN1_SENSITIVITY = 0.188121;
//constantes de calibración for SN2 o O3
float SN2_TOTAL_WE = 399, SN2_TOTAL_AE = 401, SN2_SENSITIVITY = 0.415078;
//constantes de calibración for SN3 o CO
float SN3_TOTAL_WE = 284, SN3_TOTAL_AE = 276, SN3_SENSITIVITY = -0.29346;
//constantes de calibración for SN4 o SO2
float SN4_TOTAL_WE = 277, SN4_TOTAL_AE = 288, SN4_SENSITIVITY = -0.279517;
//afectación por temperatura
float nt_NO2, nt_O3, nt_CO, nt_SO2;
//voltajes
float voltage_NO2, voltage_O3, voltage_CO, voltage_SO2;
//concentraciones
float conc_NO2, conc_O3, conc_CO, conc_SO2;
//variables usadas en el calculo del promedio
float promedio_NO2= 0.00, promedio_O3= 0.00, promedio_CO= 0.00, promedio_SO2= 0.00;
//variables usadas en el calculo del promedio
float sumatoria_NO2 = 0.00, sumatoria_O3 = 0.00, sumatoria_CO = 0.00, sumatoria_SO2 = 0.00;
int i= 0; //iteracion del promedio
float compensation = 0.95; //añadiendo un -5% de error a la medición

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// F U N C T I O N S . //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void modular_check(bool OLED, bool BME280, bool SCD30, bool SDS011){
  if(OLED){
    if(u8g2.begin()){
      u8g2.setFont(u8g2_font_7x14_tf);
      u8g2.setCursor(0,32);
      u8g2.print(" HZS-GARP-AQ-04A ");
      u8g2.sendBuffer();
      display = true;
    }else{
      Serial.println("ERROR -> Could not find the OLED, check wiring!");
    }
  }

  if(BME280){
    if(!bme.begin(0x76)){
      Serial.println("ERROR -> Could not find the BME280 sensor, check wiring!");
      if(display){
        u8g2.clearBuffer(); 
        u8g2.setCursor(0,32);
        u8g2.print("BME280 ERROR!");
        u8g2.sendBuffer();
        //presionar select para seguir ->implementar con el dispositivo original
      }
    }
  }

  if(SCD30){
    if(!scd30.begin(Wire, false)){
      Serial.println("ERROR -> Could not find the SCD30 sensor, check wiring!");
      if(display){
        u8g2.clearBuffer();
        u8g2.setCursor(0,32);
        u8g2.print("SCD30 ERROR!");
        u8g2.sendBuffer();
        //presionar select para seguir ->implementar con el dispositivo original
      }
    }
  }

  if(SDS011){
    #ifdef ESP32
    //puerto serial para el Nova SDS011
    HardwareSerial port(0);
    #endif
    sds011.begin(&port);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void get_alphasense_values(){
  //la tempt modifica los calculos asi q primero chequea eso 
  (bme_temperature > 30)?(nt_NO2=1.9,nt_O3=2.1,nt_CO=-1.5,nt_SO2=0.4):(nt_NO2=1.8,nt_O3=2.0,nt_CO=-0.9,nt_SO2=0.4);
  //recoge los valores//
  ADC_VALUE_OP1 = analogRead(Analog_channel_pin_OP1);//SN1-NO2 trabajo
  voltage_value_OP1 = (ADC_VALUE_OP1 * 0.805);
  ADC_VALUE_OP2 = analogRead(Analog_channel_pin_OP2);//SN1-NO2 AUXILIAR
  voltage_value_OP2 = (ADC_VALUE_OP2 * 0.805);
  //
  ADC_VALUE_OP3 = analogRead(Analog_channel_pin_OP3);//SN2-O3 trabajo
  voltage_value_OP3 = (ADC_VALUE_OP3 * 0.805);
  ADC_VALUE_OP4 = analogRead(Analog_channel_pin_OP4);//SN2-O3 AUXILIAR
  voltage_value_OP4 = (ADC_VALUE_OP4 * 0.805);
  // 
  ADC_VALUE_OP5 = analogRead(Analog_channel_pin_OP5);//SN3-CO trabajo
  voltage_value_OP5 = (ADC_VALUE_OP5 * 0.805);
  ADC_VALUE_OP6 = analogRead(Analog_channel_pin_OP6);//SN3-CO AUXILIAR
  voltage_value_OP6 = (ADC_VALUE_OP6 * 0.805);
  //
  ADC_VALUE_OP7 = analogRead(Analog_channel_pin_OP7);//SN4-SO2 trabajo
  voltage_value_OP7 = (ADC_VALUE_OP7 * 0.805);
  ADC_VALUE_OP8 = analogRead(Analog_channel_pin_OP8);//SN4-SO2 AUXILIAR
  voltage_value_OP8 = (ADC_VALUE_OP8 * 0.805);
  //calcula las concentraciones
  voltage_NO2 = (voltage_value_OP1-SN1_TOTAL_WE)-nt_NO2*(voltage_value_OP2-SN1_TOTAL_AE);
  conc_NO2 = (voltage_NO2/SN1_SENSITIVITY)* compensation;
  //
  voltage_O3 = (voltage_value_OP3-SN2_TOTAL_WE)-nt_O3*(voltage_value_OP4-SN2_TOTAL_AE);
  conc_O3 = ((voltage_O3-voltage_NO2)/(SN1_SENSITIVITY+SN2_SENSITIVITY))* compensation;
  //
  voltage_CO = (voltage_value_OP5-SN3_TOTAL_WE)-nt_CO*(voltage_value_OP6-SN3_TOTAL_AE);
  conc_CO = (voltage_CO/SN3_SENSITIVITY)* compensation;
  //
  voltage_SO2 = (voltage_value_OP7-SN4_TOTAL_WE)-nt_SO2*(voltage_value_OP8-SN4_TOTAL_AE);
  conc_SO2 = (voltage_SO2/SN4_SENSITIVITY)* compensation;
}



























































































































































































/*

void inicializa_OLED();
void inicializa_BME280();
void iniciliza_SCD30();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void inicializa_OLED(){
  if(u8g2.begin()){  //Inicializa OLED
    u8g2.setFont(u8g2_font_7x14_tf); // Establece el formato y el tamaño del texto
    u8g2.setCursor(0,32); // Establece la posición del cursor al inicio de la pantalla
    u8g2.print(" HZS-GARP-AQ-04A "); // Imprime el nombre del dispositivo en la pantalla
    u8g2.sendBuffer(); // Muestra el contenido en la pantalla
    //ya lo q viene abajo no es necesario pq el sistema se demora actualmente en el setup y por tanto da tiempo ver el logo
    //delay(2000); //espera 3 segundos para q de chance a q se vea el logo del dispositivo
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void inicializa_BME280(){
    if(!bme.begin(0x76)){ //Verifica el BME280
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
        u8g2.clearBuffer(); //Limpia OLED
        u8g2.setCursor(0,32);
        u8g2.print("BME280 ERROR!"); //di por pantalla q hubo error inciando el sensor
        u8g2.sendBuffer();
        while (1);
    }
    // Especifica que vas a trabajar este sensor en modo forzado y fija el tiempo de analisis y filtrado a 2seg
    bme.setSampling(Adafruit_BME280::MODE_FORCED, // Force reading after delayTime
                  Adafruit_BME280::SAMPLING_X1, // Temperature sampling set to 1 sec
                  Adafruit_BME280::SAMPLING_X1, // Pressure sampling set to 1 sec
                  Adafruit_BME280::SAMPLING_X1, // Humidity sampling set to 1 sec
                  Adafruit_BME280::FILTER_OFF   // Filter off - immediate 100% step response
                  );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void iniciliza_SCD30(){
    if (scd30.begin(Wire, false) == false){ //Verifica el SCD30
        Serial.println("Could not find a valid SCD30 sensor, check wiring!");
        u8g2.clearBuffer(); //Limpia OLED
        u8g2.setCursor(0,32);
        u8g2.print("SCD30 ERROR!"); //di por pantalla q hubo error inciando el sensor
        u8g2.sendBuffer();
        while (1);
  }
}
*/

/*
		//animacion en tiempo real para que el calibrando se vea bonito
		if(calibrando == "  calibrando ..."){
				calibrando = "  calibrando ";
		}else{
				calibrando += ".";
		}
*/

/*
		if(tab == 0){
			OLED_flag = true;
			tab++;
		}else if(tab == 5){
			OLED_flag = false;
			tab = 0;
		}else tab++;
*/

/*
if(hour <= 9 && hour >= 0){
				u8g2.print("0");
				u8g2.printf("%d%", hour);
			}else{
				u8g2.printf("%d%", hour);
			} 

u8g2.print(":");
			if(minute <= 9){
				u8g2.print("0");
			}
			u8g2.printf("%d%", minute);

u8g2.print(":");
			if(second <= 9){
				u8g2.print("0");
			}
			u8g2.printf("%d%", second);
*/