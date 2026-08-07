// ============================================================
// SMARTGROW -- Firmware R10 (PROVISORIO -- planta 2 solo, floracao)
// Base: R08
// + Rele SSR trocado -- logica ON/OFF invertida em relacao ao R08
//   (novo modulo aciona com nivel HIGH, nao mais LOW). Afeta luz E bombas,
//   ja que ambas usam os mesmos macros RELE_ON/RELE_OFF do mesmo modulo SSR.
// + Horario da luz conferido: Floracao (fase ativa) ja liga 8h e desliga 20h -- sem mudanca
// --------------------------------------------------------------
// Historico R08:
// Base: R07
// + 2 bombas no MESMO canal CH2/GPIO23 (CH3 e CH4 do rele queimaram)
// + Acionadas JUNTAS na rega -- volume = soma dos 2 sensores de fluxo
// + 2 sensores de fluxo (Fluxo1=GPIO25, Fluxo2=GPIO13)
// + Calibracao movida para 3 cliques+segurar (mostra os 2 fluxos ao vivo)
// + start_ciclo movido para 5 cliques+segurar
// + Luz no CH1/GPIO18
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
#include <Preferences.h>
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
// Sensores de fluxo -- um por bomba
#define PINO_FLUXO1   25   // Fluxo bomba 1
#define PINO_FLUXO2   13   // Fluxo bomba 2 (GPIO14 tem restricoes no ESP32)
#define PINO_BOIA     26
#define PINO_BOOT     0

// Reles -- SSR 4 canais, ativo HIGH (modulo novo, R09 -- logica invertida vs R08/LOW)
#define RELE_LUZ      18   // IN1 -- luz (SSR AC)
// R10: planta 1 (macho) eliminada. Rega somente da planta 2 via BOMBA 2.
// SSR novo (ativo HIGH): CH1=luz(GPIO18), CH2=bomba1(GPIO23, sem uso), CH3=bomba2(GPIO19)
#define RELE_BOMBA1   23   // IN2 (CH2) -- bomba 1 (RESERVA, planta 1 removida)
#define RELE_BOMBAS   19   // IN3 (CH3) -- BOMBA 2 (unica ativa; macro mantida p/ compatibilidade)
#define RELE_ON       HIGH
#define RELE_OFF      LOW

// Sensores adicionais
#define PINO_DS18B20  4    // OneWire -- temperatura da terra (resistor 4.7kOhm entre VCC e DATA)
// BME280 em barramento dedicado Wire2 (GPIO 16=SDA, 17=SCL) -- isolado do display
#define I2C2_SDA      16
#define I2C2_SCL      17

// ============================================================
// 3. CALIBRACAO
// ============================================================
const int SOLO_AR   = 3380;
const int SOLO_AGUA = 1250;

// ============================================================
// 3b. SENSORES BME280 E DS18B20
// ============================================================
TwoWire         Wire2 = TwoWire(1);
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
int faseAtual = 2;

// ============================================================
// 5. REGA
// ============================================================
const float FATOR_DOIS_VASOS       = 2.0;
const float FATOR_CALIBRACAO_FLUXO = 173.92; // Recalibrado com valvulas retentoras (media 2 rodadas: 167.11 e 180.73)
const float VAZAO_ML_S            = 4.2;  // R10: UMA bomba (metade de 8.4). RECALIBRAR com 3 cliques!
const float TIMEOUT_FOLGA         = 1.3;
const int   LIMITE_ALERTA_REGAS   = 2;
const int   LIMITE_BLOQUEAR_REGAS = 3;
int           regasHoje           = 0;
unsigned long tInicioContagem     = 0;
bool          bombaRemotaBloqueada = false;
const int   LIMIAR_DESEQUILIBRIO   = 200;  // R10: DESATIVADO -- os 2 sensores estao no MESMO vaso (planta 2)

float fatorRegaGlobal = 1.0;

// ============================================================
// 5b. R10 -- REGA AUTONOMA LOCAL PARCELADA (sem depender da nuvem)
// Floracao inicial (~2 semanas): quando a umidade media do vaso
// atinge o limiar, executa um ciclo de VOLUME_CICLO_ML dividido em
// NUM_PARCELAS regas espacadas, para o substrato absorver melhor.
// ============================================================
const int   LIMIAR_UMIDADE_REGA   = 35;      // %% media -- dispara o ciclo
const float VOLUME_CICLO_ML       = 800.0;   // total do ciclo (floracao inicial)
const int   NUM_PARCELAS          = 4;       // 4 x 200mL
const unsigned long INTERVALO_PARCELAS_MS = 5UL * 60UL * 1000UL;  // 5 min
const unsigned long COOLDOWN_CICLO_MS     = 20UL * 3600UL * 1000UL; // 20h entre ciclos

bool          cicloRegaAtivo       = false;
int           parcelasRestantes    = 0;
unsigned long tProximaParcela      = 0;
unsigned long tUltimoCicloCompleto = 0;
bool          proximaRegaEhParcela = false;  // parcelas 2+ nao contam no limite diario
float volumeBaseIA_mL = 0.0;  // recomendado pela IA (por vaso)
float volumeTotal_mL  = 0.0;  // volumeBaseIA * 2 (bombeado real)
float volumeTotalML_ultimo = 0.0;   // 0 = nunca regou -- atualizado apos cada rega real
bool  regarAgora      = false;

bool  bombaHabilitada  = true;
bool  executandoRega   = false;
float volumeAlvo_mL    = 0.0;
float volumeAtual_mL   = 0.0;

// Fluxo e rega -- variaveis independentes por bomba
volatile int pulsosFluxo1 = 0;  // bomba 1
volatile int pulsosFluxo2 = 0;  // bomba 2

// Qual bomba esta ativa na rega atual
int  bombaAtiva = 0; // 0=nenhuma, 1=bomba1, 2=bomba2

// Galao individual por bomba (cada uma tem seu reservatorio ou compartilhado)
// Por ora ambas descontam do mesmo galao -- ajustar se tiver reservatorios separados
// galaoAtual_mL e o galao compartilhado

