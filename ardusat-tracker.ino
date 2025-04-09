// ====================== LIBRERÍAS ======================
// Librerías necesarias para conectividad, gráficos y manejo de tiempo
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <time.h> // Para sincronizar hora mediante NTP

// ====================== CONFIGURACIÓN TFT ======================
#define TFT_CS    15  // Pin Chip Select para TFT
#define TFT_DC    2   // Pin Data/Command para TFT
#define TFT_RST   4   // Pin Reset para TFT

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST); // Inicializa pantalla TFT

// ====================== DATOS DE WIFI ======================
const char* ssid = "";     // Nombre de red WiFi
const char* password = "";               // Contraseña de red WiFi

// ====================== DATOS API & UBICACIÓN ======================
const char* apiKey = "";   // API key de N2YO
const int noradID = 25544;                          // ID NORAD del satélite (NOAA 18)
float latitude = 20.628914;                         // Latitud del observador
float longitude = -103.369780;                      // Longitud del observador
int altitude = 1600;                                // Altitud en metros
const char* nombreSatelite;

// ====================== CLIENTE HTTP ======================
WiFiClientSecure wifi;  // Cliente seguro para HTTPS
const char* serverAddress = "api.n2yo.com"; // Servidor de la API
const int serverPort = 443;                // Puerto HTTPS
HttpClient client = HttpClient(wifi, serverAddress, serverPort); // Cliente HTTP

// ====================== TIEMPOS DE ACTUALIZACIÓN ======================
unsigned long ultimaActualizacionPase = 0;  // Última vez que se actualizó el pase
unsigned long intervaloPase = 30000;        // Intervalo entre actualizaciones de pase (30 s)
unsigned long updateInterval = 5000;        // Intervalo para actualizar posición (5 s)

time_t tiempoInicioPase = 0;  // Inicio del pase
time_t tiempoFinPase = 0;     // Fin del pase

// ====================== CONFIGURACIÓN DEL MOTOR ======================
#define DIR_AZ   0   // Pin dirección para motor de azimuth
#define STEP_AZ  0   // Pin step para motor de azimuth
#define DIR_EL   0   // Pin dirección para motor de elevación
#define STEP_EL  0   // Pin step para motor de elevación

float pasosPorGradoAz = 10.0;  // Resolución del motor en azimuth
float pasosPorGradoEl = 10.0;  // Resolución del motor en elevación
int delayStep = 800;           // Delay entre pasos (us)

float posAzActual = 0;         // Posición actual del motor en azimuth
float posElActual = 0;         // Posición actual del motor en elevación

// ====================== GESTIÓN DE ERRORES ======================
int erroresPosicion = 0;       // Contador de errores al obtener posición
int erroresPase = 0;           // Contador de errores al obtener pase
const int maxErrores = 10;     // Máximo de errores permitidos antes de detener solicitudes

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200); // Inicia comunicación serial

  // Inicializa pantalla TFT
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 10);
  tft.println("Iniciando...");
  tft.setTextSize(4);
  tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(10, 70);
  tft.println("XE1GCR");
  tft.setTextSize(2);

  // Conexión a red WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  tft.println("Conectando WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  tft.println("WiFi OK!");
  Serial.println("✅ WiFi conectado");
  delay(1000);
  tft.fillScreen(ILI9341_BLACK);

  // Sincroniza hora desde servidor NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  tft.println("Esperando hora...");
  while (time(nullptr) < 100000) {
    delay(500);
  }
  tft.println("Hora sincronizada");
  Serial.println("⏰ Hora sincronizada");
  delay(1000);
  tft.fillScreen(ILI9341_BLACK);

  wifi.setInsecure(); // ⚠️ No valida certificados SSL (necesario si no se tiene certificado cargado)

  // Configura pines del motor
  pinMode(DIR_AZ, OUTPUT);
  pinMode(STEP_AZ, OUTPUT);
  pinMode(DIR_EL, OUTPUT);
  pinMode(STEP_EL, OUTPUT);

  moverAzimuth(0);     // Inicializa posición en 0
  moverElevacion(0);
}

// ====================== LOOP PRINCIPAL ======================
void loop() {
  unsigned long ahoraMillis = millis();

  // Verifica si es momento de actualizar el pase
  if (ahoraMillis - ultimaActualizacionPase > intervaloPase) {
    actualizarPase();
    ultimaActualizacionPase = ahoraMillis;
  }

  // Actualiza la posición del satélite
  obtenerPosicionActual();
  delay(updateInterval);
}

// ====================== FUNCIONES ======================

// --------- Obtener posición actual del satélite ---------
void obtenerPosicionActual() {
  if (erroresPosicion >= maxErrores) {
    Serial.println("🚫 Límite de errores de posición alcanzado. Deteniendo llamadas.");
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Construye la URL para la API
    String path = "https://api.n2yo.com/rest/v1/satellite/positions/" +
                  String(noradID) + "/" +
                  String(latitude, 4) + "/" +
                  String(longitude, 4) + "/" +
                  String(altitude) + "/1/&apiKey=" + apiKey;

    Serial.println("Solicitando posición: ");
    Serial.println("");
    //Serial.println(path);

    // Realiza la solicitud
    client.get(path);
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();
    // Serial.print("Respuesta: "); Serial.println(response);

    // Si la respuesta fue exitosa
    if (statusCode == 200) {
      erroresPosicion = 0; // Reinicia contador de errores
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        // Extrae datos
        nombreSatelite = doc["info"]["satname"];
        int idSatelite = doc["info"]["satid"];
        float azimuth = doc["positions"][0]["azimuth"];
        float elevation = doc["positions"][0]["elevation"];

        // Muestra en consola
        Serial.print("🛰 Satélite: "); Serial.println(nombreSatelite);
        Serial.print("🆔 ID: "); Serial.println(idSatelite);
        Serial.print("📍 Azimuth: "); Serial.println(azimuth);
        Serial.print("📍 Elevación: "); Serial.println(elevation);

        moverTracker(azimuth, elevation);
        mostrarEnPantalla(azimuth, elevation);
      } else {
        erroresPosicion++;
        Serial.println("⚠️ Error al parsear JSON.");
      }
    } else {
      erroresPosicion++;
      Serial.print("⚠️ Error HTTP posición: ");
      Serial.println(statusCode);
    }
  } else {
    Serial.println("📡 WiFi desconectado.");
  }
}

