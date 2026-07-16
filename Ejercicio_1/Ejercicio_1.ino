//Chesler Sher Szew Tovorovsky - Grupo 7 - Curso 5C
//NO BORRAR COMENTARIOS
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <time.h>

// ---------------- CREDENCIALES DE RED Y FIREBASE ----------------
#define WIFI_SSID "MECA-IoT-V2"
#define WIFI_PASSWORD "IoT$2026"

#define Web_API_KEY "AIzaSyBRdxilpXzY4n6WPU-3A7obdntHRv28_-U"
#define DATABASE_URL "https://tp5-st-5c-g7-default-rtdb.firebaseio.com/"
#define USER_EMAIL "ale2008szew@gmail.com"
#define USER_PASS "tp5-st-5c-g7"

// ---------------- OBJETOS FIREBASE ----------------
void processData(AsyncResult &aResult);  //analisis resultado firebase
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
WiFiClientSecure ssl_client;  //define la conexion wifi como ssl_client(conexion segura a firebase)
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);  //define la conexion asincronica(no traba loop) bajo el nombre aClient
RealtimeDatabase Database;

// ---------------- SENSOR Y OLED ----------------
#define DHTPIN 23
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
float temperatura = 0;
#define sw1 35
#define sw2 34

// ---------------- ESTADOS DE PANTALLA ----------------
#define PANTALLA_1 1
#define SALIR_PANTALLA_1 2
#define PANTALLA_2 3
#define AUMENTAR_TIEMPO 4
#define DISMINUIR_TIEMPO 5
#define SALIR_PANTALLA_2 6

int estado = PANTALLA_1;
int umbral = 23;
int intervaloEnvio = 30;  // Tiempo en milisegundos (mínimo 30)
int ultimoEnvio = 0;
int millisTemperatura = 0;

String uid;  //guarda el user id
String databasePath;
String parentPath;
String tempPath = "/temperatura";
String timePath = "/timestamp";

const char *ntpServer = "pool.ntp.org";

// Objetos JSON
object_t jsonData, obj1, obj2;  // Solo necesitamos 2 objetos  (Temp y Timestamp)
JsonWriter writer;

void initWiFi() {  //inicializamos el wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando a WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("\nWiFi Conectado!");
}

// ---------------- FUNCION DE TIEMPO ----------------

unsigned long getTime() {
  time_t now; //tiempo ahora, timestamp
  struct tm timeinfo; //variable para guardar estructura de tiempo
  if (!getLocalTime(&timeinfo)) {
    return (0);
  }
  time(&now);
  return now;
}

String tiempoFormateado() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "error";
  }
  char tiempo[25];
  strftime(tiempo, sizeof(tiempo), "%d/%m/%Y %H:%M:%S", &timeinfo); //defiimos el timestamp a formato mas legible
  return String(tiempo);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(sw1, INPUT_PULLUP);
  pinMode(sw2, INPUT_PULLUP);

  dht.begin();
  u8g2.begin();

  initWiFi();

  // Configuración SSL (Igual a RNT)
  ssl_client.setInsecure();

  ssl_client.setHandshakeTimeout(5);  //si falla la conexion en mas de 5 segundos se cancela

  // Inicializar Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask"); /*los reusltados de esta accion seran analizados 
  en processData*/
  app.getApp<RealtimeDatabase>(Database);                                     /**Vincula el objeto 'Database' con la configuración de la app 
  Firebase recién inicializada */
  Database.url(DATABASE_URL);
}

