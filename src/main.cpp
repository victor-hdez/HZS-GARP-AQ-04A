#include <Arduino.h>
#include <SPI.h>
#include <Wire.h> //comunicación I2C
//funcionalidades extras
#include <axp20x.h> //control de energía en placa mediante chip axp192 ":":"SE USARA EN UN FUTURO":":"
#include <TinyGPSPlus.h> //habilitación GPS
#include <SPIFFS.h> //salva de datos en flash
#include "mis_sensores.h" //el codigo de los sensores separado

TinyGPSPlus gps;
//puerto serial para el GPS
HardwareSerial GPS(1);
int day,month,year,hour,minute,second; //variable del GPS
float lng,lat; //coordenada del gps

//archivo para guardar los datos
File measurements;
int ROWS = 1; //usada para llevar conteo de la cantd de mediciones hechas

///////////////////////////////////////////////////
//////VARIABLES:PARA:EL:CONTROL:DEL:PROGRAMA://////
///////////////////////////////////////////////////

//PANTALLA OLED SH1106
volatile int interruption_happen = 0; //variable modificada al lanzarse una interrupcion
int tab; //controla que pestaña mostrar por pantalla
bool OLED_flag = 1; //controla el encendido(true) y apagado(false) de la pantalla
//TIEMPO DE MUESTREO
unsigned int sample = 10000; //MODIFIQUE EL TIEMPO DE MUESTREO DEL PROGRAMA AQUI !!!
unsigned int previus_millis = 0; //resetea el tiempo de muestreo

///////////////////////////////////////////////////
//////FUNCIONES:PARA:EL:CONTROL:DEL:PROGRAMA://////
///////////////////////////////////////////////////

