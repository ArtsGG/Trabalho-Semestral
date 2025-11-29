#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>      
#include <HTTPClient.h>  
#include <string.h>    
#include <ctype.h>     
#include <strings.h>   // AJUSTE OBRIGATÓRIO: Necessário para garantir a compatibilidade de strcasecmp no ESP32

// ==========================================================
// CONFIGURAÇÃO DE REDE
// ==========================================================
const char* ssid = "Guga"; 
const char* password = "seila1234";
const char* serverUrl = "http://10.23.115.239:5000/leituras"; 

// ==========================================================
// DEFINIÇÕES DO HARDWARE
// ==========================================================
#define SS_PIN 5
#define RST_PIN 4
#define PIR_PIN 21      
#define LED_VERMELHO 16 
#define LED_VERDE 18    

// UID Válido
const char* UID_VALIDO_CHAR = "83 49 07 F7";

MFRC522 mfrc522(SS_PIN, RST_PIN); 

// Variável para o controle de tempo não-bloqueante
static unsigned long lastInteractionTime = 0;
const unsigned long MIN_INTERACTION_DELAY = 2000; // 2 segundos

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
        Serial.println("[WIFI] Conexão perdida. Tentando reconectar de forma robusta...");
        
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
    
    //  AJUSTE OPCIONAL: Aumenta o timeout para 3 segundos para redes mais lentas (Melhoria #3)
    http.setTimeout(3000); 

    String jsonPayload;
    jsonPayload.reserve(128); 
    
    jsonPayload = "{";
    jsonPayload += "\"presenca\": " + String(presenca ? 1 : 0) + ","; 
    jsonPayload += "\"acesso\": " + String(acesso_permitido ? "true" : "false") + ",";
    
    if (uid != nullptr && strlen(uid) > 0) {
        jsonPayload += "\"uid_tag\": \"" + String(uid) + "\"";
    } else {
        jsonPayload += "\"uid_tag\": \"N/A\"";
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


void setup()
{
    Serial.begin(115200);
    while (!Serial);
    
    connectToWiFi(); 
    
    SPI.begin();
    mfrc522.PCD_Init();
    
    pinMode(PIR_PIN, INPUT);
    pinMode(LED_VERMELHO, OUTPUT);
    pinMode(LED_VERDE, OUTPUT);
    
    ledOff();
    
    Serial.println("--- SISTEMA INICIADO ---");
    
    byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    Serial.print("Leitor MFRC522 Firmware Version: ");
    Serial.println(v, HEX);
    
    if (v == 0x00 || v == 0xFF) {
        Serial.println("[ERRO CRÍTICO] Leitor MFRC522 não encontrado ou falha de comunicação.");
    }

    Serial.println("Verificando PIR e leitor RFID...");
    Serial.println();
}

void loop()
{
    // Lógica de delay não-bloqueante
    if (millis() - lastInteractionTime < MIN_INTERACTION_DELAY) {
        return; 
    }
    lastInteractionTime = millis();

    // 1. LEITURA DO SENSOR DE PRESENÇA (PIR)
    int presenceState = digitalRead(PIR_PIN);

    // Debounce (verifica duas vezes)
    if (presenceState == LOW) {
        delay(80); 
        presenceState = digitalRead(PIR_PIN); 
    }

    if (presenceState == LOW) {
        ledOff();
        Serial.println("Nenhuma pessoa detectada");
        return; 
    }
    
    // SE HÁ PRESENÇA (presenceState == HIGH)
    bool isTagPresent = mfrc522.PICC_IsNewCardPresent();
    bool accessGranted = false;
    
    char uidLido[25] = ""; 
    
    // --- VERIFICAÇÃO DA TAG ---
    if (isTagPresent && mfrc522.PICC_ReadCardSerial())
    {
        int pos = 0;
        
        for (byte i = 0; i < mfrc522.uid.size; i++)
        {
            pos += sprintf(&uidLido[pos], "%s%02X", (i == 0 ? "" : " "), mfrc522.uid.uidByte[i]);
        }
        
        // Comparação case-insensitive (strcasecmp)
        if (strcasecmp(uidLido, UID_VALIDO_CHAR) == 0) 
        {
            accessGranted = true;
            Serial.print("ACESSO PERMITIDO! Tag valida: ");
            Serial.println(uidLido);
            ledVerde(); 
        }
        else 
        {
            accessGranted = false;
            Serial.print("ACESSO NEGADO! Tag nao cadastrada: ");
            Serial.println(uidLido);
            ledVermelho(); 
        }
        
        mfrc522.PICC_HaltA(); 
        mfrc522.PCD_StopCrypto1();
    }
    else 
    {
        accessGranted = false;
        ledVermelho(); 
        Serial.println("Pessoa detectada mas sem EPI (RFID).");
    }

    // 4. ENVIO DE DADOS PARA O SERVIDOR (API Flask)
    sendDataToAPI(true, accessGranted, uidLido);
}