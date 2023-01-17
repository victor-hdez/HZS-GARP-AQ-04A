#include <Arduino.h>
#include <SPI.h>

#include <SPIFFS.h> //para almacenar valores

#include <TinyGPSPlus.h> //libreria para usar el GPS

#include <SoftwareSerial.h> //libreria para la comunicacion UART
#include <Wire.h> //para la comunicacion I2C
#include <axp20x.h> //libreria del ship axp192 q se encarga de controlar los suministros en la placa
#include <U8g2lib.h> //libreria universal de pantallas, incluye controlador SSH1106

#include <Adafruit_BME280.h> //para trabajar con el sensor de T/HR/PA
#include "SparkFun_SCD30_Arduino_Library.h" //libreria  del sensor de CO2
#include <SDS011.h> // libreria del sensor de material particulado

#ifdef ESP32
//puerto serial para el SDS011
HardwareSerial port(0); 
#endif

TinyGPSPlus gps;
//puerto serial para el gps
HardwareSerial GPS(1);

//creando un archivo para guardar los datos
File mediciones;

//objeto de la OLED. Si no se especifica direccion se coge por defecto la 0x3C como en este caso
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//objeto del sensor BME. Si no se especifica direccion se coge por defecto la 0x76 como en  este caso
Adafruit_BME280 bme;
//objeto del sensor de CO2
SCD30 scd30;
//objeto del sensor SDS011
SDS011 sds011;

//especifica 3 variables para las mediciones del BME
float bme_temperature;
float bme_humidity;
float bme_pressure;
//especifica 3 variables para las mediciones del SCD30 
float scd30_co2;
float scd30_temperature;
float scd30_humidity;
//especifica 2 variables para las mediciones del SDS011
float sds011_pm25;
float sds011_pm10;
//variables del GPS
int day,month,year,hour,minute,second;
float lng,lat;

//variables de control del programa
int tab; //variables q va a controlar las difrentes pestañas q enseña la oled
volatile bool flag_OLED; //bandera para controlar el encendido y apagado de la pantalla
unsigned int previus_millis = 0; //variable para poder fijar un tiempo de muestreo forzado

