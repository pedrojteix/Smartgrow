// ============================================================
// SMARTGROW -- Firmware R05
// + BME280 integrado (temp ar, umidade ar, pressao, VPD)
// + DS18B20 integrado (temperatura da terra/raiz)
// + WiFi resiliente: opera localmente durante quedas de rede
// + Display mostra status WiFi/sem conexao
// + Transplante 30/04/2026 -- FatorK=173.92
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
// 1. CONFIGURACOES
// ============================================================
const char* WIFI_SSID     = "Meia - Noite";
const char* WIFI_PASSWORD = "midiaengenharia2025";
const char* URL_GAS       = "https://script.google.com/macros/s/AKfycby56nn56erEcdsAG06Rg5wnkjVYau9uWnO73KT0MjMzk7uPJYujvms41aMHNYtu0IHt-A/exec";

const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = -3 * 3600;
const int   DST_OFFSET = 0;

// ============================================================
// 2. PINOS
// ============================================================
#define I2C_SDA       21
#define I2C_SCL       22
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

#define PINO_SOLO1    32
#define PINO_SOLO2    33
#define PINO_FLUXO    25
#define PINO_BOIA     26
#define PINO_BOOT     0

#define RELE_LUZ      18
#define RELE_BOMBA    19
#define RELE_ON       LOW
#define RELE_OFF      HIGH

// Sensores adicionais
#define PINO_DS18B20  4    // OneWire -- temperatura da terra (resistor 4.7kOhm entre VCC e DATA)
// BME280 usa I2C (SDA=21, SCL=22) -- mesmo barramento do display, endereco 0x76 ou 0x77

// ============================================================
// 3. CALIBRACAO
// ============================================================
const int SOLO_AR   = 3380;
const int SOLO_AGUA = 1250;

// ============================================================
// 3b. SENSORES BME280 E DS18B20
// ============================================================
Adafruit_BME280 bme;
bool            bmeOk       = false;
float           tempAr      = 0.0;   // grausC
float           umidadeAr   = 0.0;   // %
float           pressaoHpa  = 0.0;   // hPa
float           vpd         = 0.0;   // kPa

OneWire         oneWire(PINO_DS18B20);
DallasTemperature ds18b20(&oneWire);
bool            ds18Ok      = false;
float           tempTerra   = 0.0;   // grausC

// Calcula VPD a partir de temperatura e umidade relativa
float calcularVPD(float tempC, float rhPct) {
  float svp = 0.61078 * exp((17.27 * tempC) / (tempC + 237.3));
  return svp * (1.0 - rhPct / 100.0);
}

// ============================================================
// 4. FASES
// ============================================================
struct FasePlanta {
  const char* nome;
  const char* nomeGAS;
  int  horasLuz;
  int  horaLigar;
  int  horaDesligar;
  bool exigeBreu;
};

const FasePlanta FASES[] = {
  { "Germinacao", "germinacao", 17,  7,  0, false },
  { "Vegetacao",  "vegetacao",  17,  7,  0, false },
  { "Floracao",   "floracao",   12,  8, 20, true  },
};
int faseAtual = 1;

// ============================================================
// 5. REGA
// ============================================================
const float FATOR_DOIS_VASOS       = 2.0;
const float FATOR_CALIBRACAO_FLUXO = 173.92; // Recalibrado com valvulas retentoras (media 2 rodadas: 167.11 e 180.73)
const int   LIMIAR_DESEQUILIBRIO   = 20;

float fatorRegaGlobal = 1.0;
float volumeBaseIA_mL = 0.0;  // recomendado pela IA (por vaso)
float volumeTotal_mL  = 0.0;  // volumeBaseIA * 2 (bombeado real)
float volumeTotalML_ultimo = 0.0;   // 0 = nunca regou -- atualizado apos cada rega real
bool  regarAgora      = false;

bool  bombaHabilitada  = true;
bool  executandoRega   = false;
float volumeAlvo_mL    = 0.0;
float volumeAtual_mL   = 0.0;

volatile int pulsosFluxo = 0;

// ============================================================
// 6. GALAO
// ============================================================
const float GALAO_TOTAL_ML = 7000.0;
float galaoAtual_mL        = 7000.0;

// ============================================================
// 7. CICLO
// ============================================================
bool          cicloAtivo      = false;
unsigned long cicloStartMs    = 0;
int           diasCiclo       = 0;
unsigned long cicloId         = 0; // timestamp UNIX do start_ciclo

// ============================================================
// 7b. CONTROLE DE REGA -- rastreia tempo desde ultima rega
// ============================================================
unsigned long tUltimaRega_ms  = 0;  // millis() da ultima rega completa

float horasDesdeUltimaRega() {
  if (tUltimaRega_ms == 0) return 999.0; // nunca regou
  return (millis() - tUltimaRega_ms) / 3600000.0;
}

// ============================================================
// 8. LUZ
// ============================================================
bool luzManualOverride = false;
bool luzManualEstado   = false;
bool luzAtiva          = false;

// Controle de override temporario da luz
// Quando override ativo, guarda o momento em que foi ativado
unsigned long tLuzOverrideMs = 0;
// Se apagou manualmente (override OFF): segura por 3h
// Se ligou manualmente (override ON): segura por 1h
const unsigned long LUZ_OVERRIDE_APAGADA_MS = 10800000UL; // 3h
const unsigned long LUZ_OVERRIDE_LIGADA_MS  =  3600000UL; // 1h

