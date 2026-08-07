#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ==========================================
// CONFIGURAÇÕES
// ==========================================
const char* ssid     = "Meia - Noite";
const char* password = "midiaengenharia2025";

const char* urlGAS = "https://script.google.com/macros/s/AKfycbx5EC9DepPh1O3xBa507BaRWU1yaGExiOd42NZK-7A2aYOFNdM5NrPFNGG5_gOKn9Jx0Q/exec";

// Intervalo: 120000 = 2 min (testes) | 900000 = 15 min (produção)
const long INTERVALO_NUVEM = 120000;
unsigned long tempoAnterior = 0;

float fatorRegaGlobal = 1.0;

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SMARTGROW — TESTE DE INTEGRAÇÃO IA ===");
  conectarWiFi();
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  unsigned long tempoAtual = millis();

  if (tempoAnterior == 0 || (tempoAtual - tempoAnterior >= INTERVALO_NUVEM)) {
    tempoAnterior = tempoAtual;

    if (WiFi.status() == WL_CONNECTED) {
      enviarParaNuvem();
    } else {
      Serial.println("[WIFI] Conexão perdida. Reconectando...");
      conectarWiFi();
    }
  }
}

// ==========================================
// FUNÇÕES
// ==========================================
void conectarWiFi() {
  Serial.print("[WIFI] Conectando a: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Conectado!");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] FALHA. Verifique SSID e senha.");
  }
}

void enviarParaNuvem() {
  Serial.println("\n[NUVEM] Preparando envio...");

  // Dados simulados
  int umidade1   = random(20, 80);
  int umidade2   = random(20, 80);
  int alertaBoia = 0;
  int volumeRega = 0;

  JsonDocument doc;
  doc["umidade1"]   = umidade1;
  doc["umidade2"]   = umidade2;
  doc["alertaBoia"] = alertaBoia;
  doc["volumeRega"] = volumeRega;

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  Serial.print("[NUVEM] Payload: ");
  Serial.println(jsonPayload);

  HTTPClient http;
  http.begin(urlGAS);
  http.addHeader("Content-Type", "application/json");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.POST(jsonPayload);

  Serial.print("[NUVEM] HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String resposta = http.getString();
    Serial.print("[NUVEM] Resposta: \"");
    Serial.print(resposta);
    Serial.println("\"");

    float novoFator = resposta.toFloat();

    if (novoFator >= 0.0 && novoFator <= 2.0) {
      fatorRegaGlobal = novoFator;
      Serial.print("[IA] ✓ Fator de Rega: ");
      Serial.println(fatorRegaGlobal);
    } else {
      Serial.println("[IA] Resposta fora do intervalo. Mantendo fator anterior.");
    }
  } else {
    Serial.print("[NUVEM] ERRO HTTP: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  Serial.println("[NUVEM] Próximo ciclo em 2 min.");
}