// ---------------- LOOP ----------------
void loop() {
  // Mantener autenticación de Firebase
  app.loop();

  // Lectura del sensor de temperatura cada 5 segundos
  if (millis() - millisTemperatura >= 5000) {
    temperatura = dht.readTemperature();
    millisTemperatura = millis();
  }
  // ---------------- MÁQUINA DE ESTADOS (PANTALLAS) ----------------
  switch (estado) {
    // --- PANTALLA 1: Temperatura y Ciclo de Guardado ---
    case PANTALLA_1:
      Serial.println("pantalla 1");
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      // Mostrar VA: Temperatura Actual
      u8g2.drawStr(5, 20, "VA:");
      char mensajeTemp1[10];
      sprintf(mensajeTemp1, "%.1f C", temperatura);
      u8g2.drawStr(45, 20, mensajeTemp1);
      // Mostrar  Ciclo de guardado actual
      u8g2.drawStr(5, 50, "VU:");
      char mensajeUmbral[15];
      sprintf(mensajeUmbral, "%d", umbral);
      u8g2.drawStr(45, 50, mensajeUmbral);
      u8g2.sendBuffer();
      // Transición a Pantalla 2 (Ambos botones)
      if (digitalRead(sw1) == LOW && digitalRead(sw2) == LOW) {
        estado = SALIR_PANTALLA_1;
      }
      break;

    case SALIR_PANTALLA_1:
      Serial.println("salir pantalla 1");
      if (digitalRead(sw1) == HIGH && digitalRead(sw2) == HIGH) {
        estado = PANTALLA_2;
      }
      break;
    // --- PANTALLA 2: Modificar Ciclo de Guardado ---
    case PANTALLA_2:
      Serial.println("pantalla 2");
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.drawStr(5, 20, "Ciclo:");
      char mensajeCiclo[15];
      sprintf(mensajeCiclo, "%d seg", intervaloEnvio/ 1000 );
      u8g2.drawStr(20, 50, mensajeCiclo);
      u8g2.sendBuffer();
      // Transiciones
      if (digitalRead(sw1) == LOW && digitalRead(sw2) == LOW) {
        estado = SALIR_PANTALLA_2;
      } else if (digitalRead(sw1) == LOW) {
        estado = AUMENTAR_TIEMPO;
      } else if (digitalRead(sw2) == LOW) {
        estado = DISMINUIR_TIEMPO;
      }
      break;


    // ---  Aumentar ---
    case AUMENTAR_TIEMPO:
      Serial.println("aumentar tiempo");
      if (digitalRead(sw1) == HIGH) {
        intervaloEnvio += 30000;
        estado = PANTALLA_2;
      }
      break;
    // ---Disminuir ---
    case DISMINUIR_TIEMPO:
      Serial.println("disminuir tiempo");
      if (digitalRead(sw2) == HIGH) {
        if (intervaloEnvio > 30000) {
          intervaloEnvio -= 30000;
        }
        estado = PANTALLA_2;
      }

      break;

    case SALIR_PANTALLA_2:
      Serial.println("salir pantalla 2");
      if (digitalRead(sw1) == HIGH && digitalRead(sw2) == HIGH) {
        estado = PANTALLA_1;
      }
      break;
  }


  if (app.ready()) {  //si esta bien la conexion y autenticacion
    unsigned long tiempoActual = millis();
    // intervaloEnvio está en segundos, lo pasamos a milisegundos
    if (tiempoActual - ultimoEnvio >= (intervaloEnvio)) {
      ultimoEnvio = tiempoActual;

      uid = app.getUid().c_str();  //conseguimos la id del usuario

      databasePath = "/UsersData/" + uid + "/readings";     //usamos esta uid para definir el camino a la base de datos
      unsigned long timestamp = getTime();                  //guardamos el tiempo actual
      String tiempo = tiempoFormateado();
      parentPath = databasePath + "/" + String(timestamp);  //guarda lo mismo que database path pero tambien el timestamp

      // Crear JSON con Temperatura y Timestamp
      writer.create(obj1, tempPath, temperatura);  //crea objetox con direccion x y variable x
      writer.create(obj2, timePath, tiempo);
      writer.join(jsonData, 2, obj1, obj2);  //une los 2 objetos en el nombre jsonData

      Database.set<object_t>(aClient, parentPath, jsonData, processData, "RTDB_Send_Data"); /*sube los datos a firebase, se comunica via aClient 
      con parentpath, manda variable jsonData, se analiza resultado en funcion processData,
      el parametro ese entre comillas identifica la tarea*/

      Serial.println("Dato enviado a Firebase: " + String(temperatura) + "C con timestamp: " + String(timestamp));
    }
  }
}


// ---------------- ANALISIS RESULTADOS DE FIREBASE  ----------------
void processData(AsyncResult &aResult) {
  if (!aResult.isResult())  //Verifica si el paquete trae algo valido.
    return;
  if (aResult.isEvent())  // Evalúa si la notificación es un evento  (ej: conectando, autenticando).
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

  if (aResult.isDebug())  // Evalúa si la notificación trae información interna de depuración de bajo nivel (sockets, memoria, etc.)
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

  if (aResult.available())
    Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}