// ============================================================
// 9. DISPLAY
// ============================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int paginaAtual = 0;

// Controle de brilho e sleep do display
unsigned long tUltimaInteracao  = 0;   // millis() da ultima interacao com o botao
const long    T_DIM             = 120000;  // 2 min -> reduz brilho para 50%
const long    T_SLEEP           = 1800000; // 30 min -> apaga display
bool          displayDimmed     = false;
bool          displayOff        = false;
const uint8_t BRILHO_NORMAL     = 200;   // brilho padrao (0-255)
const uint8_t BRILHO_DIM        = 40;    // brilho reduzido 20%

void setBrilho(uint8_t valor) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(valor);
}

void acordarDisplay() {
  if (displayOff) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    displayOff = false;
  }
  if (displayDimmed) {
    setBrilho(BRILHO_NORMAL);
    displayDimmed = false;
  }
  tUltimaInteracao = millis();
}

void verificarSleepDisplay() {
  unsigned long agora = millis();
  unsigned long inativo = agora - tUltimaInteracao;

  if (!displayOff && inativo >= (unsigned long)T_SLEEP) {
    // 30 min sem interacao -> apaga
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    displayOff    = true;
    displayDimmed = false;
  } else if (!displayOff && !displayDimmed && inativo >= (unsigned long)T_DIM) {
    // 2 min sem interacao -> dimeia
    setBrilho(BRILHO_DIM);
    displayDimmed = true;
  }
}

// ============================================================
// 10. SENSORES
// ============================================================
int  umidadeSolo1Pct    = 0;
int  umidadeSolo2Pct    = 0;
bool nivelBaixo         = false;
bool          desequilibrioAtivo    = false;
unsigned long tUltimoDesequilibrio   = 0;       // millis() do ultimo evento enviado
const long    INTERVALO_DESEQUIL     = 1800000; // 30 min entre eventos de desequilibrio

// ============================================================
// 11. TEMPORIZACAO
// ============================================================
unsigned long tAnteriorSensores = 0;
const long    INTERVALO_SENSORES = 2000;

unsigned long tAnteriorNuvem = 0;
const long    INTERVALO_NUVEM = 900000;

unsigned long tAnteriorDias = 0;
const long    INTERVALO_DIAS = 60000;

// ============================================================
// 12. GESTOS
// ============================================================
unsigned long tBotaoPressionado = 0;
unsigned long tUltimoClique     = 0;
int           contadorCliques   = 0;
bool          botaoSegurando    = false;
bool          gestoConcluido    = false;

const int TEMPO_CLIQUE_MAX = 400;
const int TEMPO_SEGURAR    = 2000;

// ============================================================
// 13. BITMAP DA FOLHA
// ============================================================
const unsigned char epd_bitmap_Folha[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x60,0x00,0x7c,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x70,0x00,0xfe,0x00,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x78,0x00,0xfe,0x00,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x7c,0x00,0xfe,0x00,0x7c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x7e,0x00,0xfe,0x01,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x7e,0x00,0xfe,0x01,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x1f,0x80,0xfe,0x01,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x1f,0xe0,0xfe,0x0f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x1f,0xe0,0xfe,0x0f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x1f,0xe0,0xfe,0x0f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x0f,0xf8,0xfe,0x1f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x07,0xfc,0xfe,0x7f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x07,0xfc,0xfe,0x7f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x07,0xfc,0xff,0x7f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x03,0xfe,0x3d,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x03,0xfe,0x7d,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x03,0xfe,0x3d,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x00,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0xfc,0x7f,0xff,0xfc,0x7f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0xfc,0x5f,0xff,0xfc,0x7f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0xfd,0xdf,0xff,0xf1,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0xff,0x1f,0xff,0xf1,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0xff,0xff,0xff,0xff,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x7f,0xff,0xff,0xff,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x7f,0xff,0xff,0xff,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x03,0xfd,0xff,0xff,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x2f,0xff,0xe2,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x0f,0xff,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x1f,0xff,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x1f,0xff,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x3f,0x8b,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xfe,0x11,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xfe,0x11,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// ============================================================
// 14. ISR FLUXO
// ============================================================
void IRAM_ATTR ISR_ContaPulso() { pulsosFluxo++; }

// ============================================================
// 15. SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[OLED] Falha"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  pinMode(PINO_BOIA,  INPUT_PULLUP);
  pinMode(PINO_BOOT,  INPUT_PULLUP);
  pinMode(PINO_FLUXO, INPUT_PULLUP);
  pinMode(RELE_LUZ,   OUTPUT);
  pinMode(RELE_BOMBA, OUTPUT);
  digitalWrite(RELE_LUZ,   RELE_OFF);
  digitalWrite(RELE_BOMBA, RELE_OFF);

  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), ISR_ContaPulso, FALLING);

  // ?? Sensores opcionais: BME280 e DS18B20 ?????????????????????
  // Scan I2C antes de bme.begin() -- sem isso, chamar begin() com o sensor
  // desconectado pode travar o barramento I2C e corromper o display.
  bool bme280Presente = false;
  uint8_t addrsI2C[] = {0x76, 0x77};
  for (int ai = 0; ai < 2; ai++) {
    Wire.beginTransmission(addrsI2C[ai]);
    if (Wire.endTransmission() == 0) { bme280Presente = true; break; }
  }

  if (bme280Presente) {
    bmeOk = bme.begin(0x76);
    if (!bmeOk) bmeOk = bme.begin(0x77);
    if (bmeOk) {
      bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                      Adafruit_BME280::SAMPLING_X2, Adafruit_BME280::SAMPLING_X2,
                      Adafruit_BME280::SAMPLING_X2, Adafruit_BME280::FILTER_X4,
                      Adafruit_BME280::STANDBY_MS_500);
      Serial.println(F("[BME280] OK"));
    }
  } else {
    bmeOk = false;
    Serial.println(F("[BME280] nao detectado -- aguardando conexao"));
  }

  // DS18B20: begin() e seguro sem sensor; getDeviceCount() retorna 0 sem resistor.
  ds18b20.begin();
  ds18Ok = (ds18b20.getDeviceCount() > 0);
  if (ds18Ok) {
    ds18b20.setResolution(12);
    Serial.printf("[DS18B20] OK -- %d sensor(es)\n", ds18b20.getDeviceCount());
  } else {
    Serial.println(F("[DS18B20] nao detectado -- aguardando resistor 4.7kOhm"));
  }

  conectarWiFi();
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  sincronizarNTP();

  // Verifica horario imediatamente ao iniciar e aplica estado correto da luz
  controlarLuz();
  Serial.println(F("[LUZ] Estado inicial verificado"));

  // Inicializa brilho do display e timer de interacao
  setBrilho(BRILHO_NORMAL);
  tUltimaInteracao = millis();

  exibirBoot();
  Serial.println(F("[SGRW] R05 iniciado -- FatorK=173.92 | Transplante 30/04/2026"));
}

