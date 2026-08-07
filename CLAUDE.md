# CLAUDE.md — SmartGrow 2.0

> Este arquivo é lido automaticamente pelo Claude Code ao abrir esta pasta.
> É o contrato de trabalho do projeto. Leia inteiro antes de escrever qualquer linha de código.

---

## 1. O que é o SmartGrow

Sistema de cultivo indoor automatizado de Cannabis, construído por **Pedro Teixeira** (pedrojteix)
entre abril e agosto de 2026. Combina hardware próprio (ESP32 + sensores + bombas + luz),
um backend com IA (Gemini) e um dashboard web.

O sistema mede o solo e o ambiente, decide **quanto e quando regar** (hoje via IA na nuvem),
executa a rega, controla o fotoperíodo da luminária e registra tudo para análise.

**Estado**: v1 em produção, funcionando, rodando um cultivo real. O objetivo desta pasta é a
**reescrita completa (v2.0)** com arquitetura projetada desde o início, não mais orgânica.

---

## 2. Regra número zero

> **A v1 é a especificação, não o código.**

O código legado em `legacy/` existe para você entender **o que o sistema faz e por quê** —
inclusive as decisões contra-intuitivas, que quase sempre têm uma cicatriz de hardware por trás.
Ele **não** é para ser copiado e colado no `src/`.

Antes de implementar qualquer módulo do 2.0:

1. Leia o doc de referência correspondente em `docs/`.
2. Leia `docs/07-bugs-e-licoes.md` — a lista do que **não** pode ser reintroduzido.
3. Só então escreva o código novo.

---

## 3. Como o Pedro trabalha (metodologia obrigatória)

| Situação | O que fazer |
|---|---|
| **Correção pontual** | Informar "apague as linhas X–Y, cole isto no lugar", com números de linha EXATOS do arquivo atual dele. Se não tiver o arquivo atual em mãos, **pedir antes** de dar linhas. Nunca dar linhas de uma versão que não é a que ele está usando. |
| **Arquivo grande ou novo** | Entregar o arquivo completo, pronto para colar. |
| **Debug** | Seguir a cadeia lógica ponto a ponto, isolando cada etapa com teste antes de concluir. **Nunca chutar causa sem evidência.** Sketch de teste isolado é a ferramenta padrão. |
| **Antes de entregar firmware** | Validar balanceamento de chaves/parênteses e remover **todo caractere não-ASCII** (acento quebra a compilação silenciosamente). |
| **Idioma** | Português. Comentários de código em português sem acento. |

Pedro trabalha sozinho, com hardware real ligado, com plantas vivas que morrem se o
código estiver errado. **Prefira sempre o caminho conservador e verificável.**

---

## 4. Regras de ouro (violá-las já custou hardware ou plantas)

1. **Bomba NUNCA liga sem diodo flyback 1N4007** em paralelo. Já queimou 2 canais de relé mecânico.
2. **SSR AC (triac) não chaveia DC.** Bomba = relé mecânico ou SSR DC. Luz 220V = SSR AC.
3. **BME280 só no 3.3V do próprio ESP32.** A reguladora de protoboard entrega 3.6–3.8V e já matou 2 sensores.
4. **BME280 em barramento I2C dedicado (Wire2, GPIO 16/17).** Junto do display corrompe a imagem.
5. **Sensor de fluxo não é confiável.** Critério de parada da rega é **TEMPO** (`VAZAO_ML_S`), pulso é só telemetria.
6. **Guarda de reentrância na rega é sagrada** (`executandoRega`). Nunca remover.
7. **GPIO14 não serve para interrupção** de fluxo (SPI flash). Usar GPIO13.
8. **Todo update do Apps Script exige novo Deploy** para valer.
9. **Sem acento em arquivo de firmware.**

---

## 5. Arquitetura alvo do 2.0

Ver `docs/08-arquitetura-2.0.md` para o desenho completo e as justificativas.

Resumo em uma frase: **firmware modular em PlatformIO com decisão de rega local e autônoma,
nuvem como camada de conselho e telemetria — não como dependência crítica.**

```
src/
├── firmware/     PlatformIO. Módulos independentes e testáveis em host.
├── backend/      Apps Script (v2) — rotas enxutas, prompt versionado.
└── dashboard/    HTML/JS — mesma stack, código organizado.
```

Princípio central: **a planta não pode depender de WiFi.** Na v1, um bug de HTTP travou a rega
automática por quase um mês. No 2.0, a nuvem cai e a planta continua sendo regada.

---

## 6. Mapa da pasta — leia o arquivo certo antes de mexer

| Vai mexer em... | Leia primeiro |
|---|---|
| Qualquer coisa (contexto geral) | `docs/00-visao-e-objetivos.md` |
| Entender como chegamos aqui | `docs/01-historico-do-projeto.md` |
| Pinagem, sensores, relés, alimentação | `docs/02-hardware.md` |
| Lógica do firmware (rega, luz, display, gestos) | `docs/03-firmware-v1.md` |
| Apps Script, prompt da IA, planilha | `docs/04-backend-v1.md` |
| Dashboard, gráficos, comandos remotos | `docs/05-dashboard-v1.md` |
| Parâmetros de cultivo (PPFD, VPD, volumes) | `docs/06-agronomia.md` |
| **Antes de escrever qualquer código** | `docs/07-bugs-e-licoes.md` |
| Desenho do sistema novo | `docs/08-arquitetura-2.0.md` |
| O que fazer, em que ordem | `docs/09-roadmap.md` |
| URLs, IDs, chaves | `docs/10-credenciais-e-endpoints.md` |

---

## 7. Baseline congelado

- **Firmware**: `legacy/firmware/SGRW_ESP32_R09.ino` — é o que está gravado no ESP32 hoje.
- **Backend**: `legacy/backend/SGRW_APPSCR_R07.gs` — é o que está publicado no Apps Script.
- **Dashboard**: `legacy/dashboard/index.html` — é o que está no GitHub Pages.

`legacy/firmware/SGRW_ESP32_R10_experimental.ino` é um **experimento não gravado** (06/08/2026):
planta 1 eliminada, uma bomba só, rega autônoma local parcelada. A ideia da rega autônoma
local dele foi promovida a requisito do 2.0 — o código, não.

---

## 8. Estado físico atual do cultivo (agosto/2026)

- Fase: **floração** (`faseAtual = 2`, luz 12h: 8h→20h, breu obrigatório).
- **Planta 1 era macho e foi eliminada.** Restou a planta 2.
- Os dois sensores capacitivos passaram a ficar no **mesmo vaso** — a lógica de
  desequilíbrio hidráulico da v1 perdeu o sentido nessa configuração.
- BME280 morto (dados de ar/VPD indisponíveis até trocar).
- CH3/CH4 do relé mecânico queimados; SSR novo instalado é **ativo HIGH** (invertido vs R08).

Esse contexto muda a modelagem do 2.0: o sistema precisa suportar **N vasos configuráveis**,
não dois vasos hardcoded.
