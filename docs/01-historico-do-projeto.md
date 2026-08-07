# 01 — Histórico do Projeto

Reconstruído a partir dos arquivos datados em `legacy/`, dos relatórios em
`referencias/relatorios/` e do log do repositório GitHub.

---

## Linha do tempo

### Abril/2026 — Nascimento e calibração física

| Data | Marco |
|---|---|
| 10–11/04 | Germinação das duas sementes (P2 em 10/04, P1 em 11/04). |
| 18–19/04 | Primeiro vaso. Primeiro sketch: `SGRW_18-04-26_F3_R00.ino` — 15KB, prova de conceito. |
| 19/04 | `Testes_hidraulicos.ino` — sketch isolado para levantar vazão e fator K do fluxo. |
| 21/04 | `Testes_conexaoIA.ino` — prova de conceito da comunicação ESP32 → Apps Script → Gemini. |
| 23–24/04 | **Relatório de calibração `SGRW_DC_CAL_01`**: descoberto que o YF-S401 estava instalado ao contrário; fator K medido em ~46,7 pulsos/mL (vs 5,88 de fábrica); identificado o efeito sifão causando desequilíbrio de até 21,9% entre vasos; proposta de válvulas de retenção. Mapeamento de PPFD da quantum board por distância. |
| 23–24/04 | As duas plantas entram no SmartGrow. |
| 26/04 | `SGRW_ESP32_R04.ino` (35KB) — primeiro firmware "de verdade". |

**Lição estrutural do período**: os testes foram feitos com **sketches isolados** e resultados
lidos no display OLED (sem Serial Monitor). Esse padrão virou a metodologia de debug do projeto.

---

### Maio/2026 — Consolidação do laço completo

| Data | Marco |
|---|---|
| 30/04–01/05 | `SGRW_ESP32_R05` + `SGRW_APPSCR_R05.gs` + `index.html` — primeira tríade completa versionada junta. Backups em texto puro. |
| 03/05 | `R06` firmware + `APPSCR_R06`. Detecção de sensor de solo falho; rega remota via site; correção do bug de luz apagada no boot (NTP). |
| 05/05 | Quatro iterações do R06 em um dia (`R00`→`R03`) — período de debug intenso da hidráulica. |
| 08/05 | Mais quatro iterações. `R03 (ch de rele invertidos)` — descoberta da inversão de lógica dos canais. Fator K recalibrado com válvulas retentoras: **173,92 pulsos/mL**. |
| 08/05 | **`BK_Claude_08-05-26.docx`** — primeiro documento de handoff completo. Marca a consciência de que o projeto precisava de documentação para continuidade entre sessões de IA. |
| ~29/05 | Início do **bug HTTP -11**: todo POST ao Apps Script passa a falhar com `CONNECTION_REFUSED`. Rega automática para. |

---

### Junho/2026 — Crise e reorganização

| Data | Marco |
|---|---|
| 13/06 | `SGRW_ESP32_R07.ino` (57KB, ~1494 linhas). Tentativas de correção do HTTP -11: `Content-Type: text/plain`, log de heap antes do POST. |
| — | Diagnóstico do -11 por eliminação: WiFi OK, heap OK (212KB livres), TCP/TLS connect OK, GET isolado funciona, POST sempre falha. Descartados: créditos Google, DNS, memória, tamanho de payload. Suspeita recaiu sobre o HTTPClient do core ESP32 3.3.8. |
| — | **BME280 morto** confirmado (ACK no I2C, todos os registros 0xFF). Causa: reguladora de protoboard entregando 3,6–3,8V. Segundo sensor perdido pelo mesmo motivo. |
| — | **CH3 e CH4 do relé mecânico queimados** — contato soldado por pico reverso de motor DC sem diodo flyback. As duas bombas passam a dividir o CH2. |
| 20–21/06 | **Migração para Claude Code**: o repositório `pedrojteix/Smartgrow` deixa de receber só `index.html` e passa a versionar firmware e Apps Script juntos. Commits param de se chamar "Update index.html" e passam a descrever a mudança. Nasce a pasta `Claude/github/Smartgrow`. |
| 20–21/06 | Dashboard ganha card de Hidráulica, correção dos gráficos (janela deslizante de 24h), tabela REGAS, correção de encoding. |

**Este é o ponto de virada do projeto**: de "colar código no Arduino IDE" para "repositório
versionado com histórico legível". A pasta atual, Smartgrow 2.0, é o passo seguinte dessa curva.

---

### Julho/2026 — Estabilização