// ============================================================
// 16. LOOP
// ============================================================
void loop() {
  unsigned long agora = millis();

  if (agora - tAnteriorSensores >= INTERVALO_SENSORES) {
    tAnteriorSensores = agora;
    lerSensores();
    verificarDesequilibrio();
    controlarLuz();
    verificarSleepDisplay();  // dim/sleep apos inatividade
    if (!displayOff) atualizarDisplay(); // so atualiza se display ativo
  }

  // Primeiro envio so apos 30s do boot para sensores estabilizarem
  if (tAnteriorNuvem == 0) tAnteriorNuvem = millis() - INTERVALO_NUVEM + 30000;
  if (agora - tAnteriorNuvem >= (unsigned long)INTERVALO_NUVEM) {
    tAnteriorNuvem = agora;
    if (WiFi.status() == WL_CONNECTED) enviarParaNuvem(false);
    else conectarWiFi();
  }

  if (cicloAtivo && agora - tAnteriorDias >= INTERVALO_DIAS) {
    tAnteriorDias = agora;
    diasCiclo = (int)((agora - cicloStartMs) / 86400000UL);
  }

  if (executandoRega) {
    noInterrupts();
    int copia = pulsosFluxo;
    interrupts();
    volumeAtual_mL = (float)copia / FATOR_CALIBRACAO_FLUXO;
    if (volumeAtual_mL >= volumeAlvo_mL || digitalRead(PINO_BOIA) == HIGH) {
      pararBomba("Rega concluida");
    }
  }

  processarBotao();
}

// ============================================================
// 17. WI-FI / NTP
// ============================================================
// ?? WiFi resiliente: nao bloqueia o loop durante reconexao ????
// O sistema continua operando (rega, luz, sensores) mesmo sem WiFi.
// Tenta reconectar a cada chamada (que ocorre a cada INTERVALO_NUVEM).
// A reconexao tem timeout de 15s; se falhar, retorna e tenta no proximo ciclo.
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return; // ja conectado, nada a fazer

  Serial.print(F("[WIFI] Conectando"));
  WiFi.disconnect(true);
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Timeout nao bloqueante de 15s -- bomba e sensores continuam rodando
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    // Mantem rega ativa durante tentativa de conexao
    if (executandoRega) {
      noInterrupts(); int p = pulsosFluxo; interrupts();
      volumeAtual_mL = (float)p / FATOR_CALIBRACAO_FLUXO;
      if (volumeAtual_mL >= volumeAlvo_mL || digitalRead(PINO_BOIA) == HIGH) {
        pararBomba("Rega concluida (durante reconexao WiFi)");
      }
    }
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WIFI] Reconectado"));
    // Sincroniza NTP novamente apos reconexao
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    sincronizarNTP();
  } else {
    Serial.println(F("\n[WIFI] Sem conexao -- operando localmente"));
    // Sistema continua: luz e rega funcionam normalmente offline
    // Proxima tentativa de reconexao no proximo INTERVALO_NUVEM (15min)
  }
}

void sincronizarNTP() {
  struct tm ti;
  int t = 0;
  while (!getLocalTime(&ti) && t < 20) { delay(500); t++; }
  if (getLocalTime(&ti)) Serial.printf("[NTP] %02d:%02d\n", ti.tm_hour, ti.tm_min);
}

bool obterHora(int &hora, int &minuto) {
  struct tm ti;
  if (!getLocalTime(&ti)) return false;
  hora = ti.tm_hour; minuto = ti.tm_min;
  return true;
}