//lanzada al presionar el boton de [SELECT]
void interruption();
//inicializa y comprueba el correcto acceso a la flash
void validate_FLASH();
void check_data_through_serial_monitor();
//promedia las mediciones de los alphasense y devuelve true al terminar
bool avg();
void write_on_FLASH();
void write_on_OLED();
//el SmartDelay del GPS?
static void smartDelay(unsigned long); 

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// S E T U P . ///////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup(){ 
	Serial.begin(9600); //puerto serie
	Wire.begin(); //comunicacion I2C
	GPS.begin(9600, SERIAL_8N1, 34, 12); // (baudios,modo de trabajo, rx, tx)
	modular_check(1,1,1,1); //inicializamos los sensores q se van a usar
	validate_FLASH();
	//SPIFFS.format(); //descomentarear para formatear la flash
	//check_data_through_serial_monitor();

	//Asigna una interrupción al boton de select de la placa
	attachInterrupt(digitalPinToInterrupt(38), interruption, RISING);
		
	////POR ÚLTIMO...Y PARA DEJARLO TODO LISTO////
	//cada vez q se inicia el programa el siguiente encabezado se añade en mediciones.txt
	//para hacer alusion al comienzo de una nueva sesión de mediciones
	measurements = SPIFFS.open("/mediciones.txt","a+");
	measurements.println();
	measurements.print("Fila; HORA; Latitud_GPS; Longitud_GPS;");
	measurements.print(" BME_temp; BME_humd; BME_Pres_Atmf;");
	measurements.print(" SCD30_CO2; SCD30_temp; SCD30_hum;");
	measurements.print(" SDS011_PM25; SDS011_PM10;");
	measurements.print(" conc_NO2; work_voltage; aux_voltage;");
	measurements.print(" conc_O3; work_voltage; aux_voltage;");
	measurements.print(" conc_CO; work_voltage; aux_voltage;");
	measurements.println(" conc_SO2; work_voltage; aux_voltage;");
	measurements.close();

	//Se recogen los primeros valores q tienen que salir por pantalla
	bme_temperature = bme.readTemperature();
	bme_humidity = bme.readHumidity();
	bme_pressure = bme.readPressure()/100.0F;
	scd30_co2 = scd30.getCO2();
	scd30_temperature = scd30.getTemperature();
	scd30_humidity = scd30.getHumidity();
	sds011.read(&sds011_pm25,&sds011_pm10);
	//ademas arranca siempre con ...
	OLED_flag = true; //la pantalla encendida
	tab = 1; //en la primera pestaña
	interruption_happen = 0; //la interrupcion desactivada
	//Ahora si nos vemos en el loop ...
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////// L O O P . ///////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop(){
	//Datos del GPS en tiempo real
	if(gps.date.isValid()){
		Serial.println("si ves esto por monitor.serial la fecha se validó cosa q no se habia logrado hasta ahora");
		day = gps.date.day();
		month = gps.date.month();
		year = gps.date.year();
		Serial.println("FECHA->" + String(day) + "/" + String(month) + "/" + String(year));
	}
	if(gps.time.isValid()){
		hour = ((gps.time.hour() - 4) < 0)?(gps.time.hour() + 20):(gps.time.hour() - 4);
		minute = gps.time.minute();
		second = gps.time.second();
		Serial.println("TIME->"+ String(hour) + ":" + String(minute) + ":" + String(second));
	}
	if(gps.location.isValid()){
		lat = gps.location.lat();
		lng = gps.location.lng();
		Serial.println("longitud->" + String(lng));
		Serial.println("latitud->" + String(lat));
	}
	//animacion del mensaje "calibrando" mostrado en la pestaña de los alphasense 
	calibrando = (calibrando == "  calibrando ...")?("  calibrando "):(calibrando + "."); 
	smartDelay(1000);

	if((millis() - previus_millis) >= sample){
		Serial.println("medicion realizada al -> "+ String(millis()));
		//promedia 12 veces antes de sacar un resultado
		if(avg()){ //las doce iteraciones se completaran en 120 segundos (1 iteracion cada 10 seg).
			//si el promedio termina devuelve true por lo q entramos a este if y medimos tambien
			//valores del BME
			bme.takeForcedMeasurement();
			bme_temperature = bme.readTemperature();
			bme_humidity = bme.readHumidity();
			bme_pressure = bme.readPressure()/100.0F;
			//valores del SCD30
			scd30_co2 = scd30.getCO2();
			scd30_temperature = scd30.getTemperature();
			scd30_humidity = scd30.getHumidity();
			//valores del SDS011
			sds011.read(&sds011_pm25,&sds011_pm10);					
						
			write_on_FLASH();
			Serial.println("iteracion escrita en la flash al -> " + String(millis()));
		} 
		//al final de la medicion siempre resetee el tiempo de muestreo
		previus_millis = millis(); 
	}
	//todo lo q pasa tiene q ser mostrado continuamente por pantalla por lo q hay q...
	write_on_OLED();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// F U N C T I O N S . //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void interruption(){
	interruption_happen++;
	//Serial.println((interruption_happen)+" rebounds.");  
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void validate_FLASH(){  //PARTE DE CODIGO DEDICADA A CARGAR Y VERIFICAR EL ARCHIVO ALOJADO EN LA FLASH
	if(!SPIFFS.begin(true)){
		Serial.println("ERROR -> Flash File System not found.");
		if(display){
			u8g2.clearBuffer();
			u8g2.setCursor(0,32);
			u8g2.print("ERROR RUNNING:");
			u8g2.setCursor(0,64);
			u8g2.print("Flash File System.");
			u8g2.sendBuffer();
			//presionar select para seguir ->implementar con el dispositivo original
		}
	}
	//Abre el archivo que se subió desde la carpeta "data"
	measurements = SPIFFS.open("/mediciones.txt", "a+");
	if(!measurements){
		Serial.println("ERROR -> Trying to open mediciones.txt");
		if(display){
		u8g2.clearBuffer();
		u8g2.setCursor(0,32);
		u8g2.print("ERROR TRYING TO REACH:");
		u8g2.setCursor(0,64);
		u8g2.print("mediciones.txt");
		u8g2.sendBuffer();
		//presionar select para seguir ->implementar con el dispositivo original
		}
	}
	measurements.close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void check_data_through_serial_monitor(){
		measurements = SPIFFS.open("/mediciones.txt","r"); //abrir archivo en modo lectura
		//Imprimir contenido del archivo por monitor serial
		Serial.println("Q contiene actualmente mediciones.txt?");
		while(measurements.available()){
		Serial.write(measurements.read());
		}
		measurements.close();
		Serial.println("Final del archivo de mediciones");

		//decir ademas por consola q este archivo contiene x iteraciones por lo q a un tempo de 2 min
		//...el archivo estuvo midiendo por tanto tiempo
		/* TODO 
			* asd
			* asd
		*/
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool avg(){
	//recoge y guarda los valores recien medidos por los alphasense
	get_alphasense_values();
	sumatoria_NO2 += abs(conc_NO2);
	sumatoria_O3 += abs(conc_O3);
	sumatoria_CO += abs(conc_CO);
	sumatoria_SO2 += abs(conc_SO2);
	i++; //realiza una iteración

	if(i == 12){ //se iteró lo suficiente como para calcular el promedio?
		if(millis() > 130000){ //se supero el periodo de acondicionamiento de los alphasense
			promedio_NO2 = (promedio_NO2 + sumatoria_NO2)/13.00;
			promedio_O3 = (promedio_O3 + sumatoria_O3)/13.00;
			promedio_CO = (promedio_CO + sumatoria_CO)/13.00;
			promedio_SO2 = (promedio_SO2 + sumatoria_SO2)/13.00;
		}else{ //caso especial para el primer promedio q se realizara
			promedio_NO2 = sumatoria_NO2/12.00;
			promedio_O3 = sumatoria_O3/12.00;
			promedio_CO = sumatoria_CO/12.00;
			promedio_SO2 = sumatoria_SO2/12.00;
		}
		Serial.print("se completo un promedio a los: " + String(millis()));
		//resetea las variables
		sumatoria_NO2 = 0.00;
		sumatoria_O3 = 0.00;
		sumatoria_CO = 0.00;
		sumatoria_SO2 = 0.00;
		i = 0;
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void write_on_FLASH(){  //esta escritura responde a un formato CSV
		measurements = SPIFFS.open("/mediciones.txt", "a+");
		measurements.print(ROWS++);measurements.print("; ");
		//GPS
		measurements.print(hour);measurements.print(":");
		measurements.print(minute);measurements.print(":");
		measurements.print(second);measurements.print("; ");
		measurements.print(lat);measurements.print("; ");
		measurements.print(lng);measurements.print("; ");
		//BME   
		measurements.print(bme_temperature);measurements.print("; ");
		measurements.print(bme_humidity);measurements.print("; ");
		measurements.print(bme_pressure);measurements.print("; ");
		//SCD30   
		measurements.print(scd30_co2);measurements.print("; ");
		measurements.print(scd30_temperature);measurements.print("; ");
		measurements.print(scd30_humidity);measurements.print("; ");
		//SDS011
		measurements.print(sds011_pm25);measurements.print("; ");
		measurements.print(sds011_pm10);measurements.print("; ");
		//ALPHASENSE NO2 concentración + voltaje de trabajo + voltaje auxiliar 
		measurements.print(promedio_NO2);measurements.print("; ");
		measurements.print(voltage_value_OP1);measurements.print("; ");
		measurements.print(voltage_value_OP2);measurements.print("; ");
		//ALPHASENSE O3 concentración + voltaje de trabajo + voltaje auxiliar 
		measurements.print(promedio_O3);measurements.print("; ");
		measurements.print(voltage_value_OP3);measurements.print("; ");
		measurements.print(voltage_value_OP4);measurements.print("; ");
		//ALPHASENSE CO concentración + voltaje de trabajo + voltaje auxiliar 
		measurements.print(promedio_CO);measurements.print("; ");
		measurements.print(voltage_value_OP5);measurements.print("; ");
		measurements.print(voltage_value_OP6);measurements.print("; ");
		//ALPHASENSE SO2 concentración + voltaje de trabajo + voltaje auxiliar 
		measurements.print(promedio_SO2);measurements.print("; ");
		measurements.print(voltage_value_OP7);measurements.print("; ");
		//este es el ultimo dato de la fila asi q lleva un salto de linea
		measurements.print(voltage_value_OP8);measurements.println(";");
		//termine de escribir
		measurements.close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void write_on_OLED(){	
	if(interruption_happen > 0){ //primero comprueba si se lanzo alguna interrupcion
		(tab == 0)?(OLED_flag = true, tab++):((tab == 5)?(OLED_flag = false, tab = 0):(tab++));
		interruption_happen = 0; //la reseteamos para poder volver a usarla luego
	}

	if(OLED_flag == true){ //si la pantalla no esta ahorrando bateria metete aqui a despliega datos

		u8g2.sleepOff(); //despierta la pantalla
		u8g2.clearBuffer(); //limpia la oled

		if(tab == 1){ //muestra el menu del BME280 
			u8g2.setCursor(0, 16); u8g2.print("::::::BME280::::::"); // imprime el titular de la pestaña
			u8g2.setCursor(0, 32); u8g2.printf("Tempt:  %.1f \260C%", bme_temperature);
			u8g2.setCursor(0, 48); u8g2.printf("Humd:   %.1f %%", bme_humidity);
			u8g2.setCursor(0, 64); u8g2.printf("Pres.A: %0.f hPa%", bme_pressure);
			u8g2.sendBuffer(); // actualiza la pantalla con los nuevos valores
		}

		if(tab == 2){ //muestra el menu del SCD30
			u8g2.setCursor(0, 16); u8g2.print(":::::::SCD30::::::");
			u8g2.setCursor(0, 32); u8g2.printf("CO2:    %0.f ppm%", scd30_co2);
			u8g2.setCursor(0, 48); u8g2.printf("Tempt:  %.1f \260C%", scd30_temperature);
			u8g2.setCursor(0, 64); u8g2.printf("Humd:   %.1f %%", scd30_humidity);
			u8g2.sendBuffer();
		}
			
		if(tab == 3){ //muestra el menu del SDS011
			u8g2.setCursor(0, 16); u8g2.print("::::::SDS011::::::");
			u8g2.setCursor(0, 32); u8g2.printf("PM2.5:  %.1f ug/m3%", sds011_pm25);
			u8g2.setCursor(0, 48); u8g2.printf("PM10:   %.1f ug/m3%", sds011_pm10);
			u8g2.sendBuffer();
		}
				
		if(tab == 4){ //muestra el menu de los alphasense
			if(millis() > 130000){ //solo mostramos si ya paso mas de dos minutos de encendido de los sensores
				u8g2.setCursor(0, 16); u8g2.printf("NO2: %.0f ppb%", promedio_NO2);
				u8g2.setCursor(0, 32); u8g2.printf("O3:  %.0f ppb%", promedio_O3);
				u8g2.setCursor(0, 48); u8g2.printf("CO:  %.0f ppb%", promedio_CO);
				u8g2.setCursor(0, 64); u8g2.printf("SO2: %.0f ppb%", promedio_SO2);
			}else{ //decimos q estamos en proceso de calibración
				u8g2.setCursor(0, 25); u8g2.print("    Alphasense  ");
				u8g2.setCursor(0, 48); u8g2.print(calibrando);
			}
			u8g2.sendBuffer();
		}
				
		if(tab == 5){ //muestra el menu del GPS
			u8g2.setCursor(0, 16); u8g2.print("::::::::GPS:::::::");
			u8g2.setCursor(0, 32); u8g2.print("Time: ");
			(hour <= 9)?(u8g2.printf("0%d%", hour)):(u8g2.printf("%d%", hour)); //escribe hora
			(minute <= 9)?(u8g2.printf(":0%d%", minute)):(u8g2.printf(":%d%", minute)); //escribe minuto
			(second <= 9)?(u8g2.printf(":0%d%", second)):(u8g2.printf(":%d%", second)); //escribe segundos
			u8g2.setCursor(0, 48); u8g2.printf("Lat: %.4f%", lat);
			u8g2.setCursor(0, 64); u8g2.printf("Lng: %.4f%", lng);
			u8g2.sendBuffer();
		}

	}else u8g2.sleepOn();//significa q la OLED_flag == false por lo tanto manten la pantalla dormida
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void smartDelay(unsigned long ms){
	unsigned long start = millis();
	do{
		while (GPS.available())
		gps.encode(GPS.read());
	} while (millis() - start < ms);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
