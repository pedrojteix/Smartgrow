// ============================================================
// SmartGrow 2.0 -- secrets.example.h
//
// COPIE este arquivo para secrets.h e preencha os valores reais.
// secrets.h esta no .gitignore e NUNCA deve ser versionado.
//
// Valores reais estao em docs/10-credenciais-e-endpoints.md
// (que tambem nao vai para repositorio publico).
// ============================================================

#pragma once

// -- Rede ----------------------------------------------------
#define SGRW_WIFI_SSID      "SUA_REDE"
#define SGRW_WIFI_PASSWORD  "SUA_SENHA"

// -- Backend -------------------------------------------------
// URL /exec do Google Apps Script publicado.
// Lembrete: todo update do Apps Script exige um NOVO DEPLOY.
#define SGRW_URL_GAS        "https://script.google.com/macros/s/SEU_ID/exec"

// -- Identificacao do dispositivo ----------------------------
// Usado no payload. Prepara o terreno para multiplos grows.
#define SGRW_DEVICE_ID      "sgrw-01"