// ============================================================
// 18. SENSORES
// ============================================================
void lerSensores() {
  // Solo 1 e Solo 2
  int b1 = analogRead(PINO_SOLO1);
  umidadeSolo1Pct = constrain(map(b1, SOLO_AR, SOLO_AGUA, 0, 100), 0, 100);
  int b2 = analogRead(PINO_SOLO2);
  umidadeSolo2Pct = constrain(map(b2, SOLO_AR, SOLO_AGUA, 0, 100), 0, 100);
  nivelBaixo = (digitalRead(PINO_BOIA) == HIGH);

  // BME280 -- temperatura e umidade do ar, pressao, VPD
  if (bmeOk) {
    tempAr     = bme.readTemperature();
    umidadeAr  = bme.readHumidity();
    pressaoHpa = bme.readPressure() / 100.0F;
    vpd        = calcularVPD(tempAr, umidadeAr);
    Serial.printf("[BME280] T=%.1fC H=%.1f%% P=%.1fhPa VPD=%.2fkPa\n",
                  tempAr, umidadeAr, pressaoHpa, vpd);
  }

  // DS18B20 -- temperatura da terra
  if (ds18Ok) {
    ds18b20.requestTemperatures();
    float t = ds18b20.getTempCByIndex(0);
    if (t > -127.0) tempTerra = t;
    Serial.printf("[DS18B20] T_terra=%.1fC\n", tempTerra);
  }

  Serial.printf("[SENS] S1=%d%% S2=%d%%\n", umidadeSolo1Pct, umidadeSolo2Pct);
}

void verificarDesequilibrio() {
  int dif = abs(umidadeSolo1Pct - umidadeSolo2Pct);
  bool novo = (dif > LIMIAR_DESEQUILIBRIO);
  unsigned long agora = millis();

  if (novo && !desequilibrioAtivo) {
    // Entra no estado de desequilibrio
    desequilibrioAtivo = true;
    // So envia evento se passou tempo suficiente desde o ultimo
    if (agora - tUltimoDesequilibrio >= (unsigned long)INTERVALO_DESEQUIL) {
      tUltimoDesequilibrio = agora;
      String desc = "Dif=" + String(dif) + "% S1=" + String(umidadeSolo1Pct) + "% S2=" + String(umidadeSolo2Pct) + "%";
      enviarEventoParaNuvem("desequilibrio_hidraulico", desc.c_str());
      Serial.println("[HIDRO] Desequilibrio: " + desc);
    }
  } else if (!novo && desequilibrioAtivo) {
    // Sai do estado de desequilibrio -- silencioso, sem evento
    desequilibrioAtivo = false;
    Serial.println("[HIDRO] Desequilibrio resolvido");
  }
}

// ============================================================
// 19. LUZ
// ============================================================
void controlarLuz() {
  int hora, minuto;
  if (!obterHora(hora, minuto)) return;
  FasePlanta f = FASES[faseAtual];

  // Estado que a luz DEVERIA ter segundo os horarios programados
  // horaDesligar == 0 significa meia-noite (00h)
  // Vegetacao: liga 7h, desliga 00h -> ligada das 7h ate 23h59
  // Floracao:  liga 8h, desliga 20h  -> ligada das 8h ate 19h59
  bool deveAutomatico;
  if (f.horaDesligar == 0) {
    // Desliga a meia-noite: ligada se hora >= horaLigar (e nunca das 0h as horaLigar-1)
    deveAutomatico = (hora >= f.horaLigar); // 0h-6h = false, 7h-23h = true
  } else {
    deveAutomatico = (hora >= f.horaLigar && hora < f.horaDesligar);
  }

  if (luzManualOverride) {
    unsigned long tempoOverride = millis() - tLuzOverrideMs;
    unsigned long limite = luzManualEstado
      ? LUZ_OVERRIDE_LIGADA_MS   // ligou manualmente: segura 1h
      : LUZ_OVERRIDE_APAGADA_MS; // apagou manualmente: segura 3h

    if (tempoOverride >= limite) {
      // Tempo de override expirou -- volta ao automatico
      luzManualOverride = false;
      Serial.printf("[LUZ] Override expirado apos %luh -- voltando ao automatico\n",
                    tempoOverride / 3600000UL);
    }
  }

  bool deve = luzManualOverride ? luzManualEstado : deveAutomatico;

  if (deve != luzAtiva) {
    luzAtiva = deve;
    digitalWrite(RELE_LUZ, luzAtiva ? RELE_ON : RELE_OFF);
    Serial.printf("[LUZ] %s\n", luzAtiva ? "ON" : "OFF");
  }
}

// ============================================================
// 20. BOMBA
// ============================================================
// Armazena millis() e pulsos no momento em que a bomba liga
// para calcular tempo total e pulsos na parada
unsigned long tInicioRega_ms = 0;
int           pulsosInicioRega = 0;

