#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h>  // Para fecha y hora

// --------------------- CONFIGURACIÓN WIFI ---------------------
#define WIFI_SSID "online-OCAMPO"
#define WIFI_PASSWORD "ocampo20"

// --------------------- CONFIGURACIÓN FIREBASE -----------------
#define API_KEY "AIzaSyDm4UQ_tOSC36IfWjHwNOP2aYAy_iMjE9c"
#define DATABASE_URL "https://riego-hibrido-v3-default-rtdb.firebaseio.com/"
#define USER_EMAIL "test@riego.com"
#define USER_PASSWORD "123456"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --------------------- PINES ---------------------
int sensorPin = 34;      // Sensor de humedad
int ledPin    = 2;       // LED indicador
int relePin   = 25;      // Relé de la bomba

// --------------------- CALIBRACIÓN ---------------------
int valorSeco  = 3000;
int valorHumedo = 1000;

// --------------------- PROMEDIO ---------------------
int numLecturasPromedio = 10;
int delayEntreLecturas  = 50;

// --------------------- INTERVALO DE LECTURA ---------------------
int tiempoEntreLecturas = 5000;

// --------------------- HISTERESIS ---------------------
int umbralEncender = 40;
int umbralApagar   = 50;

// --------------------- MEDICIONES CONSECUTIVAS ---------------------
int medicionesNecesarias = 10;
int contadorEncender = 0;
int contadorApagar   = 0;

// --------------------- ESTADO ---------------------
int sensorValor = 0;
int humedad     = 0;
String estado   = "OFF";
String origen   = "auto";

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(relePin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(relePin, LOW);

  // --------------------- CONEXIÓN WIFI ---------------------
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Conectado a WiFi");

  // --------------------- FIREBASE ---------------------
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // --------------------- CONFIGURAR NTP ---------------------
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  // --------------------- PROMEDIO ---------------------
  long suma = 0;
  for (int i = 0; i < numLecturasPromedio; i++) {
    suma += analogRead(sensorPin);
    delay(delayEntreLecturas);
  }
  sensorValor = suma / numLecturasPromedio;

  // --------------------- HUMEDAD ---------------------
  humedad = map(sensorValor, valorSeco, valorHumedo, 0, 100);
  humedad = constrain(humedad, 0, 100);

  Serial.printf("🌡️ Crudo promedio: %d | Humedad: %d%%\n", sensorValor, humedad);

  if (Firebase.ready()) {
    // Guardar lectura actual
    Firebase.RTDB.setInt(&fbdo, "/riego/crudo", sensorValor);
    Firebase.RTDB.setInt(&fbdo, "/riego/humedad", humedad);

    // Guardar en historial por fecha
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);

    String rutaHistorial = "/riego/historial/" +
      String(t->tm_year + 1900) + "/" +
      String(t->tm_mon + 1) + "/" +
      String(t->tm_mday) + "/" +
      String(t->tm_hour) + ":" + String(t->tm_min);

    Firebase.RTDB.setInt(&fbdo, rutaHistorial, humedad);

    // Leer estado y origen
    if (Firebase.RTDB.getString(&fbdo, "/riego/bomba")) {
      estado = fbdo.stringData();
    }
    if (Firebase.RTDB.getString(&fbdo, "/riego/origen")) {
      origen = fbdo.stringData();
    }

    // --------------------- APLICAR ESTADO ---------------------
    if (estado == "ON") {
      digitalWrite(ledPin, HIGH);
      digitalWrite(relePin, HIGH);
      Serial.println("💧 Riego activado (" + origen + ")");
    } else {
      digitalWrite(ledPin, LOW);
      digitalWrite(relePin, LOW);
      Serial.println("💤 Riego desactivado (" + origen + ")");
    }

    // --------------------- ACTIVACIÓN AUTOMÁTICA ---------------------
    if (humedad < umbralEncender && estado == "OFF" && origen != "manual") {
      contadorEncender++;
      if (contadorEncender >= medicionesNecesarias) {
        Firebase.RTDB.setString(&fbdo, "/riego/bomba", "ON");
        Firebase.RTDB.setString(&fbdo, "/riego/origen", "auto");
        Serial.println("⚙️ Riego activado automáticamente");
        contadorEncender = 0;
      }
    } else {
      contadorEncender = 0;
    }

    // --------------------- APAGADO AUTOMÁTICO ---------------------
    if (humedad > umbralApagar && estado == "ON" && origen == "auto") {
      contadorApagar++;
      if (contadorApagar >= medicionesNecesarias) {
        Firebase.RTDB.setString(&fbdo, "/riego/bomba", "OFF");
        Firebase.RTDB.setString(&fbdo, "/riego/origen", "auto");
        Serial.println("🛑 Riego desactivado automáticamente");
        contadorApagar = 0;
      }
    } else {
      contadorApagar = 0;
    }
  }

  delay(tiempoEntreLecturas);
}