// -- Deteccao de sensor solo falho (queda abrupta > LIMIAR_QUEDA em 1 leitura)
int  ultimoSolo1Pct    = -1;
int  ultimoSolo2Pct    = -1;
int  suspeito1Contagem = 0;
int  suspeito2Contagem = 0;
bool sensor1Falho      = false;
bool sensor2Falho      = false;
const int LIMIAR_QUEDA   = 30;
const int LEITURAS_FALHO = 3;

// -- Rega remota via site
float regaRemotaVolume_mL = 0.0;
bool  regaRemotaPendente  = false;

// -- Rega manual BOOT (case 5) e modo calibracao
bool          regaManualAtiva   = false;
unsigned long tRegaManualInicio = 0;
const unsigned long REGA_MANUAL_MAX_MS = 60000;

// -- Modo calibracao (5 cliques + segurar) -- maquina de 5 estados:
// 0 = inativo
// 1 = aguardando selecao de bomba (mostra menu)
// 2 = bomba rodando (mostra tempo e pulsos ao vivo)
// 3 = bomba parada, aguardando pulsos zerarem (inercia da agua)
// 4 = resultado congelado (mostra resultado, aguarda clique para sair)
unsigned long tRegaTimeoutMs      = 0;
int           estadoCalib         = 0;
unsigned long tInicioCalib        = 0;
unsigned long tInicioRega_ms      = 0;
unsigned long tBombaParou         = 0;    // quando a bomba foi desligada
float         calibDuracaoMs      = 0;    // duracao ATIVA da bomba em ms
int           calibPulsosAnterior  = 0;   // para detectar quando pararam
unsigned long tUltimoPulsoCalib   = 0;    // ultimo momento com pulso novo
const unsigned long CALB_TIMEOUT_MS   = 30000;
const unsigned long CALB_PULSO_TIMEOUT = 3000; // 3s sem pulso = agua parou
bool          modoCalibracao      = false;

// ============================================================
// 6. GALAO
// ============================================================
const float GALAO_TOTAL_ML = 7000.0;
float galaoAtual_mL        = 7000.0;

// Persistencia em NVS (flash) -- sobrevive a reboot/queda de energia.
// Sem isto, galaoAtual_mL volta a 7000 a cada reset (bug do registro de agua).
Preferences prefs;

// Trigger da rega em andamento (para o log estruturado)
char regaTrigger[8] = "auto";  // "auto" | "remota" | "teste" | "manual"

// -- Store-and-forward de regas --------------------------------
// Cada rega vira um registro persistido em NVS e enviado no proximo
// ciclo de telemetria. Garante que nenhuma rega se perca, mesmo que o
// WiFi esteja fora no momento do acionamento (causa-raiz das regas
// automaticas que nao apareciam na tabela).
struct RegaPendente {
  uint8_t  hora;         // hora local da rega (do RTC/NTP)
  uint8_t  minuto;
  uint16_t vaso1_mL;
  uint16_t vaso2_mL;
  uint16_t duracaoS;
  char     trigger[8];
};
const int      FILA_REGAS_MAX = 8;
RegaPendente   filaRegas[FILA_REGAS_MAX];
int            filaRegasN = 0;

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
void IRAM_ATTR ISR_ContaPulso1() { pulsosFluxo1++; }
void IRAM_ATTR ISR_ContaPulso2() { pulsosFluxo2++; }

// Forward declarations
void iniciarRega(float volumeBase_mL, int numBomba = 1);
void gerenciarRegaAutonoma();
void pararBomba(const char* motivo);
void exibirFeedback(const char* l1, const char* l2);
void acordarDisplay();
int  postGAS(const String& payload, String* respOut);
void flushFilaRegas();
void enfileirarRega(const char* trigger, int p1, int p2, int duracaoS);
void salvarFilaRegas();
void carregarFilaRegas();
uint32_t epochAgora();