void iniciarRega(float volumeBase_mL) {
  if (!bombaHabilitada) { Serial.println(F("[BOMBA] Desabilitada")); return; }
  if (nivelBaixo)       { Serial.println(F("[BOMBA] Nivel baixo")); return; }
  if (executandoRega)   { Serial.println(F("[BOMBA] Ja regando")); return; }

  volumeBaseIA_mL = volumeBase_mL;
  volumeAlvo_mL   = volumeBase_mL * FATOR_DOIS_VASOS;
  volumeAtual_mL  = 0.0;

  noInterrupts(); pulsosFluxo = 0; interrupts();
  pulsosInicioRega = 0; // zera contador local tambem
  tInicioRega_ms   = millis();

  executandoRega = true;
  digitalWrite(RELE_BOMBA, RELE_ON);
  Serial.printf("[BOMBA] Inicio. Base=%.0fmL Alvo=%.0fmL\n", volumeBase_mL, volumeAlvo_mL);

  // Evento de inicio: registra horario exato de ativacao da bomba
  if (WiFi.status() == WL_CONNECTED) {
    int h = 0, m = 0, s = 0;
    struct tm ti;
    if (getLocalTime(&ti)) { h = ti.tm_hour; m = ti.tm_min; s = ti.tm_sec; }
    char desc[80];
    snprintf(desc, sizeof(desc), "Bomba ON %02d:%02d:%02d -- alvo %.0fmL/vaso (%.0fmL total)",
             h, m, s, volumeBase_mL, volumeBase_mL * FATOR_DOIS_VASOS);
    enviarEventoParaNuvem("bomba_on", desc);
  }
}

void pararBomba(const char* motivo) {
  digitalWrite(RELE_BOMBA, RELE_OFF);
  volumeTotal_mL       = volumeAtual_mL;
  volumeTotalML_ultimo = volumeAtual_mL;
  galaoAtual_mL = max(0.0f, galaoAtual_mL - volumeTotal_mL);
  executandoRega = false;
  tUltimaRega_ms = millis();

  // Calcula tempo total de ativacao e pulsos consumidos
  unsigned long duracaoMs = millis() - tInicioRega_ms;
  int           duracaoS  = (int)(duracaoMs / 1000);
  noInterrupts(); int pulsosTotal = pulsosFluxo; interrupts();

  Serial.printf("[BOMBA] Parada: %s | %.0fmL | Galao: %.0fmL | %ds | %d pulsos\n",
                motivo, volumeTotal_mL, galaoAtual_mL, duracaoS, pulsosTotal);

  // Evento de fim: registra horario, pulsos, duracao e volume real
  if (WiFi.status() == WL_CONNECTED) {
    int h = 0, m = 0, s = 0;
    struct tm ti;
    if (getLocalTime(&ti)) { h = ti.tm_hour; m = ti.tm_min; s = ti.tm_sec; }
    char desc[128];
    snprintf(desc, sizeof(desc),
             "Bomba OFF %02d:%02d:%02d | %ds | %d pulsos | %.0fmL total (%.0fmL/vaso)",
             h, m, s, duracaoS, pulsosTotal,
             volumeTotal_mL, volumeTotal_mL / FATOR_DOIS_VASOS);
    enviarEventoParaNuvem("bomba_off", desc);
  }
}

// ============================================================
// 21. DISPLAY
// ============================================================
const int DISP_X = 58;  // Validado nas fotos dos testes
const int DISP_W = 70;

void atualizarDisplay() {
  if (nivelBaixo) { exibirAlertaBoia(); return; }
  display.clearDisplay();
  display.drawBitmap(0, 0, epd_bitmap_Folha, 128, 64, SSD1306_WHITE);
  switch (paginaAtual) {
    case 0: exibirPaginaRega();     break;
    case 1: exibirPaginaLuz();      break;
    case 2: exibirPaginaSensores(); break;
  }
  display.display();
}

void exibirPaginaRega() {
  // Layout centralizado verticalmente no display 64px
  // Y=4  : "agua" (fonte 1, 8px altura)
  // Y=14 : XX dias (fonte 3 para XX, fonte 1 para "dias" alinhado a base)
  // Y=42 : XXXmL / Xh (fonte 1)
  // Y=54 : alertas (fonte 1, so se necessario)

  // Autonomia: usa volume real da ultima rega (volumeTotalML_ultimo = total dos 2 vasos)
  // Autonomia: galao ? consumo estimado por rega
  // Prioridade: 1) volume real medido pelo fluxo, 2) volume recomendado pela IA?2, 3) 300mL fallback
  // consumoPorRega = volume total saindo do galao por rega (ambos os vasos)
  // volumeTotalML_ultimo ja e o total real. volumeBaseIA_mL e por vaso (?2). Fallback 600mL (300?2)
  float consumoPorRega = (volumeTotalML_ultimo > 0) ? volumeTotalML_ultimo
                       : (volumeBaseIA_mL > 0)      ? volumeBaseIA_mL * 2.0
                       :                              600.0; // 300mL/vaso ? 2 vasos
  int diasAutonomia = (int)(galaoAtual_mL / consumoPorRega);
  int intervaloH = (faseAtual == 2) ? 24 : 48;
  int volTotal2Vasos = (int)(volumeBaseIA_mL * 2);

  // "agua"
  display.setTextSize(1);
  display.setCursor(DISP_X, 4);
  display.print("agua");

  // XX em fonte 3 (18px altura)
  display.setTextSize(3);
  display.setCursor(DISP_X, 14);
  if (diasAutonomia < 10) display.print("0");
  display.print(diasAutonomia);

  // "dias" em fonte 1 alinhado a base do numero grande (Y=14+18-8=24)
  display.setTextSize(1);
  display.setCursor(DISP_X + 38, 24);
  display.print("dias");

  // "XXXmL / Xh"
  display.setCursor(DISP_X, 42);
  display.print(volTotal2Vasos);
  display.print("mL / ");
  display.print((int)intervaloH);
  display.print("h");

  // Alertas
  if (executandoRega || desequilibrioAtivo || nivelBaixo) {
    display.setCursor(DISP_X, 54);
    if (executandoRega)        display.print("REGANDO...");
    else if (desequilibrioAtivo) display.print("!HIDRO");
    else if (nivelBaixo)       display.print("RESERV VAZIO");
  }
}