//declaracion de las funciones q se van a usar
void interrupcion(); //funcion q dice q es lo q hay q hacer cuando se aprieta select
static void smartDelay(unsigned long ms); //para el GPS
void escribe_en_OLED(); //funcion q muestra los datos por pantalla
void escribe_en_FLASH(); //funcion para guardar los datos en mediciones.txt

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// S E T U P . /////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup(){ 
  Serial.begin(9600); //Inicializa el puerto serie
  pinMode(13, OUTPUT); //Inicializamos el pin integrado 13 como salida
  Wire.begin(); //iniciliza la comunicacion I2C
  sds011.begin(&port); //inicializa el SDS011
  GPS.begin(9600, SERIAL_8N1, 34, 12); // (baudios,modo de trabajo,rx, tx)

  u8g2.begin(); //Inicializa OLED
  u8g2.clearBuffer(); //Limpia OLED
  u8g2.setFont(u8g2_font_7x14_tf); // Establece el formato y el tamaño del texto
  u8g2.setCursor(0,32); // Establece la posición del cursor al inicio de la pantalla
  u8g2.print(" HZS-GARP-AQ-04A "); // Imprime el nombre del dispositivo en la pantalla
  u8g2.sendBuffer(); // Muestra el contenido en la pantalla
  //ya lo q viene abajo no es necesario pq el sistema se demora en el setup
  //delay(2000); //espera 3 segundos para q de chance a q se vea el logo del dispositivo 

  //A PARTIR DE ESTE PUNTO EMPIEZA LA VERIFICACION DE SENSORES
  if (!bme.begin(0x76)) { //Verifica el BME280
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

  if (scd30.begin(Wire, false) == false){ //Verifica el SCD30
    Serial.println("Could not find a valid SCD30 sensor, check wiring!");
    u8g2.clearBuffer(); //Limpia OLED
    u8g2.setCursor(0,32);
    u8g2.print("SCD30 ERROR!"); //di por pantalla q hubo error inciando el sensor
    u8g2.sendBuffer();
    while (1);
  }

  //PARTE DE CODIGO DEDICADA A CARGAR Y VERIFICAR EL ARCHIVO DE LA FLASH
  if (!SPIFFS.begin(true)) {
    Serial.println("Flash File System ERROR RUNNING.");
    u8g2.clearBuffer(); //Limpia OLED
    u8g2.setCursor(0,32);
    u8g2.print("ERROR RUNNING:");
    u8g2.setCursor(0,64);
    u8g2.print("Flash File System.");
    u8g2.sendBuffer();
    return;
  }
  //Abre el archivo que se subió desde la carpeta "data"
  mediciones = SPIFFS.open("/mediciones.txt");
  //verifica si se puede abrir el txt
  if (!mediciones) {
    Serial.println("ERROR trying to open mediciones.txt");
    u8g2.clearBuffer(); //Limpia OLED
    u8g2.setCursor(0,32);
    u8g2.print("ERROR TRYING TO REACH:");
    u8g2.setCursor(0,64);
    u8g2.print("mediciones.txt");
    u8g2.sendBuffer();
    return;
  }
  mediciones.close();

  //SPIFFS.format(); //para formatear la memoria descomentarear esto

  //CHEQUEANDO LO Q HAY EN EL ARCHIVO POR MONITOR SERIAL
  //ESTO SE PUEDE COMENTAREAR SI NO SE QUIERE USAR
  /*
  //Imprimir contenido del archivo por monitor serial
  Serial.println("  Q contiene actualmente mediciones.txt?");
  while (mediciones.available()) {
  Serial.write(mediciones.read());
  }
  //Detiene el proceso
  mediciones.close();
  Serial.println("Final del archivo de mediciones");
  */

  //cada vez q se inicia el programa el siguiente encabezado se le añade a mediciones.txt 
  //para hacer alusion al comienzo de una nueva sesion de mediciones
  mediciones = SPIFFS.open("/mediciones.txt","a+");
  mediciones.println();
  mediciones.println("HORA;Latitud_GPS;Longitud_GPS;BME_temp;BME_humd;BME_Pres_Atmf;SCD30_CO2;SCD30_temp;SCD30_hum;SDS011_PM25;SDS011_PM10;");
  mediciones.close();
  
  //Por último... 
  ////DEJALO TODO LISTO PARA BRINCAR HACIA EL LOOP////
  // Asigna una interrupcion al boton de select de la placa
  attachInterrupt(digitalPinToInterrupt(38), interrupcion, RISING);

  // Reduce el formato y el tamaño de texto para q el menu se vea bien
  // Y recoge los primeros valores q tienen que salir por pantalla
  u8g2.clearDisplay();
  u8g2.setFont(u8g2_font_7x14_tf);
  bme_temperature = bme.readTemperature();
  bme_humidity = bme.readHumidity();
  bme_pressure = bme.readPressure()/100.0F;

  scd30_co2 = scd30.getCO2();
  scd30_temperature = scd30.getTemperature();
  scd30_humidity = scd30.getHumidity();

  sds011.read(&sds011_pm25,&sds011_pm10);

  //arranca siempre con la primera pestaña y la pantalla encendida
  flag_OLED = true;
  tab = 1;
  // Ahora si nos vemos en el loop
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////// L O O P . //////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop(){
  //Vamos a hacer un apartado para recoger los datos del GPS en tiempo real
  if(gps.date.isValid()){ //recoge la fecha
    day = gps.date.day();
    month = gps.date.month();
    year = gps.date.year();
    }
    if(gps.time.isValid()){ //recoge la hora
    u8g2.setCursor(0, 48);
    hour = (gps.time.hour()-5);
    minute = gps.time.minute();
    second = gps.time.second();
    }
    if(gps.location.isValid()){ //recoge las coordenadas
      u8g2.setCursor(0, 64);
      lat = gps.location.lat();
      lng = gps.location.lng();
    }

    smartDelay(1000);

  // establezca el tiempo de muestreo forzado en el resultado del siguiente if
  if((millis()-previus_millis) >= 60000){ //60000milisegundos = 1 min de tiempo de muestreo
    //recoge los valores del BME
    bme.takeForcedMeasurement(); //fuerza una medicion
    bme_temperature = bme.readTemperature();
    bme_humidity = bme.readHumidity();
    bme_pressure = bme.readPressure()/100.0F;

    //recoge los valores del SCD30
    scd30_co2 = scd30.getCO2();
    scd30_temperature = scd30.getTemperature();
    scd30_humidity = scd30.getHumidity();

    //recoge los valores del SDS011
    sds011.read(&sds011_pm25,&sds011_pm10);

    //guardalo todo en la flash
    escribe_en_FLASH();
    
    previus_millis = millis(); //actualiza la variable de tiempo
    digitalWrite(13,!(digitalRead(13))); //nueva medicion = a cambio de estado en el pin
  } 

  //si la pantalla no esta ahorrando bateria metete aqui a despliega datos
  if(flag_OLED == true){
    u8g2.sleepOff(); //despierta la pantalla
    escribe_en_OLED();
  } else if( flag_OLED == false){
    u8g2.sleepOn(); //manten dormida la pantalla
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// F U N C T I O N S . /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void interrupcion(){
  if(tab == 0){
    flag_OLED = true;
    tab++;
  }else if(tab == 1){
    tab++;
  }else if(tab == 2){
    tab++;
  }else if(tab == 3){
    tab++;
  }else if(tab == 4){
    flag_OLED = false;
    tab = 0;
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////

static void smartDelay(unsigned long ms){
  unsigned long start = millis();
  do
  {
    while (GPS.available())
      gps.encode(GPS.read());
  } while (millis() - start < ms);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

void escribe_en_OLED(){
  if(tab == 1){//muestra el menu del BME280
    //muestra los valores por oled 
    u8g2.clearBuffer(); //primero limpia la oled
    //despues escribe los valores 
    u8g2.setCursor(0, 16);
    u8g2.print("::::::BME280::::::"); // imprime el titular de la pestaña
    u8g2.setCursor(0, 32);
    u8g2.printf("Tempt:  %.1f \260C%", bme_temperature);
    u8g2.setCursor(0, 48);
    u8g2.printf("Humd:   %.1f %%", bme_humidity);
    u8g2.setCursor(0, 64);
    u8g2.printf("Pres.A: %0.f hPa%", bme_pressure);
    u8g2.sendBuffer(); // actualiza la pantalla con los nuevos valores

  }else if(tab == 2){ //muestra el menu del SCD30
  
    u8g2.clearBuffer();
    //despues escribe los valores 
    u8g2.setCursor(0, 16);
    u8g2.print(":::::::SCD30::::::");
    u8g2.setCursor(0, 64);
    u8g2.printf("Humd:   %.1f %%", scd30_humidity);
    u8g2.setCursor(0, 48);
    u8g2.printf("Tempt:  %.1f \260C%", scd30_temperature);
    u8g2.setCursor(0, 32);
    u8g2.printf("CO2:    %0.f ppm%", scd30_co2);
    u8g2.sendBuffer(); // actualiza la pantalla con los nuevos valores

  }else if(tab == 3){ //muestra el menu del SDS011

    u8g2.clearBuffer();
    u8g2.setCursor(0, 16);
    u8g2.print("::::::SDS011::::::");

    u8g2.setCursor(0, 32);
    u8g2.printf("PM2.5:  %.1f ug/m3%", sds011_pm25);

    u8g2.setCursor(0, 48);
    u8g2.printf("PM10:   %.1f ug/m3%", sds011_pm10);

    u8g2.sendBuffer(); // actualiza la pantalla con los nuevos valores

  }else if(tab == 4){ //muestra el menu del GPS
    u8g2.clearBuffer();
    u8g2.setCursor(0, 16);
    u8g2.print("::::::::GPS:::::::");
    
    u8g2.setCursor(0, 32);
    u8g2.print("Time: ");

    //escribe hora
    if(hour < 0 ){
      u8g2.printf("%d%", hour + 24);
    }else if(hour <= 9 && hour >= 0){
      u8g2.print("0");
      u8g2.printf("%d%", hour);
    }else{
      u8g2.printf("%d%", hour);
    }  

    //escribe minuto
    u8g2.print(":");
    if(minute <= 9){
      u8g2.print("0");
    }
    u8g2.printf("%d%", minute);

    //escribe segundos
    u8g2.print(":");
    if(second <= 9){
      u8g2.print("0");
    }
    u8g2.printf("%d%", second);

    u8g2.setCursor(0, 48);
    u8g2.printf("Lat: %.4f%", lat);

    u8g2.setCursor(0, 64);
    u8g2.printf("Lng: %.4f%", lng);

    u8g2.sendBuffer(); // actualiza la pantalla con los nuevos valores
  }  
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

void escribe_en_FLASH(){
  //GPS
  mediciones = SPIFFS.open("/mediciones.txt", "a+");
  mediciones.print(hour);mediciones.print(":");
  mediciones.print(minute);mediciones.print(":");
  mediciones.print(second);mediciones.print(";");
  mediciones.print(lat);mediciones.print(";");
  mediciones.print(lng);mediciones.print(";");
  //BME   
  mediciones.print(bme_temperature);mediciones.print(";");
  mediciones.print(bme_humidity);mediciones.print(";");
  mediciones.print(bme_pressure);mediciones.print(";");
  //SCD30   
  mediciones.print(scd30_co2);mediciones.print(";");
  mediciones.print(scd30_temperature);mediciones.print(";");
  mediciones.print(scd30_humidity);mediciones.print(";");
  //SDS011
  mediciones.print(sds011_pm25);mediciones.print(";");
  //este es el ultimo dato de la fila asi q lleva un salto de linea
  mediciones.print(sds011_pm10);mediciones.println(";"); 
  //termine de escribir
  mediciones.close();
}

//digitalWrite(PIN_LED,HIGH);