// ============================================================
// 15. SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Persistencia (NVS): recupera o nivel do galao e a fila de regas
  // pendentes. Sem isto o galao "reabastece" sozinho a cada reboot.
  prefs.begin("sgrw", false);
  galaoAtual_mL        = prefs.getFloat("galao", GALAO_TOTAL_ML);
  volumeTotalML_ultimo = prefs.getFloat("ultRega", 0.0f);
  carregarFilaRegas();
  Serial.printf("[NVS] Galao=%.0fmL | ultRega=%.0fmL | filaRegas=%d\n",
                galaoAtual_mL, volumeTotalML_ultimo, filaRegasN);

  // Display no barramento principal Wire (GPIO 21/22)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(100);

  bool dispOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!dispOk) dispOk = display.begin(SSD1306_EXTERNALVCC, 0x3C);
  if (!dispOk) {
    Serial.println(F("[OLED] Falha -- continuando"));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.display();
    Serial.println(F("[OLED] OK"));
  }

  // BME280 no barramento dedicado Wire2 (GPIO 16/17) -- isolado do display
  Wire2.begin(I2C2_SDA, I2C2_SCL);
  Wire2.setClock(100000);
  delay(200);

  bmeOk = bme.begin(0x76, &Wire2);
  if (!bmeOk) bmeOk = bme.begin(0x77, &Wire2);
  if (bmeOk) {
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2, Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X2, Adafruit_BME280::FILTER_X4,
                    Adafruit_BME280::STANDBY_MS_500);
    Serial.println(F("[BME280] OK em Wire2 (GPIO 16/17)"));
  } else {
    Serial.println(F("[BME280] nao detectado em Wire2"));
  }

  pinMode(PINO_BOIA,   INPUT_PULLUP);
  pinMode(PINO_BOOT,   INPUT_PULLUP);
  pinMode(PINO_FLUXO1, INPUT_PULLUP);
  pinMode(PINO_FLUXO2, INPUT_PULLUP);
  pinMode(RELE_LUZ,    OUTPUT);
  pinMode(RELE_BOMBAS, OUTPUT);
  digitalWrite(RELE_LUZ,     RELE_OFF);
  digitalWrite(RELE_BOMBAS,  RELE_OFF);

  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO1), ISR_ContaPulso1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO2), ISR_ContaPulso2, FALLING);

  // BME280 ja inicializado acima em Wire2

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

  // Aguarda NTP sincronizar e aplica estado correto da luz no boot
  // Tenta por ate 10s para garantir que o horario esteja disponivel
  {
    int hora, minuto;
    unsigned long tEspera = millis();
    while (!obterHora(hora, minuto) && millis() - tEspera < 10000) {
      delay(500);
      Serial.print(".");
    }
    controlarLuz();
    Serial.printf("[LUZ] Estado inicial: %s (hora obtida: %d:%02d)\n",
                  luzAtiva ? "ON" : "OFF", hora, minuto);
  }

  // Inicializa brilho do display e timer de interacao
  setBrilho(BRILHO_NORMAL);
  tUltimaInteracao = millis();

  exibirBoot();
  Serial.println(F("[SGRW] R09 iniciado -- FatorK=173.92 | Rele SSR ativo HIGH | Transplante 30/04/2026"));
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
    gerenciarRegaAutonoma();  // R10: decisao de rega local (nao depende da nuvem)
    controlarLuz();
    verificarSleepDisplay();  // dim/sleep apos inatividade
    if (!displayOff) atualizarDisplay(); // so atualiza se display ativo
  }

  // Durante calibracao (estados 2 e 3) atualiza o display a cada 100ms
  // para mostrar a passagem dos segundos e pulsos em tempo real
  static unsigned long tCalibRefresh = 0;
  if ((estadoCalib == 2 || estadoCalib == 3) && agora - tCalibRefresh >= 100) {
    tCalibRefresh = agora;
    atualizarDisplay();
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
    // Le o fluxo da bomba ativa
    noInterrupts();
    int copia = pulsosFluxo1 + pulsosFluxo2; // soma dos dois fluxos
    interrupts();
    volumeAtual_mL = (float)copia / FATOR_CALIBRACAO_FLUXO;
    if (regaManualAtiva) {
      // Durante a calibracao (estadoCalib != 0) o encerramento e tratado
      // pelo proprio fluxo de calib (botao ou timeout de 120s), evitando
      // contagem dupla da rega de teste em pararBomba.
      if (estadoCalib == 0 && millis() - tRegaManualInicio >= REGA_MANUAL_MAX_MS) {
        pararBomba("Rega manual: timeout 60s");
        regaManualAtiva = false;
      }
    } else {
      bool porVolume = (volumeAtual_mL >= volumeAlvo_mL);
      bool porTempo  = (tRegaTimeoutMs > 0 && millis() >= tRegaTimeoutMs);
      if (porVolume)       pararBomba("Rega concluida (volume)");
      else if (porTempo)   pararBomba("Rega concluida (timeout tempo)");
      else if (nivelBaixo) pararBomba("Nivel baixo");
    }
  }

  // Rega remota pendente (enviada pelo site) -- por ora aciona bomba 1
  if (regaRemotaPendente && !executandoRega) {
    regaRemotaPendente = false;
    float volPorVaso = regaRemotaVolume_mL / FATOR_DOIS_VASOS;
    Serial.printf("[REMOTO] Rega remota: %.0fmL total (%.0fmL/vaso)\n",
                  regaRemotaVolume_mL, volPorVaso);
    strcpy(regaTrigger, "remota");
    iniciarRega(volPorVaso); // as duas bombas juntas
  }

  processarBotao();

  // Timeout do menu de calibracao (estado 1 apenas)
  // (estado 1 / menu de selecao removido -- entra direto no estado 2)
  // Timeout maximo da rega de calibracao (estado 2) -- 120s
  if (estadoCalib == 2 && millis() - tInicioRega_ms > 120000UL) {
    calibDuracaoMs = millis() - tInicioRega_ms;
    noInterrupts();
    int p1 = pulsosFluxo1, p2 = pulsosFluxo2;
    interrupts();
    calibPulsosAnterior = p1 + p2;
    tUltimoPulsoCalib   = millis();
    regaManualAtiva     = false;
    digitalWrite(RELE_BOMBAS, RELE_OFF);
    executandoRega = false;
    estadoCalib = 3;
    Serial.println(F("[CALB] Timeout 120s -- aguardando inercia"));
  }
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
      noInterrupts();
      int p = pulsosFluxo1 + pulsosFluxo2;
      interrupts();
      volumeAtual_mL = (float)p / FATOR_CALIBRACAO_FLUXO;
      if (volumeAtual_mL >= volumeAlvo_mL || nivelBaixo) {
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
  // Leitura bruta dos sensores de solo
  int b1 = analogRead(PINO_SOLO1);
  int lido1 = constrain(map(b1, SOLO_AR, SOLO_AGUA, 0, 100), 0, 100);
  int b2 = analogRead(PINO_SOLO2);
  int lido2 = constrain(map(b2, SOLO_AR, SOLO_AGUA, 0, 100), 0, 100);
  nivelBaixo = false; // BOIA DESATIVADA TEMPORARIAMENTE -- sensor com problema

  // Deteccao de queda abrupta -- sensor saiu do substrato
  // Aplica apenas para QUEDAS, nunca para subidas (pos-rega e normal subir rapido)
  if (ultimoSolo1Pct >= 0 && !sensor1Falho) {
    int queda = ultimoSolo1Pct - lido1;
    if (queda >= LIMIAR_QUEDA) {
      if (++suspeito1Contagem >= LEITURAS_FALHO) {
        sensor1Falho = true;
        Serial.println(F("[SENS] S1 declarado FALHO"));
        enviarEventoParaNuvem("sensor_falho", "S1: queda abrupta detectada");
      }
    } else { suspeito1Contagem = 0; }
  }
  if (ultimoSolo2Pct >= 0 && !sensor2Falho) {
    int queda = ultimoSolo2Pct - lido2;
    if (queda >= LIMIAR_QUEDA) {
      if (++suspeito2Contagem >= LEITURAS_FALHO) {
        sensor2Falho = true;
        Serial.println(F("[SENS] S2 declarado FALHO"));
        enviarEventoParaNuvem("sensor_falho", "S2: queda abrupta detectada");
      }
    } else { suspeito2Contagem = 0; }
  }

  // Aplica valores -- sensor falho usa o outro como referencia
  if (!sensor1Falho && !sensor2Falho) {
    umidadeSolo1Pct = lido1; umidadeSolo2Pct = lido2;
  } else if (sensor1Falho && !sensor2Falho) {
    umidadeSolo1Pct = lido2; umidadeSolo2Pct = lido2;
  } else if (!sensor1Falho && sensor2Falho) {
    umidadeSolo1Pct = lido1; umidadeSolo2Pct = lido1;
  }
  if (!sensor1Falho) ultimoSolo1Pct = lido1;
  if (!sensor2Falho) ultimoSolo2Pct = lido2;

  // BME280 -- via Wire2 dedicado
  if (bmeOk) {
    tempAr     = bme.readTemperature();
    umidadeAr  = bme.readHumidity();
    pressaoHpa = bme.readPressure() / 100.0F;
    vpd        = calcularVPD(tempAr, umidadeAr);
    Serial.printf("[BME280] T=%.1fC H=%.1f%% P=%.1fhPa VPD=%.2fkPa\n",
                  tempAr, umidadeAr, pressaoHpa, vpd);
  }

  // DS18B20
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
int           pulsosInicioRega = 0;

// ATENCAO: CH3 e CH4 do rele queimaram. As duas bombas estao no MESMO canal
// R10 -- gerenciador da rega autonoma parcelada. Chamado no loop a cada ciclo
// de sensores. Dispara o ciclo quando a media atinge o limiar e agenda as
// parcelas seguintes apos cada parcela concluida.
void gerenciarRegaAutonoma() {
  int media = (umidadeSolo1Pct + umidadeSolo2Pct) / 2;

  // Disparo de um novo ciclo
  if (!cicloRegaAtivo && !executandoRega && !regaManualAtiva && estadoCalib == 0) {
    bool cooldownOk = (tUltimoCicloCompleto == 0) ||
                      (millis() - tUltimoCicloCompleto >= COOLDOWN_CICLO_MS);
    bool galaoOk    = (galaoAtual_mL >= VOLUME_CICLO_ML);
    if (media <= LIMIAR_UMIDADE_REGA && cooldownOk && galaoOk &&
        !bombaRemotaBloqueada && !nivelBaixo) {
      cicloRegaAtivo    = true;
      parcelasRestantes = NUM_PARCELAS;
      Serial.printf("[AUTO] Umidade %d%% <= %d%% -- ciclo de %.0fmL em %d parcelas\n",
                    media, LIMIAR_UMIDADE_REGA, VOLUME_CICLO_ML, NUM_PARCELAS);
      if (WiFi.status() == WL_CONNECTED) {
        char d[96];
        snprintf(d, sizeof(d), "Ciclo autonomo: umidade %d%%, %.0fmL em %dx%.0fmL",
                 media, VOLUME_CICLO_ML, NUM_PARCELAS, VOLUME_CICLO_ML/NUM_PARCELAS);
        enviarEventoParaNuvem("rega_autonoma", d);
      }
      strcpy(regaTrigger, "auto");
      iniciarRega(VOLUME_CICLO_ML / NUM_PARCELAS, 1);  // 1a parcela (conta no limite diario)
      if (executandoRega) {
        parcelasRestantes--;
      } else {
        // guarda bloqueou (limite diario etc.) -- cancela o ciclo
        cicloRegaAtivo = false;
        parcelasRestantes = 0;
      }
    }
  }

  // Proximas parcelas do ciclo em andamento
  if (cicloRegaAtivo && parcelasRestantes > 0 && !executandoRega &&
      millis() >= tProximaParcela && tProximaParcela > 0) {
    Serial.printf("[AUTO] Parcela %d/%d\n",
                  NUM_PARCELAS - parcelasRestantes + 1, NUM_PARCELAS);
    proximaRegaEhParcela = true;
    strcpy(regaTrigger, "auto");
    iniciarRega(VOLUME_CICLO_ML / NUM_PARCELAS, 1);
    if (executandoRega) {
      parcelasRestantes--;
      tProximaParcela = 0;
    } else {
      proximaRegaEhParcela = false;
      cicloRegaAtivo = false;  // bloqueio inesperado -- aborta ciclo
    }
  }
}

// (CH2 = RELE_BOMBAS = GPIO23) e sao acionadas JUNTAS. O volume medido e a
// SOMA dos dois sensores de fluxo. O parametro numBomba e ignorado (legado).
void iniciarRega(float volumeBase_mL, int numBomba) {
  if (!bombaHabilitada) { Serial.println(F("[BOMBA] Desabilitada")); return; }
  if (nivelBaixo)       { Serial.println(F("[BOMBA] Nivel baixo")); return; }
  if (executandoRega)   { Serial.println(F("[BOMBA] Ja regando")); return; }

  // Bloqueio remoto
  if (bombaRemotaBloqueada) {
    Serial.println(F("[BOMBA] Bloqueada remotamente"));
    return;
  }

  // Limite de regas diarias -- reseta contagem a cada 24h
  if (tInicioContagem == 0 || millis() - tInicioContagem > 86400000UL) {
    tInicioContagem = millis();
    regasHoje = 0;
  }
  // R10: parcelas 2+ de um ciclo ja validado nao contam de novo
  if (proximaRegaEhParcela) {
    proximaRegaEhParcela = false;
  } else {
    regasHoje++;
  }
  if (regasHoje > LIMITE_BLOQUEAR_REGAS) {
    Serial.printf("[SEGURANCA] %d regas hoje -- BLOQUEADO\n", regasHoje);
    enviarEventoParaNuvem("alerta_rega", "BLOQUEIO: limite de regas diarias atingido");
    return;
  }
  if (regasHoje > LIMITE_ALERTA_REGAS) {
    Serial.printf("[SEGURANCA] %d regas hoje -- alerta\n", regasHoje);
    enviarEventoParaNuvem("alerta_rega", "ALERTA: numero de regas acima do esperado");
  }

  bombaAtiva      = 1;
  volumeBaseIA_mL = volumeBase_mL;
  volumeAlvo_mL   = volumeBase_mL;   // R10: 1 vaso apenas -- sem multiplicar por 2
  volumeAtual_mL  = 0.0;

  // Timeout por tempo calibrado (mais confiavel que pulsos)
  float tempoCalculado = volumeBase_mL / VAZAO_ML_S;
  tRegaTimeoutMs = millis() + (unsigned long)(tempoCalculado * TIMEOUT_FOLGA * 1000);
  Serial.printf("[BOMBAS] Timeout: %.0fs\n", tempoCalculado * TIMEOUT_FOLGA);

  noInterrupts(); pulsosFluxo1 = 0; pulsosFluxo2 = 0; interrupts();
  tInicioRega_ms = millis();

  executandoRega = true;
  digitalWrite(RELE_BOMBAS, RELE_ON);
  Serial.printf("[BOMBAS] Inicio. Base=%.0fmL/vaso Alvo=%.0fmL total\n",
                volumeBase_mL, volumeAlvo_mL);

  if (WiFi.status() == WL_CONNECTED) {
    int h = 0, m = 0, s = 0;
    struct tm ti;
    if (getLocalTime(&ti)) { h = ti.tm_hour; m = ti.tm_min; s = ti.tm_sec; }
    char desc[80];
    snprintf(desc, sizeof(desc), "Bombas ON %02d:%02d:%02d -- alvo %.0fmL/vaso",
             h, m, s, volumeBase_mL);
    enviarEventoParaNuvem("bomba_on", desc);
  }
}

void pararBomba(const char* motivo) {
  digitalWrite(RELE_BOMBAS, RELE_OFF);

  // Volume total = soma dos dois sensores de fluxo
  noInterrupts(); int p1 = pulsosFluxo1, p2 = pulsosFluxo2; interrupts();
  int pulsosTotal = p1 + p2;
  volumeTotal_mL       = (float)pulsosTotal / FATOR_CALIBRACAO_FLUXO;
  volumeTotalML_ultimo = volumeTotal_mL;
  galaoAtual_mL = max(0.0f, galaoAtual_mL - volumeTotal_mL);
  executandoRega = false;
  tUltimaRega_ms = millis();

  // Persiste o nivel do galao e o volume da ultima rega (NVS)
  prefs.putFloat("galao",   galaoAtual_mL);
  prefs.putFloat("ultRega", volumeTotalML_ultimo);

  unsigned long duracaoMs = millis() - tInicioRega_ms;
  int           duracaoS  = (int)(duracaoMs / 1000);

  // Registra a rega no log estruturado (store-and-forward -> aba REGAS)
  enfileirarRega(regaTrigger, p1, p2, duracaoS);

  Serial.printf("[BOMBAS] Parada: %s | %.0fmL | Galao: %.0fmL | %ds | B1=%dp B2=%dp\n",
                motivo, volumeTotal_mL, galaoAtual_mL, duracaoS, p1, p2);

  if (WiFi.status() == WL_CONNECTED) {
    int h = 0, m = 0, s = 0;
    struct tm ti;
    if (getLocalTime(&ti)) { h = ti.tm_hour; m = ti.tm_min; s = ti.tm_sec; }
    char desc[128];
    snprintf(desc, sizeof(desc),
             "Bombas OFF %02d:%02d:%02d | %ds | B1=%dp B2=%dp | %.0fmL total",
             h, m, s, duracaoS, p1, p2, volumeTotal_mL);
    enviarEventoParaNuvem("bomba_off", desc);
  }
  bombaAtiva = 0;

  // R10: fecha parcela do ciclo autonomo
  if (cicloRegaAtivo) {
    if (parcelasRestantes > 0) {
      tProximaParcela = millis() + INTERVALO_PARCELAS_MS;
      Serial.printf("[AUTO] Proxima parcela em %lu min\n", INTERVALO_PARCELAS_MS/60000UL);
    } else {
      cicloRegaAtivo = false;
      tUltimoCicloCompleto = millis();
      Serial.println(F("[AUTO] Ciclo de rega completo"));
      if (WiFi.status() == WL_CONNECTED)
        enviarEventoParaNuvem("rega_autonoma", "Ciclo completo -- 4 parcelas executadas");
    }
  }
}

// ============================================================
// 21. DISPLAY
// ============================================================
const int DISP_X = 58;  // Validado nas fotos dos testes
const int DISP_W = 70;

void atualizarDisplay() {
  // Estado 1: menu de calibracao
  // Estado 2: AS DUAS bombas rodando -- mostra os dois fluxos ao vivo
  if (estadoCalib == 2) {
    float seg = (millis() - tInicioRega_ms) / 1000.0;
    noInterrupts();
    int p1 = pulsosFluxo1;
    int p2 = pulsosFluxo2;
    interrupts();
    float v1 = (float)p1 / FATOR_CALIBRACAO_FLUXO;
    float v2 = (float)p2 / FATOR_CALIBRACAO_FLUXO;
    display.clearDisplay();
    display.drawBitmap(0, 0, epd_bitmap_Folha, 128, 64, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(DISP_X, 0);
    display.print("T:"); display.print(seg, 2); display.print("s");
    display.setCursor(DISP_X, 12);
    display.print("B1 p:"); display.print(p1);
    display.setCursor(DISP_X, 23);
    display.print("  "); display.print(v1, 1); display.print("mL");
    display.setCursor(DISP_X, 35);
    display.print("B2 p:"); display.print(p2);
    display.setCursor(DISP_X, 46);
    display.print("  "); display.print(v2, 1); display.print("mL");
    display.setCursor(DISP_X, 57);
    display.print("clique=parar");
    display.display();
    return;
  }
  // Estado 3: bombas paradas, aguardando inercia dos dois fluxos
  if (estadoCalib == 3) {
    noInterrupts();
    int p1 = pulsosFluxo1;
    int p2 = pulsosFluxo2;
    interrupts();
    float v1 = (float)p1 / FATOR_CALIBRACAO_FLUXO;
    float v2 = (float)p2 / FATOR_CALIBRACAO_FLUXO;
    float duracaoAtiva = calibDuracaoMs / 1000.0;
    // Detecta quando os DOIS fluxos pararam de crescer
    int somaPulsos = p1 + p2;
    if (somaPulsos > calibPulsosAnterior) {
      tUltimoPulsoCalib   = millis();
      calibPulsosAnterior = somaPulsos;
    }
    bool aguaParou = (millis() - tUltimoPulsoCalib >= CALB_PULSO_TIMEOUT);
    display.clearDisplay();
    display.drawBitmap(0, 0, epd_bitmap_Folha, 128, 64, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(DISP_X, 0);
    display.print(aguaParou ? "PRONTO" : "ESCOANDO");
    display.setCursor(DISP_X, 11);
    display.print("T:"); display.print(duracaoAtiva, 2); display.print("s");
    display.setCursor(DISP_X, 23);
    display.print("B1:"); display.print(p1); display.print("p ");
    display.print(v1, 1);
    display.setCursor(DISP_X, 35);
    display.print("B2:"); display.print(p2); display.print("p ");
    display.print(v2, 1);
    display.setCursor(DISP_X, 50);
    display.print(aguaParou ? "clique=ver" : "aguarde...");
    display.display();
    if (aguaParou) {
      Serial.printf("[CALB] FINAL | T_ativo=%.2fs | B1=%dp(%.1fmL) | B2=%dp(%.1fmL) | FatorK=%.2f\n",
                    duracaoAtiva, p1, v1, p2, v2, FATOR_CALIBRACAO_FLUXO);
      // A rega de teste tambem consome agua real -- desconta do galao e registra
      float totalTeste     = v1 + v2;
      galaoAtual_mL        = max(0.0f, galaoAtual_mL - totalTeste);
      volumeTotalML_ultimo = totalTeste;
      prefs.putFloat("galao",   galaoAtual_mL);
      prefs.putFloat("ultRega", volumeTotalML_ultimo);
      enfileirarRega("teste", p1, p2, (int)duracaoAtiva);
      estadoCalib = 4;
    }
    return;
  }
  // Estado 4: resultado congelado -- nao atualiza
  if (estadoCalib == 4) return;

  // Normal
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
  display.setCursor(DISP_X, 2);
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
  display.setCursor(DISP_X, 14);
  display.print(f.horasLuz);
  display.print("h ON ");
  if (f.horaLigar < 10) display.print("0");
  display.print(f.horaLigar);
  display.print("h");

  // Linha OFF
  display.setCursor(DISP_X, 26);
  display.print(24 - f.horasLuz);
  display.print("h OFF ");
  if (f.horaDesligar == 0) display.print("00");
  else {
    if (f.horaDesligar < 10) display.print("0");
    display.print(f.horaDesligar);
  }
  display.print("h");

  // R10: PPFD alvo e distancia da Quantum Board por fase
  // Germinacao 100-300umol/45-50cm | Vegetacao 500-600/25-30cm | Floracao 800-900/10-15cm
  display.setCursor(DISP_X, 40);
  if (faseAtual == 2)      display.print("PPFD 800-900");
  else if (faseAtual == 1) display.print("PPFD 500-600");
  else                     display.print("PPFD 100-300");
  display.setCursor(DISP_X, 52);
  if (faseAtual == 2)      display.print("Dist 10-15cm");
  else if (faseAtual == 1) display.print("Dist 25-30cm");
  else                     display.print("Dist 45-50cm");
}

void exibirPaginaSensores() {
  display.setTextSize(1);

  // Layout pagina sensores -- 6 linhas, Y: 0, 11, 22, 33, 44, 55
  // S1 na linha 0, S2 na linha 1 -- cada um na sua linha para nao transbordar

  // S1
  display.setCursor(DISP_X, 0);
  if (sensor1Falho) {
    display.print("S1: FALHO");
  } else {
    display.print("S1:");
    if (umidadeSolo1Pct < 10) display.print("0");
    display.print(umidadeSolo1Pct); display.print("%");
  }

  // S2
  display.setCursor(DISP_X, 11);
  if (sensor2Falho) {
    display.print("S2: FALHO");
  } else {
    display.print("S2:");
    if (umidadeSolo2Pct < 10) display.print("0");
    display.print(umidadeSolo2Pct); display.print("%");
  }

  // Umidade do ar
  display.setCursor(DISP_X, 22);
  if (bmeOk) {
    display.print("Um.Ar:");
    display.print((int)umidadeAr); display.print("%");
  } else {
    display.print("Um.Ar: --");
  }

  // Temperatura do ar
  display.setCursor(DISP_X, 33);
  if (bmeOk) {
    display.print("T.Ar:");
    display.print((int)tempAr); display.print("C");
  } else {
    display.print("T.Ar: --");
  }

  // Temperatura da terra
  display.setCursor(DISP_X, 44);
  if (ds18Ok) {
    display.print("Terra:");
    display.print((int)tempTerra); display.print("C");
  } else {
    display.print("Terra: --");
  }

  // VPD ou SEM WIFI
  display.setCursor(DISP_X, 55);
  if (bmeOk) {
    display.print("VPD:");
    display.print(vpd, 2); display.print("kPa");
  } else if (WiFi.status() != WL_CONNECTED) {
    display.print("SEM WIFI");
  } else {
    display.print("VPD: --");
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
  display.setCursor(20, 20); display.print("SmartGrow R09");
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
      noInterrupts(); int p = pulsosFluxo1 + pulsosFluxo2; interrupts();
      volumeAtual_mL = (float)p / FATOR_CALIBRACAO_FLUXO;
      if (volumeAtual_mL >= volumeAlvo_mL || nivelBaixo) {
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
    // Estado 1: menu de selecao -- 1 clique = B1, 2 cliques = B2
    // Estado 2: as duas bombas rodando -- clique para e vai aguardar inercia
    if (estadoCalib == 2) {
      calibDuracaoMs = millis() - tInicioRega_ms;
      noInterrupts();
      int p1 = pulsosFluxo1, p2 = pulsosFluxo2;
      interrupts();
      calibPulsosAnterior = p1 + p2;
      tUltimoPulsoCalib   = millis();
      regaManualAtiva     = false;
      digitalWrite(RELE_BOMBAS, RELE_OFF);
      executandoRega = false;
      estadoCalib    = 3;
      Serial.printf("[CALB] Bombas pararam | %.2fs ativos | B1=%dp B2=%dp\n",
                    calibDuracaoMs/1000.0, p1, p2);
    // Estado 3: aguardando agua parar -- clique ignorado
    } else if (estadoCalib == 3) {
      Serial.println(F("[CALB] Aguardando agua parar..."));
    // Estado 4: resultado congelado -- clique sai
    } else if (estadoCalib == 4) {
      estadoCalib     = 0;
      modoCalibracao  = false;
      regaManualAtiva = false;
      acordarDisplay();
      Serial.println(F("[CALB] Saindo do modo calibracao"));
        } else if (displayOff || displayDimmed) {
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
  if (!pressionado && !botaoSegurando &&
      (agora - tBotaoPressionado > (unsigned long)(TEMPO_SEGURAR + 500))) {
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
      prefs.putFloat("galao", galaoAtual_mL);   // persiste o reabastecimento
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

    case 3:
      // Modo calibracao de rega (movido de 5 para 3 cliques)
      // Aciona AS DUAS bombas juntas e mostra os dois fluxos ao vivo
      if (executandoRega) { exibirFeedback("Ja regando", "aguarde"); break; }
      estadoCalib    = 2;  // vai direto para rodando (sem menu de selecao)
      modoCalibracao = true;
      // Zera os dois contadores de fluxo
      noInterrupts(); pulsosFluxo1 = 0; pulsosFluxo2 = 0; interrupts();
      calibPulsosAnterior = 0;
      tUltimoPulsoCalib   = millis();
      tInicioRega_ms      = millis();
      regaManualAtiva     = true;
      tRegaManualInicio   = millis();
      executandoRega      = true;
      digitalWrite(RELE_BOMBAS, RELE_ON);
      contadorCliques = 0;
      display.clearDisplay();
      display.drawBitmap(0, 0, epd_bitmap_Folha, 128, 64, SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(DISP_X, 16); display.print("CALIBRANDO");
      display.setCursor(DISP_X, 30); display.print("2 bombas ON");
      display.setCursor(DISP_X, 44); display.print("clique=parar");
      display.display();
      Serial.println(F("[BTN] Calibracao 2 bombas -- clique=parar"));
      break;

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

    case 5: {
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

    default:
      Serial.printf("[BTN] Gesto nao reconhecido: %d\n", cliques);
      break;
  }
}

// ============================================================
// 23. ENVIO PRINCIPAL
// ============================================================
// -- Helper HTTP unico --------------------------------------------------
// Faz POST text/plain ao Apps Script e segue manualmente o redirect 302
// com GET (workaround do bug de redirect TLS do ESP32). Centralizar o
// fluxo TLS num so lugar reduz fragmentacao de heap e duplicacao de codigo.
// Retorna o codigo HTTP efetivo (200 = ok). Preenche *respOut se != nullptr.
uint32_t epochAgora() {
  struct tm ti;
  if (getLocalTime(&ti)) return (uint32_t)mktime(&ti);
  return 0;
}

int postGAS(const String& payload, String* respOut) {
  if (respOut) *respOut = "";
  if (WiFi.status() != WL_CONNECTED) return -1;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);
  HTTPClient http;
  http.begin(client, URL_GAS);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "text/plain");
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  int code = http.POST(payload);
  if (code == 301 || code == 302) {
    String loc = http.getLocation();
    http.end();
    WiFiClientSecure c2;
    c2.setInsecure();
    c2.setTimeout(15);
    HTTPClient h2;
    h2.begin(c2, loc);
    h2.setTimeout(15000);
    int code2 = h2.GET();
    if (respOut && code2 == 200) *respOut = h2.getString();
    h2.end();
    return code2;
  }
  if (respOut && code == 200) *respOut = http.getString();
  http.end();
  return code;
}

// -- Fila de regas (store-and-forward, persistida em NVS) ---------------
void salvarFilaRegas() {
  prefs.putInt("filaN", filaRegasN);
  prefs.putBytes("fila", filaRegas, filaRegasN * sizeof(RegaPendente));
}

void carregarFilaRegas() {
  filaRegasN = prefs.getInt("filaN", 0);
  if (filaRegasN < 0 || filaRegasN > FILA_REGAS_MAX) filaRegasN = 0;
  if (filaRegasN > 0) prefs.getBytes("fila", filaRegas, filaRegasN * sizeof(RegaPendente));
}

void enfileirarRega(const char* trigger, int p1, int p2, int duracaoS) {
  RegaPendente r;
  int h = 0, m = 0;
  obterHora(h, m);
  r.hora     = (uint8_t)h;
  r.minuto   = (uint8_t)m;
  r.vaso1_mL = (uint16_t)((float)p1 / FATOR_CALIBRACAO_FLUXO);
  r.vaso2_mL = (uint16_t)((float)p2 / FATOR_CALIBRACAO_FLUXO);
  r.duracaoS = (uint16_t)duracaoS;
  strncpy(r.trigger, trigger, sizeof(r.trigger) - 1);
  r.trigger[sizeof(r.trigger) - 1] = '\0';

  if (filaRegasN < FILA_REGAS_MAX) {
    filaRegas[filaRegasN++] = r;
  } else {
    // Fila cheia: descarta a mais antiga
    for (int i = 1; i < FILA_REGAS_MAX; i++) filaRegas[i - 1] = filaRegas[i];
    filaRegas[FILA_REGAS_MAX - 1] = r;
  }
  salvarFilaRegas();
  Serial.printf("[REGA] Enfileirada (%s): V1=%dmL V2=%dmL | fila=%d\n",
                r.trigger, r.vaso1_mL, r.vaso2_mL, filaRegasN);
}

void flushFilaRegas() {
  if (WiFi.status() != WL_CONNECTED || filaRegasN == 0) return;
  while (filaRegasN > 0) {
    RegaPendente &r = filaRegas[0];
    JsonDocument doc;
    doc["tipo"]       = "rega";
    doc["trigger"]    = r.trigger;
    doc["vaso1_ml"]   = r.vaso1_mL;
    doc["vaso2_ml"]   = r.vaso2_mL;
    doc["total_ml"]   = r.vaso1_mL + r.vaso2_mL;
    doc["duracao_s"]  = r.duracaoS;
    doc["hora"]       = r.hora;
    doc["minuto"]     = r.minuto;
    doc["galaoMl"]    = (int)galaoAtual_mL;
    doc["fasePlanta"] = FASES[faseAtual].nomeGAS;
    doc["diasCiclo"]  = diasCiclo;

    String payload;
    serializeJson(doc, payload);
    if (postGAS(payload, nullptr) != 200) {
      Serial.println(F("[REGA] Flush falhou -- tenta no proximo ciclo"));
      break; // mantem na fila; nao perde a rega
    }
    // Sucesso: remove o primeiro e persiste
    for (int i = 1; i < filaRegasN; i++) filaRegas[i - 1] = filaRegas[i];
    filaRegasN--;
    salvarFilaRegas();
    Serial.printf("[REGA] Enviada ao Sheets | restam %d\n", filaRegasN);
  }
}

void enviarParaNuvem(bool manual) {
  Serial.println(manual ? F("[NUVEM] Envio manual") : F("[NUVEM] Envio automatico"));

  // 1) Drena regas pendentes antes da telemetria (store-and-forward)
  flushFilaRegas();

  // 2) Monta o payload completo de telemetria
  JsonDocument doc;
  doc["tipo"]            = "dados";
  doc["manual"]          = manual;
  doc["umidade1"]        = umidadeSolo1Pct;
  doc["umidade2"]        = umidadeSolo2Pct;
  doc["alertaBoia"]      = nivelBaixo ? 1 : 0;
  doc["volumeBaseML"]    = (int)volumeBaseIA_mL;
  doc["volumeTotalML"]   = (int)volumeTotalML_ultimo;
  doc["desequilibrio"]   = desequilibrioAtivo ? 1 : 0;
  doc["fasePlanta"]      = FASES[faseAtual].nomeGAS;
  doc["diasCiclo"]       = diasCiclo;
  doc["galaoMl"]         = (int)galaoAtual_mL;
  doc["luzAtiva"]        = luzAtiva ? 1 : 0;
  doc["horasUltimaRega"] = (int)horasDesdeUltimaRega();
  // Clima -- so envia se valido (evita gravar NaN/lixo no Sheets)
  if (bmeOk) {
    if (!isnan(tempAr))     doc["tempAr"]     = tempAr;
    if (!isnan(umidadeAr))  doc["umidadeAr"]  = umidadeAr;
    if (!isnan(pressaoHpa)) doc["pressaoHpa"] = pressaoHpa;
    if (!isnan(vpd))        doc["vpd"]        = vpd;
  }
  if (ds18Ok && tempTerra > -100.0) doc["tempTerra"] = tempTerra;

  String payload;
  serializeJson(doc, payload);
  Serial.println(payload);
  Serial.printf("[NUVEM] Payload=%d bytes | Heap=%d\n", payload.length(), ESP.getFreeHeap());

  // 3) Envia e processa a resposta da IA
  String resp;
  int code = postGAS(payload, &resp);
  Serial.printf("[NUVEM] code=%d\n", code);
  if (code != 200 || resp.length() == 0) {
    Serial.println(F("[NUVEM] Sem resposta valida -- mantendo estado"));
    if (!manual) tAnteriorNuvem = millis();
    return;
  }

  JsonDocument respDoc;
  if (deserializeJson(respDoc, resp)) {
    // Fallback: resposta pode ser um float puro (compat. versao antiga)
    float f = resp.toFloat();
    if (f >= 0.0 && f <= 2.0) fatorRegaGlobal = f;
    Serial.println(F("[NUVEM] Resposta nao-JSON"));
    if (!manual) tAnteriorNuvem = millis();
    return;
  }

  float novoFator = respDoc["fator_rega"]  | 1.0f;
  float novoVol   = respDoc["volume_ml"]   | 0.0f;
  bool  regar     = respDoc["regar_agora"] | false;
  if (novoFator >= 0.0 && novoFator <= 2.0) fatorRegaGlobal = novoFator;
  if (novoVol > 0) volumeBaseIA_mL = novoVol;
  regarAgora = regar;
  Serial.printf("[IA] fator=%.2f vol=%.0f regar=%d\n", fatorRegaGlobal, volumeBaseIA_mL, regarAgora);

  // R10: rega da IA DESATIVADA -- decisao agora e local (gerenciarRegaAutonoma).
  // Telemetria continua; reativar quando o sistema de analise voltar.
  if (regarAgora && volumeBaseIA_mL > 0) {
    Serial.println(F("[IA] regar_agora recebido -- IGNORADO (modo autonomo local R10)"));
  }

  // Comando de rega remota do site
  float volRemoto = respDoc["rega_remota_ml"] | 0.0f;
  if (volRemoto > 0 && volRemoto <= GALAO_TOTAL_ML) {
    regaRemotaVolume_mL = volRemoto;
    regaRemotaPendente  = true;
    Serial.printf("[REMOTO] Comando recebido: %.0fmL\n", volRemoto);
  }

  // Bloqueio remoto da bomba (-1 = sem mudanca)
  int bloqueio = respDoc["bomba_bloqueada"] | -1;
  if (bloqueio == 1) {
    bombaRemotaBloqueada = true;
    Serial.println(F("[REMOTO] Bomba BLOQUEADA pelo site"));
  } else if (bloqueio == 0) {
    bombaRemotaBloqueada = false;
    Serial.println(F("[REMOTO] Bomba DESBLOQUEADA pelo site"));
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
  evtHttp1.addHeader("Content-Type", "text/plain");
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