void exibirPaginaLuz() {
  FasePlanta f = FASES[faseAtual];
  display.setTextSize(1);

  // Layout centralizado: 3 linhas, espacamento de ~16px
  // Y=10 : status
  // Y=26 : horarios
  // Y=42 : on/off

  // Status com tempo restante do override
  display.setCursor(DISP_X, 10);
  if (luzManualOverride) {
    unsigned long limite = luzManualEstado ? LUZ_OVERRIDE_LIGADA_MS : LUZ_OVERRIDE_APAGADA_MS;
    unsigned long restante = (millis() - tLuzOverrideMs < limite)
      ? (limite - (millis() - tLuzOverrideMs)) / 60000  // em minutos
      : 0;
    if (luzManualEstado) {
      display.print("ON "); display.print((int)restante); display.print("min");
    } else {
      display.print("OFF "); display.print((int)restante); display.print("min");
    }
  } else {
    display.print(luzAtiva ? "LUZ ON" : "LUZ OFF");
  }

  // Linha 2: "17h ON (07-00h)"  ex vegetacao -- mas nao cabe (>70px)
  // Solucao: duas linhas separadas, cada uma com sua info
  // Y=24: "17h ON (07h~00h)" -> "17ON/7OFF" cabe (66px)
  // Y=36: "07h -> 00h"       -> horarios    cabe (60px)
  //
  // Como pedido: linha ON e linha OFF separadas com horarios
  // "17h ON (07-23:59)" nao cabe -> abreviado: "17h ON 07~00h" = 13?6=78 nao
  // Melhor: "17hON (07-00h)" = 14?6=84 nao
  // Adotamos: linha ON com horario inicio, linha OFF com horario inicio
  //   L2: "17h ON  (07h~)"   -- 12chars=72px -- marginal
  //   L3: " 7h OFF (~00h)"   -- 13chars=78px -- nao
  // Solucao final adotada:
  //   L2: "17h ON"   e "07h ate 00h"
  //   L3: " 7h OFF"  -- nao repete o horario (ja esta na L2)
  // Formato final:
  //   L2 Y=26: "17h ON (07h)"
  //   L3 Y=38: " 7h OFF (00h)"

  // Linha ON
  display.setCursor(DISP_X, 26);
  display.print(f.horasLuz);
  display.print("h ON ");
  if (f.horaLigar < 10) display.print("0");
  display.print(f.horaLigar);
  display.print("h");

  // Linha OFF
  display.setCursor(DISP_X, 40);
  display.print(24 - f.horasLuz);
  display.print("h OFF ");
  if (f.horaDesligar == 0) display.print("00");
  else {
    if (f.horaDesligar < 10) display.print("0");
    display.print(f.horaDesligar);
  }
  display.print("h");
}

void exibirPaginaSensores() {
  display.setTextSize(1);

  // Solo 1
  display.setCursor(DISP_X, 5);
  display.print("S1:");
  if (umidadeSolo1Pct < 10) display.print("0");
  display.print(umidadeSolo1Pct);
  display.print("% S2:");
  if (umidadeSolo2Pct < 10) display.print("0");
  display.print(umidadeSolo2Pct);
  display.print("%");

  // Temperatura do ar (BME280) e da terra (DS18B20)
  display.setCursor(DISP_X, 20);
  if (bmeOk) {
    display.print("Ar:");
    display.print((int)tempAr);
    display.print("C ");
    display.print((int)umidadeAr);
    display.print("%");
  } else {
    display.print("Ar: --miss--");
  }

  display.setCursor(DISP_X, 34);
  if (ds18Ok) {
    display.print("Terra:");
    display.print((int)tempTerra);
    display.print("C");
  } else {
    display.print("Terra: --miss--");
  }

  // VPD
  display.setCursor(DISP_X, 48);
  if (bmeOk) {
    display.print("VPD:");
    display.print(vpd, 2);
    display.print("kPa");
  } else {
    display.print("VPD: --miss--");
  }

  // Alerta desequilibrio ou sem WiFi
  if (desequilibrioAtivo) {
    display.setCursor(DISP_X, 57);
    display.print("!HIDRO");
  } else if (WiFi.status() != WL_CONNECTED) {
    display.setCursor(DISP_X, 57);
    display.print("SEM WIFI");
  }
}

void exibirAlertaBoia() {
  display.clearDisplay();
  display.drawBitmap(0, 0, epd_bitmap_Folha, 128, 64, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(DISP_X, 16); display.print("ALERTA!");
  display.setCursor(DISP_X, 28); display.print("Abastecer");
  display.setCursor(DISP_X, 40); display.print("reserv.");
  display.display();
}

void exibirBoot() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 20); display.print("SmartGrow R05");
  display.setCursor(20, 34); display.print("Iniciando...");
  display.display();
  delay(2000);
}