| Data | Marco |
|---|---|
| 12/07 | `SGRW_ESP32_R08.ino` (~1645 linhas). Duas bombas no mesmo CH2, acionadas juntas; volume = soma dos dois fluxos; calibração movida para 3 cliques + segurar; `start_ciclo` para 5 cliques. |
| 18/07 | **`SGRW_ESP32_R09.ino` — baseline atual.** Relé trocado por SSR novo, **ativo HIGH** (lógica invertida vs R08, afeta luz E bombas). |
| — | **HTTP -11 resolvido no R09**: todo o TLS centralizado no helper `postGAS()` — um único ponto com `WiFiClientSecure` + `setInsecure()` + `setTimeout(15)` + `Content-Type: text/plain` + redirect 302 seguido manualmente com GET. Reduziu fragmentação de heap e duplicação. |
| — | **Fila de regas store-and-forward** persistida em NVS: se a rega acontece offline, ela é enfileirada e enviada ao Sheets quando o WiFi volta. Primeira concessão real ao princípio de "não depender da nuvem". |

---

### Agosto/2026 — Floração e o limite da v1

| Data | Marco |
|---|---|
| ~ago | Planta 1 revela-se **macho** e é eliminada. Os dois sensores capacitivos passam ao mesmo vaso (planta 2). A lógica de desequilíbrio hidráulico perde sentido. |
| — | Entrada em **floração** (`faseAtual = 2`, 12h de luz, 8h→20h). |
| 06/08 | `SGRW_ESP32_R10` — marcado "PROVISORIO". Uma bomba só (CH3/GPIO19), `VAZAO_ML_S` 8.4→4.2, desequilíbrio desativado por gambiarra (`LIMIAR_DESEQUILIBRIO = 200`), e **rega autônoma local parcelada**: quando a média cai a 35%, executa 800mL em 4 parcelas de 200mL espaçadas 5 min, com cooldown de 20h — **sem consultar a nuvem**. |
| 07/08 | Criação desta pasta. Decisão: **reescrita total**, com o R09 como baseline congelado. |

O R10 é sintomático: para mudar de dois vasos para um, foi preciso desativar uma feature com
um valor absurdo (`LIMIAR_DESEQUILIBRIO = 200`), redefinir uma macro mantendo o nome antigo
"para compatibilidade" (`RELE_BOMBAS` apontando para a bomba 2) e inserir um subsistema novo
no meio de um arquivo de 1650 linhas. **A arquitetura acabou.** Daí o 2.0.

---

## Evolução dos artefatos

| Versão | Data | Linhas/tamanho | Marco |
|---|---|---|---|
| F3_R00 | 18/04 | 15 KB | Prova de conceito |
| R04 | 26/04 | 35 KB | Primeiro firmware completo |
| R05 | 01/05 | 42 KB | Tríade firmware+GAS+site versionada junta |
| R06 | 03–08/05 | ~50 KB | Sensor falho, rega remota, fix da luz no boot |
| R07 | 13/06 | 57 KB (~1494 l.) | Bloqueio remoto, limites de rega, tentativas anti-HTTP-11 |
| R08 | 12/07 | 64 KB (~1645 l.) | Duas bombas no CH2, calibração 3 cliques |
| **R09** | **18/07** | **64 KB** | **BASELINE — SSR ativo HIGH, postGAS(), fila NVS** |
| R10 | 06/08 | 70 KB | Experimento: 1 planta, rega autônoma local |

Backend: R05 (30/04) → R06 (03/05) → **R07 (atual)**, 756 linhas.
Dashboard: `index.html` cresceu de 54 KB a 81 KB / 1606 linhas.

---

## O que o histórico ensina sobre o 2.0

1. **O código cresceu 4,5× em 4 meses sem nunca ser reestruturado.** Cada feature entrou
   como seção nova num arquivo único. O custo marginal de mudança está insustentável.
2. **Todo bug caro veio de hardware, não de lógica.** Relé queimado, sensor morto por
   sobretensão, sensor de fluxo invertido, GPIO com função reservada. O firmware do 2.0
   precisa **assumir que o hardware falha** e degradar com segurança.
3. **A dependência da nuvem custou um mês de cultivo.** Não repetir.
4. **Documentar salvou o projeto.** O handoff de 08/05 e a skill do SmartGrow são o que
   permitiu continuidade entre sessões. O 2.0 nasce com `docs/` antes de `src/`.
5. **A metodologia de debug do Pedro funciona** — sketch isolado, uma variável por vez,
   resultado no display. Preservar isso como ferramenta de primeira classe no 2.0.