// --------- Actualizar información del pase ---------
void actualizarPase() {
  if (erroresPase >= maxErrores) {
    Serial.println("🚫 Límite de errores de pase alcanzado. Deteniendo llamadas.");
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    String path = "https://api.n2yo.com/rest/v1/satellite/radiopasses/" +
                  String(noradID) + "/" +
                  String(latitude, 4) + "/" +
                  String(longitude, 4) + "/" +
                  String(altitude) +
                  "/1/0/&apiKey=" + apiKey;

    Serial.println("Solicitando pase: ");
    Serial.println("");

    //Serial.println(path);

    client.get(path);
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();
    //Serial.print("Respuesta: "); Serial.println(response);

    if (statusCode == 200) {
      erroresPase = 0;
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, response);
      if (!error) {
        JsonObject pass = doc["passes"][0];
        tiempoInicioPase = pass["startUTC"];
        tiempoFinPase = pass["endUTC"];

        Serial.println("✅ Pase actualizado:");
        Serial.print("Inicio: "); Serial.println(tiempoInicioPase);
        Serial.print("Fin: "); Serial.println(tiempoFinPase);
      } else {
        erroresPase++;
        Serial.println("❌ Error al parsear pase.");
      }
    } else {
      erroresPase++;
      Serial.print("❌ Error HTTP pase: ");
      Serial.println(statusCode);
    }
  } else {
    Serial.println("📡 WiFi desconectado.");
  }
}

// --------- Mover motores para seguir al satélite ---------
void moverTracker(float azimuth, float elevation) {
  moverAzimuth(azimuth);

  if (elevation > 0) {
    moverElevacion(elevation);
    Serial.println("🔁 Rastreo activo.");
  } else {
    Serial.println("⏳ Satélite no visible.");
  }
}

void moverAzimuth(float azimuth) {
  float delta = azimuth - posAzActual;
  int pasos = abs(delta) * pasosPorGradoAz;

  digitalWrite(DIR_AZ, delta >= 0 ? HIGH : LOW);
  for (int i = 0; i < pasos; i++) {
    digitalWrite(STEP_AZ, HIGH);
    delayMicroseconds(delayStep);
    digitalWrite(STEP_AZ, LOW);
    delayMicroseconds(delayStep);
  }

  posAzActual = azimuth;
  Serial.print("↪ Azimuth moved to: "); Serial.println(azimuth);
}

void moverElevacion(float elevation) {
  float delta = elevation - posElActual;
  int pasos = abs(delta) * pasosPorGradoEl;

  digitalWrite(DIR_EL, delta >= 0 ? HIGH : LOW);
  for (int i = 0; i < pasos; i++) {
    digitalWrite(STEP_EL, HIGH);
    delayMicroseconds(delayStep);
    digitalWrite(STEP_EL, LOW);
    delayMicroseconds(delayStep);
  }

  posElActual = elevation;
  Serial.print("↥ Elevation moved to: "); Serial.println(elevation);
}

// --------- Mostrar datos en la pantalla TFT ---------
void mostrarEnPantalla(float azimuth, float elevation) {
  tft.fillScreen(ILI9341_BLACK);

  tft.setCursor(10, 10);
  tft.setTextColor(ILI9341_CYAN);
  tft.println("🛰 Rastreo Satelital");

  tft.setCursor(10, 50);
  tft.setTextColor(ILI9341_YELLOW);
  tft.print("Azimuth: ");
  tft.println(azimuth, 1);

  tft.setCursor(10, 80);
  tft.setTextColor(ILI9341_ORANGE);
  tft.print("Elevacion: ");
  tft.println(elevation, 1);
  tft.setCursor(10, 180);
  tft.setTextColor(ILI9341_GREEN);
  tft.println("Satelite: ");
  tft.setTextSize(3);
  tft.setCursor(10, 210);
  tft.println(nombreSatelite);    // satelite y su id
  tft.setTextSize(2);

  time_t ahora = time(nullptr);

  if (ahora >= tiempoInicioPase && ahora <= tiempoFinPase) {
    int restante = tiempoFinPase - ahora;
    tft.setCursor(10, 120);
    tft.setTextColor(ILI9341_GREEN);
    tft.println("Sat. Visible");
    tft.setCursor(10, 150);
    tft.print("Resta: ");
    tft.print(restante);
    tft.println("s");
  } else if (ahora < tiempoInicioPase) {
    int espera = tiempoInicioPase - ahora;
    if (espera < 3600) {
      tft.setCursor(10, 120);
      tft.setTextColor(ILI9341_RED);
      tft.println("Esperando pase");
      tft.setCursor(10, 150);
      tft.print("En: ");
      tft.print(espera);
      tft.println("s");
    }
    tft.setCursor(10, 120);
    tft.setTextColor(ILI9341_WHITE);
    tft.println("Sin pase activo");
  } 
}