void exibirFeedback(const char* l1, const char* l2 = "") {
  // So exibe se display estiver ativo
  if (displayOff) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 22); display.print(l1);
  if (strlen(l2) > 0) { display.setCursor(10, 36); display.print(l2); }
  display.display();
  // Delay nao bloqueante: processa bomba durante espera
  unsigned long t = millis();
  while (millis() - t < 1500) {
    if (executandoRega) {
      noInterrupts(); int p = pulsosFluxo; interrupts();
      volumeAtual_mL = (float)p / FATOR_CALIBRACAO_FLUXO;
      if (volumeAtual_mL >= volumeAlvo_mL || digitalRead(PINO_BOIA) == HIGH) {
        pararBomba("Rega concluida");
        break;
      }
    }
    delay(10);
  }
}

// ============================================================
// 22. GESTOS
// ============================================================
void processarBotao() {
  bool pressionado = (digitalRead(PINO_BOOT) == LOW);
  unsigned long agora = millis();

  // Borda de descida: botao acabou de ser pressionado
  if (pressionado && !botaoSegurando) {
    botaoSegurando    = true;
    tBotaoPressionado = agora;
    // Conta o clique ja na descida para que o segurar capture o numero correto
    // So conta se o ultimo clique foi recente (dentro da janela de sequencia)
    if (agora - tUltimoClique < (unsigned long)(TEMPO_CLIQUE_MAX * 3)) {
      contadorCliques++;
    } else {
      contadorCliques = 1; // primeiro clique de uma nova sequencia
    }
    tUltimoClique = agora;
  }

  // Borda de subida: botao foi solto
  if (!pressionado && botaoSegurando) {
    botaoSegurando = false;
    // Se soltou rapido (clique normal), nao faz nada -- ja contou na descida
    // Se soltou depois de segurar, o gesto ja foi executado
  }

  // Segurar: botao mantido por TEMPO_SEGURAR ms -> executa gesto
  if (botaoSegurando && (agora - tBotaoPressionado >= TEMPO_SEGURAR) && !gestoConcluido) {
    gestoConcluido = true;
    Serial.printf("[BTN] Gesto: %d cliques + segurar\n", contadorCliques);
    executarGesto(contadorCliques);
    contadorCliques = 0;
  }

  // Sem segurar: sequencia de cliques sem hold -> troca de pagina
  if (!botaoSegurando && !gestoConcluido && contadorCliques > 0
      && (agora - tUltimoClique > (unsigned long)(TEMPO_CLIQUE_MAX * 2))
      && (agora - tBotaoPressionado > TEMPO_CLIQUE_MAX)) {
    // Se display estava dim ou off, so acorda -- nao muda pagina
    if (displayOff || displayDimmed) {
      acordarDisplay();
      Serial.println("[BTN] Display acordado");
    } else {
      paginaAtual = (paginaAtual + 1) % 3;
      acordarDisplay(); // registra interacao
      Serial.printf("[BTN] Pagina: %d\n", paginaAtual);
    }
    contadorCliques = 0;
  }

  // Reset do flag gestoConcluido quando botao e solto
  if (!pressionado && !botaoSegurando) {
    gestoConcluido = false;
  }
}

void executarGesto(int cliques) {
  acordarDisplay(); // qualquer gesto acorda o display
  switch (cliques) {
    case 0:
      // Gesto nao reconhecido -- ignora silenciosamente
      break;

    case 1:
      galaoAtual_mL = GALAO_TOTAL_ML;
      exibirFeedback("Galao", "reabastecido!");
      enviarEventoParaNuvem("galao_cheio", "Galao reabastecido 7L");
      Serial.println(F("[BTN] Galao reset"));
      break;

    case 2: {
      // Alterna override: se ja ativo, desativa; se inativo, ativa com estado oposto
      if (!luzManualOverride) {
        luzManualOverride = true;
        luzManualEstado   = !luzAtiva;  // oposto do estado atual
        tLuzOverrideMs    = millis();   // marca inicio do override
        const char* msg = luzManualEstado ? "Luz ON (1h)" : "Luz OFF (3h)";
        exibirFeedback("LUZ", msg);
        enviarEventoParaNuvem("toggle_luz", msg);
      } else {
        // Cancela override manualmente -- volta ao automatico agora
        luzManualOverride = false;
        exibirFeedback("LUZ", "Auto retomado");
        enviarEventoParaNuvem("toggle_luz", "Override cancelado");
      }
      break;
    }

    case 3: {
      cicloAtivo   = true;
      cicloStartMs = millis();
      diasCiclo    = 0;
      // Gera ciclo_id baseado no timestamp UNIX atual
      struct tm ti;
      if (getLocalTime(&ti)) {
        cicloId = (unsigned long)mktime(&ti);
      } else {
        cicloId = millis() / 1000;
      }
      exibirFeedback("Ciclo iniciado", "Dia 0");
      // Envia evento com ciclo_id para o site abrir o formulario
      char descCiclo[64];
      snprintf(descCiclo, sizeof(descCiclo), "ciclo_id:%lu", cicloId);
      enviarEventoParaNuvem("start_ciclo", descCiclo);
      Serial.printf("[BTN] Ciclo start id=%lu\n", cicloId);
      break;
    }

    case 4:
      // Envio manual imediato -- nao reseta o timer normal
      exibirFeedback("Enviando", "dados...");
      Serial.println(F("[BTN] Envio manual"));
      if (WiFi.status() == WL_CONNECTED) {
        enviarParaNuvem(true); // true = envio manual
        exibirFeedback("Enviado!", "OK");
      } else {
        exibirFeedback("Sem WiFi", "");
      }
      break;

    default:
      Serial.printf("[BTN] Gesto nao reconhecido: %d\n", cliques);
      break;
  }
}

