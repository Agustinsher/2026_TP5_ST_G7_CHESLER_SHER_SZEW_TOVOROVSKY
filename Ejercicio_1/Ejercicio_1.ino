/*
  Integración de DHT11 + OLED (U8g2) + Firebase Realtime Database
  Máquina de estados para control de pantallas y ciclo de guardado.
*/
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

#define Web_API_KEY "REPLACE_WITH_YOUR_FIREBASE_PROJECT_API_KEY"
#define DATABASE_URL "REPLACE_WITH_YOUR_FIREBASE_DATABASE_URL"
#define USER_EMAIL "REPLACE_WITH_FIREBASE_PROJECT_EMAIL_USER"
#define USER_PASS "REPLACE_WITH_FIREBASE_PROJECT_USER_PASS"

// ---------------- OBJETOS FIREBASE ----------------
void processData(AsyncResult &aResult);
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS); 
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// ---------------- SENSOR Y OLED ----------------
#define DHTPIN 23
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

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
int estadoAnterior = PANTALLA_1;  // Para saber a dónde volver después de soltar



int intervaloEnvio = 30;  // Tiempo en segundos (mínimo 30)

unsigned long lastSendTime = 0;
String uid; //guarda el user id
const char *ntpServer = "pool.ntp.org";

// Objetos JSON
object_t jsonData, obj1, obj2, obj3; //objetos para la comunicacion con Firebase
JsonWriter writer;



void initWiFi() { //inicializamos el wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando a WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("\nWiFi Conectado!");
}

// ---------------- FUNCIONES DE TIEMPO (NTP) ----------------
String getFecha() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Error";
  char fecha[15];
  strftime(fecha, sizeof(fecha), "%Y-%m-%d", &timeinfo);
  return String(fecha);
}

String getHora() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Error";
  char hora[15];
  strftime(hora, sizeof(hora), "%H:%M:%S", &timeinfo);
  return String(hora);
}

unsigned long getTimestamp() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return 0;
  time(&now);
  return now;
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(sw1, INPUT_PULLUP);
  pinMode(sw2, INPUT_PULLUP);

  dht.begin();
  u8g2.begin();

  initWiFi();
  configTime(-10800, 0, ntpServer);  // Ajusta -10800 al offset de tu zona horaria (ej: -3 hs * 3600 = -10800)

  // Configuración SSL
  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(1000);
  ssl_client.setHandshakeTimeout(5);

  // Inicializar Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

// ---------------- LOOP PRINCIPAL ----------------
void loop() {
  // Mantener autenticación de Firebase
  app.loop();

  // Leer temperatura
  float temperatura = dht.readTemperature();

  // Verificar si es momento de enviar datos a Firebase
  if (app.ready()) {
    unsigned long currentTime = millis();
    // intervaloEnvio está en segundos, lo pasamos a milisegundos
    if (currentTime - lastSendTime >= (intervaloEnvio * 1000)) {
      lastSendTime = currentTime;

      uid = app.getUid().c_str();
      unsigned long ts = getTimestamp();
      String fechaActual = getFecha();
      String horaActual = getHora();

      String parentPath = "/UsersData/" + uid + "/readings/" + String(ts);

      // Crear JSON con Temperatura, Fecha y Hora
      writer.create(obj1, "/temperatura", temperatura);
      writer.create(obj2, "/fecha", fechaActual);
      writer.create(obj3, "/hora", horaActual);
      writer.join(jsonData, 3, obj1, obj2, obj3);

      Database.set<object_t>(aClient, parentPath, jsonData, processData, "RTDB_Send_Data");
      Serial.println("Dato enviado a Firebase: " + String(temperatura) + "C a las " + horaActual);
    }
  }

  // ---------------- MÁQUINA DE ESTADOS (PANTALLAS) ----------------
  switch (estado) {
    // --- PANTALLA 1: Temperatura y Ciclo de Guardado ---
    case PANTALLA_1:
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB10_tr);

      // Mostrar VA: Temperatura Actual
      u8g2.drawStr(5, 20, "VA:");
      char mensajeTemp1[10];
      sprintf(mensajeTemp1, "%.1f C", temperatura);
      u8g2.drawStr(45, 20, mensajeTemp1);

      // Mostrar VU: Ciclo de guardado actual
      u8g2.drawStr(5, 50, "VU:");
      char mensajeCiclo1[15];
      sprintf(mensajeCiclo1, "%d seg", intervaloEnvio);
      u8g2.drawStr(45, 50, mensajeCiclo1);
      u8g2.sendBuffer();
      // Transición a Pantalla 2 (Ambos botones)
      if (digitalRead(sw1) == LOW && digitalRead(sw2) == LOW) {
        estado = SALIR_PANTALLA_1;
      }
      break;

    case SALIR_PANTALLA_1:
      if (digitalRead(sw1) == HIGH && digitalRead(sw2) == HIGH) {
        estado = PANTALLA_2;
      }

    // --- PANTALLA 2: Modificar Ciclo de Guardado ---
    case PANTALLA_2:
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.drawStr(5, 20, "Ciclo:");
      char mensajeCiclo2[15];
      sprintf(mensajeCiclo2, "%d seg", intervaloEnvio);
      u8g2.drawStr(20, 50, mensajeCiclo2);

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


    // --- ACCIÓN: Aumentar ---
    case AUMENTAR_TIEMPO:
      intervaloEnvio += 30;
      estado = PANTALLA_2;
      break;
    // --- ACCIÓN: Disminuir ---
    case DISMINUIR_TIEMPO:
      if (intervaloEnvio > 30) {
        intervaloEnvio -= 30;
      }
      estado = PANTALLA_2;
      break;


    // --- ESPERAR A QUE SE SUELTEN LOS BOTONES (Anti-Rebote) ---
    case SALIR_PANTALLA_2:
      if (digitalRead(sw1) == HIGH && digitalRead(sw2) == HIGH) {
        estado = PANTALLA_1;
      }
      break;
  }
}

// ---------------- CALLBACK DE FIREBASE ----------------
void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;
  if (aResult.isError()) {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  }
}