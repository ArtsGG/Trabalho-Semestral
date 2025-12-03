/*
  Sketch ESP32-S3 adaptado para enviar leituras ao servidor Banco-IoT
  - Mantém toda a lógica de percepção (PIR + MFRC522) igual ao seu código
  - Gera JSON compatível com o endpoint POST /leituras do servidor
  - Não altera lógica de decisão (acesso permitido/negado)

  Variáveis a ajustar: ssid, password, serverUrl
*/

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

// ==========================================================
// CONFIGURAÇÃO DE REDE
// ==========================================================
const char* ssid = "Guga";
const char* password = "seila1234";
// Aponte para o servidor onde o Banco-IoT está rodando
const char* serverUrl = "http://10.0.12.191:5000/leituras";

// ==========================================================
// DEFINIÇÕES DO HARDWARE
// ==========================================================
#define SS_PIN 5
#define RST_PIN 4
#define PIR_PIN 21
#define LED_VERMELHO 16
#define LED_VERDE 18

// UID Válido (exemplo)
const char* UID_VALIDO_CHAR = "83 49 07 F7";

MFRC522 mfrc522(SS_PIN, RST_PIN);

// Controle de tempo não-bloqueante
static unsigned long lastInteractionTime = 0;
const unsigned long MIN_INTERACTION_DELAY = 2000; // 2 segundos

// Buffer para UID lido
char uidLido[25] = "";

// Variáveis locais que substituem propriedades do Arduino Cloud
bool haPresencaLocal = false;
bool acessoPermitidoLocal = false;
String tagUIDLocal = "---";

// ==========================================================
// FUNÇÕES AUXILIARES DE LEDS
// ==========================================================
void setLEDs(bool redState, bool greenState) {
  digitalWrite(LED_VERMELHO, redState ? HIGH : LOW);
  digitalWrite(LED_VERDE, greenState ? HIGH : LOW);
}
void ledVerde()    { setLEDs(false, true); }
void ledVermelho() { setLEDs(true, false); }
void ledOff()      { setLEDs(false, false); }


// ==========================================================
// FUNÇÕES DE REDE
// ==========================================================
void connectToWiFi() {
  Serial.print("[WIFI] Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Conectado!");
    Serial.print("[WIFI] Endereço IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Falha na conexão Wi-Fi inicial. Reiniciando...");
    delay(5000);
    ESP.restart();
  }
}

void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Conexão perdida. Tentando reconectar...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(200);
      attempts++;
    }
  }
}


// ==========================================================
// FUNÇÃO DE ENVIO DE DADOS
// Gera JSON com as keys: presenca (boolean), acesso (boolean), uid_tag (string)
// ==========================================================
void sendDataToAPI(bool presenca, bool acesso_permitido, const char* uid) {
  ensureWiFiConnected();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] Falha de conexão Wi-Fi. Cancelando envio de dados.");
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  String jsonPayload;
  jsonPayload.reserve(128);
  jsonPayload = "{";
  // presenca como booleano (true/false)
  jsonPayload += "\"presenca\":";
  jsonPayload += (presenca ? "true" : "false");
  jsonPayload += ",";
  // acesso como booleano
  jsonPayload += "\"acesso\":";
  jsonPayload += (acesso_permitido ? "true" : "false");
  jsonPayload += ",";
  // uid_tag sempre string
  jsonPayload += "\"uid_tag\":";
  if (uid != nullptr && strlen(uid) > 0) {
    // escapando aspas se necessário (simples)
    String s = String(uid);
    s.replace("\"", "\\\"");
    jsonPayload += "\"" + s + "\"";
  } else {
    jsonPayload += "\"N/A\"";
  }
  jsonPayload += "}";

  Serial.print("[API] Enviando JSON: ");
  Serial.println(jsonPayload);

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    Serial.printf("[API] Código de Resposta HTTP: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("[API] Resposta do Servidor: " + response);
  } else {
    Serial.printf("[API] Erro na Requisição HTTP. Código: %d. Mensagem: %s\n",
                  httpResponseCode, http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}


void setup() {
  Serial.begin(115200);
  while (!Serial) ;
  connectToWiFi();

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  ledOff();

  Serial.println("--- SISTEMA INICIADO ---");
}


void loop() {
  // Lógica de delay não-bloqueante
  if (millis() - lastInteractionTime < MIN_INTERACTION_DELAY) {
    return;
  }
  lastInteractionTime = millis();

  // 1. LEITURA DO SENSOR DE PRESENÇA (PIR)
  int presenceState = digitalRead(PIR_PIN);
  // Debounce
  if (presenceState == LOW) {
    delay(80);
    presenceState = digitalRead(PIR_PIN);
  }

  if (presenceState == LOW) {
    ledOff();
    haPresencaLocal = false;
    acessoPermitidoLocal = false;
    tagUIDLocal = "---";
    // Envia status para API (sem tag)
    sendDataToAPI(false, false, "");
    Serial.println("Nenhuma pessoa detectada");
    return;
  }

  // SE HÁ PRESENÇA
  haPresencaLocal = true;
  bool isTagPresent = mfrc522.PICC_IsNewCardPresent();
  bool accessGranted = false;
  uidLido[0] = '\0';

  if (isTagPresent && mfrc522.PICC_ReadCardSerial()) {
    int pos = 0;
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      pos += sprintf(&uidLido[pos], "%s%02X", (i == 0 ? "" : " "), mfrc522.uid.uidByte[i]);
    }

    if (strcasecmp(uidLido, UID_VALIDO_CHAR) == 0) {
      accessGranted = true;
      ledVerde();
    } else {
      accessGranted = false;
      ledVermelho();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    acessoPermitidoLocal = accessGranted;
    tagUIDLocal = String(uidLido);
    // Envia leitura com tag
    sendDataToAPI(true, acessoPermitidoLocal, uidLido);

    Serial.print("Interacao completa. Acesso: ");
    Serial.println(accessGranted ? "PERMITIDO" : "NEGADO");
  } else {
    // Pessoa detectada, mas sem Tag
    accessGranted = false;
    ledVermelho();
    acessoPermitidoLocal = accessGranted;
    tagUIDLocal = "SEM TAG";
    sendDataToAPI(true, acessoPermitidoLocal, "");
    Serial.println("Pessoa detectada mas sem EPI (RFID).");
  }
}