// ============================================================
// 23. ENVIO PRINCIPAL
// ============================================================
void enviarParaNuvem(bool manual) {
  Serial.println(manual ? F("[NUVEM] Envio manual") : F("[NUVEM] Envio automatico"));

  JsonDocument doc;
  doc["tipo"]           = "dados";
  doc["manual"]         = manual;
  doc["umidade1"]       = umidadeSolo1Pct;
  doc["umidade2"]       = umidadeSolo2Pct;
  doc["alertaBoia"]     = nivelBaixo ? 1 : 0;
  doc["volumeBaseML"]   = (int)volumeBaseIA_mL;   // por vaso
  doc["volumeTotalML"]  = (int)volumeTotal_mL;    // sistema (x2)
  doc["fasePlanta"]     = FASES[faseAtual].nomeGAS;
  doc["diasCiclo"]      = diasCiclo;
  doc["galaoMl"]        = (int)galaoAtual_mL;
  doc["desequilibrio"]  = desequilibrioAtivo ? 1 : 0;
  doc["luzAtiva"]         = luzAtiva ? 1 : 0;
  doc["horasUltimaRega"]  = (int)horasDesdeUltimaRega();

  String payload;
  serializeJson(doc, payload);
  Serial.println(payload);

  // Passo 1: POST para a URL do Apps Script (vai receber redirect 302)
  WiFiClientSecure client1;
  client1.setInsecure();
  HTTPClient http1;
  http1.begin(client1, URL_GAS);
  http1.setTimeout(15000);
  http1.addHeader("Content-Type", "application/json");
  http1.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  int code1 = http1.POST(payload);
  Serial.printf("[NUVEM] HTTP1: %d\n", code1);

  // Passo 2: Segue o redirect manualmente com GET (evita bug do ESP32)
  if (code1 == 302 || code1 == 301) {
    String location = http1.getLocation();
    http1.end();
    Serial.println("[NUVEM] Redirect para: " + location);

    WiFiClientSecure client2;
    client2.setInsecure();
    HTTPClient http2;
    http2.begin(client2, location);
    http2.setTimeout(15000);

    int code2 = http2.GET();
    Serial.printf("[NUVEM] HTTP2: %d\n", code2);

    if (code2 == 200) {
      String resp = http2.getString();
      Serial.println(resp);

      JsonDocument respDoc;
      if (!deserializeJson(respDoc, resp)) {
        float novoFator = respDoc["fator_rega"] | 1.0f;
        float novoVol   = respDoc["volume_ml"]  | 0.0f;
        bool  regar     = respDoc["regar_agora"] | false;
        if (novoFator >= 0.0 && novoFator <= 2.0) fatorRegaGlobal = novoFator;
        if (novoVol > 0) volumeBaseIA_mL = novoVol;
        regarAgora = regar;
        Serial.printf("[IA] fator=%.1f vol=%.0f regar=%d\n", fatorRegaGlobal, volumeBaseIA_mL, regarAgora);
        if (regarAgora && volumeBaseIA_mL > 0) iniciarRega(volumeBaseIA_mL);
      } else {
        float f = resp.toFloat();
        if (f >= 0.0 && f <= 2.0) fatorRegaGlobal = f;
      }
    }
    http2.end();
  } else {
    Serial.printf("[NUVEM] Resposta inesperada: %d\n", code1);
    http1.end();
  }

  if (!manual) tAnteriorNuvem = millis();
}

// ============================================================
// 24. ENVIO DE EVENTO
// ============================================================
void enviarEventoParaNuvem(const char* tipo, const char* desc) {
  if (WiFi.status() != WL_CONNECTED) return;
  int hora = 0, minuto = 0;
  obterHora(hora, minuto);

  JsonDocument doc;
  doc["tipo"]       = "evento";
  doc["evento"]     = tipo;
  doc["descricao"]  = desc;
  doc["hora"]       = hora;
  doc["minuto"]     = minuto;
  doc["fasePlanta"] = FASES[faseAtual].nomeGAS;
  doc["diasCiclo"]  = diasCiclo;

  String payload;
  serializeJson(doc, payload);

  // POST + redirect manual (evita bug do ESP32 com Google)
  WiFiClientSecure evtClient1;
  evtClient1.setInsecure();
  HTTPClient evtHttp1;
  evtHttp1.begin(evtClient1, URL_GAS);
  evtHttp1.setTimeout(10000);
  evtHttp1.addHeader("Content-Type", "application/json");
  evtHttp1.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  int evtCode = evtHttp1.POST(payload);
  if (evtCode == 302 || evtCode == 301) {
    String loc = evtHttp1.getLocation();
    evtHttp1.end();
    WiFiClientSecure evtClient2;
    evtClient2.setInsecure();
    HTTPClient evtHttp2;
    evtHttp2.begin(evtClient2, loc);
    evtHttp2.setTimeout(10000);
    evtHttp2.GET();
    evtHttp2.end();
  } else {
    evtHttp1.end();
  }
  Serial.printf("[EVENTO] %s: %s\n", tipo, desc);
}
