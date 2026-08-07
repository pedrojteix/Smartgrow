#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// 1. PINOS E CONFIGURAÇÃO
// ==========================================
#define I2C_SDA 21
#define I2C_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define PINO_FLUXO 25
#define PINO_BOOT 0
#define RELE_LUZ 18
#define RELE_BOMBA 19
#define RELE_ON LOW
#define RELE_OFF HIGH

// ==========================================
// 2. VARIÁVEIS DE CALIBRAÇÃO (DADOS REAIS)
// ==========================================
float fatorK_atual = 58.62; // Pulsos/mL descobertos no teste anterior

// ==========================================
// 3. VARIÁVEIS DE ESTADO
// ==========================================
int etapaAtual = 2; 
bool luzManualOn = false;
bool rodandoBomba = false;
volatile int pulsosFluxo = 0;
unsigned long tempoInicioTeste = 0;
unsigned long duracaoTeste_ms = 0;

// Máquina de Estados do Botão
unsigned long tempoUltimoClique = 0;
int contadorCliques = 0;
bool aguardandoSegurado = false;
unsigned long tempoBotaoPressionado = 0;
bool acaoExecutada = false;

// ==========================================
// 4. INTERRUPÇÕES
// ==========================================
void IRAM_ATTR ISR_ContaPulso() {
  pulsosFluxo++;
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Falha OLED"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(15, 25);
  display.println("CALIBRAR");
  display.display();
  delay(1000);

  pinMode(PINO_BOOT, INPUT_PULLUP);
  pinMode(PINO_FLUXO, INPUT_PULLUP); 
  pinMode(RELE_LUZ, OUTPUT);
  pinMode(RELE_BOMBA, OUTPUT);
  
  digitalWrite(RELE_LUZ, RELE_OFF);
  digitalWrite(RELE_BOMBA, RELE_OFF);

  attachInterrupt(digitalPinToInterrupt(PINO_FLUXO), ISR_ContaPulso, FALLING);
  atualizarDisplay();
}

// ==========================================
// 6. LOOP PRINCIPAL
// ==========================================
void loop() {
  unsigned long tempoAtual = millis();
  bool botaoApertado = (digitalRead(PINO_BOOT) == LOW);

  static bool ultimoEstadoBotao = HIGH;
  
  // Aperto inicial
  if (botaoApertado && ultimoEstadoBotao == HIGH) { 
    tempoBotaoPressionado = tempoAtual;
    acaoExecutada = false;
  } 
  
  // Soltou o botão
  if (!botaoApertado && ultimoEstadoBotao == LOW) { 
    unsigned long duracao = tempoAtual - tempoBotaoPressionado;
    if (duracao < 500 && !acaoExecutada) {
      contadorCliques++;
      tempoUltimoClique = tempoAtual;
    }
    aguardandoSegurado = false;
  }

  // Detecta se segurou por 2 segundos
  if (botaoApertado && (tempoAtual - tempoBotaoPressionado > 2000) && !acaoExecutada) {
    if (contadorCliques == 1) { 
      // 1 Clique + Segurar 2s = AVANÇAR
      proximaEtapa();
      contadorCliques = 0;
    } else { 
      // Apenas Segurar 2s = RESET
      resetarTeste();
    }
    acaoExecutada = true;
  }

  // Processa cliques após timeout (400ms sem novo clique)
  if (!botaoApertado && contadorCliques > 0 && (tempoAtual - tempoUltimoClique > 400)) {
    if (contadorCliques == 1) acaoCurta();
    else if (contadorCliques == 3) toggleLuz();
    contadorCliques = 0;
  }
  
  ultimoEstadoBotao = !botaoApertado;

  // Desligamento automático do Teste 2 (10s)
  if (etapaAtual == 2 && rodandoBomba) {
    if (tempoAtual - tempoInicioTeste >= 10000) {
      pararBomba();
    }
  }
  
  // Refresh dinâmico para o Teste 3 (Atualiza o visor a cada 500ms)
  if (etapaAtual == 3 && rodandoBomba) {
    static unsigned long ultimoRefresh = 0;
    if (tempoAtual - ultimoRefresh > 500) {
      atualizarDisplay();
      ultimoRefresh = tempoAtual;
    }
  }
}

// ==========================================
// 7. FUNÇÕES DE COMANDO
// ==========================================
void acaoCurta() {
  if (!rodandoBomba) {
    digitalWrite(RELE_BOMBA, RELE_ON);
    rodandoBomba = true;
    tempoInicioTeste = millis();
    if (etapaAtual == 3) { noInterrupts(); pulsosFluxo = 0; interrupts(); }
  } else {
    pararBomba();
  }
  atualizarDisplay();
}

void pararBomba() {
  digitalWrite(RELE_BOMBA, RELE_OFF);
  if (rodandoBomba && etapaAtual == 3) duracaoTeste_ms = millis() - tempoInicioTeste;
  rodandoBomba = false;
  atualizarDisplay();
}

void toggleLuz() {
  luzManualOn = !luzManualOn;
  digitalWrite(RELE_LUZ, luzManualOn ? RELE_ON : RELE_OFF);
  atualizarDisplay();
}

void resetarTeste() {
  pararBomba();
  duracaoTeste_ms = 0;
  noInterrupts(); pulsosFluxo = 0; interrupts();
  atualizarDisplay();
}

void proximaEtapa() {
  pararBomba();
  etapaAtual = (etapaAtual == 2) ? 3 : 2;
  atualizarDisplay();
}

// ==========================================
// 8. RENDERIZAÇÃO DO DISPLAY
// ==========================================
void atualizarDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  
  // Status da Luz no topo
  display.print("LUZ: "); display.println(luzManualOn ? "ON [3-Clicks]" : "OFF");
  display.println("---------------------");

  if (etapaAtual == 2) {
    display.setTextSize(2); display.println("SIMETRIA");
    display.setTextSize(1); display.println("10s de bomba");
    if (rodandoBomba) display.println("\nSTATUS: RODANDO...");
    else display.println("\n1 Click p/ iniciar");
  } 
  else {
    display.setTextSize(2); display.println("FATOR K");
    display.setTextSize(1);
    
    // Leitura segura dos pulsos
    noInterrupts(); int p = pulsosFluxo; interrupts();
    
    // Cálculos de tempo e volume esperado
    float s = rodandoBomba ? (millis() - tempoInicioTeste)/1000.0 : duracaoTeste_ms/1000.0;
    float volumeCalculado = p / fatorK_atual; // mL baseados no Fator K atual
    
    display.print("Pulsos: "); display.println(p);
    display.print("Tempo : "); display.print(s, 1); display.println(" s");
    display.print("Vol.  : "); display.print(volumeCalculado, 1); display.println(" mL");
    
    if (s > 0) {
       display.print("Vazao : "); display.print(volumeCalculado/s, 2); display.println(" mL/s");
    } else {
       display.println("Vazao : 0.00 mL/s");
    }
  }
  display.display();
}