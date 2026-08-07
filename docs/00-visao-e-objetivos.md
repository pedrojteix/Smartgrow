# 00 — Visão e Objetivos

## O problema

Cultivo indoor de Cannabis é um problema de controle com quatro variáveis acopladas —
água, luz, temperatura e umidade do ar — e uma constante de tempo longa (dias a semanas).
O erro só aparece depois que já causou dano. Regar demais apodrece a raiz; regar de menos
trava o crescimento; PPFD errado na fase errada custa produção inteira.

O cultivador humano decide por intuição e olhômetro. O SmartGrow existe para substituir
isso por **medição, decisão registrada e execução automática**, com histórico auditável.

## O que o sistema faz

1. **Mede**: umidade do solo (2 sensores capacitivos), temperatura do solo (DS18B20),
   temperatura/umidade/pressão do ar (BME280 → calcula VPD), volume bombeado (2 sensores de fluxo).
2. **Decide**: envia o estado à nuvem a cada 15 min; o Apps Script monta um prompt com
   contexto agronômico completo e consulta o Gemini, que retorna `regar_agora`, `volume_ml`,
   `fator_rega`, diagnóstico e alerta de longo prazo.
3. **Executa**: aciona as bombas pelo tempo calculado, controla o fotoperíodo da luminária
   conforme a fase, respeita limites e bloqueios de segurança.
4. **Registra**: grava tudo em Google Sheets (dados, eventos, regas) e exibe num dashboard
   web público com gráficos, cards e comandos remotos.
5. **Permite intervenção**: gestos no botão BOOT do ESP32 (calibração, rega manual, toggle
   de luz, reset do galão) e comandos remotos pelo site (rega remota, bloqueio de bomba,
   nota do cultivador que entra no prompt da IA).

## O que deu certo na v1

- O laço completo **funciona de ponta a ponta**, com plantas reais, por meses.
- A rega por **tempo** (e não por pulso de fluxo) provou-se o único critério confiável.
- A **calibração por gesto** no botão BOOT, com resultado no display, permitiu levantar
  parâmetros sem Serial Monitor — foi o que destravou a hidráulica.
- O **dashboard** virou a interface principal de operação; o display OLED virou secundário.
- A **nota do cultivador** injetada no prompt deu à IA contexto que nenhum sensor captura.

## O que deu errado (e define o 2.0)

| Problema | Consequência | O que o 2.0 precisa garantir |
|---|---|---|
| Decisão de rega **depende da nuvem** | Bug de HTTP travou a rega automática ~1 mês (jun/26). Solo seco, IA "querendo" regar, nada acontecia. | Decisão de rega **local por padrão**. Nuvem é conselho e telemetria, nunca caminho crítico. |
| Firmware **monolítico** (1 arquivo, ~1650 linhas, 24 seções) | Toda mudança arrisca tudo. Bugs de escopo, chaves desbalanceadas e regressões recorrentes. | Módulos independentes, com interface explícita e testáveis fora do hardware. |
| **Zero testes** | Só se descobre bug com bomba ligada e planta molhada. | Lógica pura (VPD, decisão de rega, agendamento de luz, máquina de gestos) testável em host. |
| **Dois vasos hardcoded** em todo lugar | Planta 1 morreu (macho) e o sistema inteiro ficou inconsistente. Desequilíbrio hidráulico virou ruído. | Modelo de **N zonas configuráveis**, com sensores e bombas mapeados por configuração. |
| **Configuração dentro do código** | Trocar WiFi, fase ou calibração = recompilar e regravar. | Configuração persistida em NVS, editável em runtime (display/site), sem recompilar. |
| **Estado volátil** | `tUltimaRega_ms` zera no boot → `horasUltimaRega=999` → IA cega. | Estado crítico persistido com timestamp real (epoch), não `millis()`. |
| **Segredos no código-fonte** | Chave do Gemini, senha do WiFi e URL do GAS versionados em texto puro. | Segredos fora do repositório versionado. |
| Sem observabilidade | Debug via Serial Monitor e foto do display. | Log estruturado, contadores de falha, healthcheck no dashboard. |

## Objetivos do SmartGrow 2.0

**Primários**

1. **Autonomia real**: 30 dias sem internet e a planta continua sendo regada corretamente.
2. **Modularidade**: cada subsistema (sensores, rega, luz, display, gestos, nuvem, config)
   isolado, com contrato claro, substituível sem tocar nos outros.
3. **Testabilidade**: toda regra de decisão coberta por teste que roda sem hardware.
4. **Configurabilidade**: número de zonas, fases, limiares e calibrações mudam sem recompilar.
5. **Segurança do hardware**: guardas de rega, watchdog, e nenhuma condição em que a bomba
   possa ficar ligada indefinidamente.

**Secundários**

6. IA como **camada de conselho e diagnóstico**, com o firmware autorizado a ignorá-la.
7. Backend com contrato de API versionado e testável.
8. Dashboard com estado de saúde do sistema visível (online/offline, última rega, falhas).
9. Base pronta para **replicação** — o `SGRW_MODELO_NEGOCIOS.pdf` em `referencias/relatorios/`
   indica intenção de produto; a arquitetura não pode assumir "um único grow do Pedro".

**Fora de escopo agora**

- Controle de nutrientes/pH, exaustão e umidificação.
- App mobile nativo.
- Multiusuário/autenticação.

## Critério de sucesso

O 2.0 está pronto quando:

- [ ] O ESP32 rega corretamente com o WiFi desligado por 7 dias.
- [ ] Adicionar uma terceira zona é uma mudança de configuração, não de código.
- [ ] `pio test` roda verde e cobre decisão de rega, luz, VPD e gestos.
- [ ] Trocar a fase de vegetação para floração não exige regravar o firmware.
- [ ] O dashboard mostra, sem ambiguidade, se o sistema está saudável.
