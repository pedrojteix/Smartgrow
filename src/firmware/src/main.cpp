// ============================================================
// SMARTGROW 2.0 -- main.cpp
//
// FASE 0 do roadmap: este arquivo existe apenas para validar o
// toolchain PlatformIO. Blink + "Hello" no OLED.
//
// A partir da Fase 1, main.cpp deve conter APENAS setup(), loop()
// e o wiring dos modulos de lib/. Nenhuma logica de decisao aqui.
// Ver src/firmware/MODULOS.md.
//
// LEMBRETE: sem caractere acentuado neste arquivo.
// ============================================================

#include <Arduino.h>

// Primeira instrucao do setup(): todos os reles OFF.
// Invariante de seguranca ADR-08 item 2 -- nunca remover.
#define RELE_LUZ     18
#define RELE_BOMBAS  23
#define RELE_ON      HIGH   // SSR novo (R09+). No R08 e anteriores era LOW.
#define RELE_OFF     LOW

void setup() {
  pinMode(RELE_LUZ, OUTPUT);
  pinMode(RELE_BOMBAS, OUTPUT);
  digitalWrite(RELE_LUZ, RELE_OFF);
  digitalWrite(RELE_BOMBAS, RELE_OFF);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("[BOOT] SmartGrow 2.0 -- Fase 0 (toolchain)"));
}

void loop() {
  Serial.println(F("[LOOP] alive"));
  delay(5000);
}
