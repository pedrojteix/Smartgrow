# 08 — Arquitetura do SmartGrow 2.0

Status: **proposta**. Este documento é a base de discussão e deve ser revisado pelo Pedro
antes da Fase 1 do roadmap. As decisões marcadas com **[ADR]** são pontos de virada —
mudá-las depois custa caro.

---

## Princípio central

> **A planta não pode depender de WiFi.**

Na v1, um bug de biblioteca HTTP parou a irrigação por quase um mês. No 2.0, a decisão de
regar vive no ESP32. A nuvem é **conselho e memória**, nunca caminho crítico.

Isso reorganiza as responsabilidades:

| | v1 | 2.0 |
|---|---|---|
| Decide quando/quanto regar | Gemini na nuvem | **Firmware, localmente** |
| Papel da IA | Comandar a bomba | Ajustar os **parâmetros** que o firmware usa e diagnosticar |
| Se a nuvem cair | Rega para | Firmware segue com os últimos parâmetros válidos |
| Fonte da verdade do estado | Espalhada (RAM, NVS, Sheets, localStorage) | NVS no ESP32; Sheets é histórico |

---

## [ADR-01] Firmware: PlatformIO, modular, testável em host

**Contexto**: 1650 linhas em um `.ino`, sem testes, com bugs recorrentes de escopo e chaves.

**Decisão**: migrar para **PlatformIO** com estrutura de biblioteca interna e testes unitários
rodando no ambiente `native` (sem hardware).

**Consequências**:
- (+) Testes de lógica pura sem ESP32 conectado; `pio test` no CI.
- (+) Cada módulo compila e é raciocinado isoladamente.
- (+) Versionamento de dependências travado no `platformio.ini` — fim de "quebrou porque o
  core atualizou" (que é exatamente a suspeita do bug HTTP -11).
- (−) Curva de aprendizado vs Arduino IDE. Mitigação: `pio run -t upload` cobre 95% do uso.
- (−) A calibração continua exigindo hardware real. Aceito.

**Alternativas descartadas**:
- *Continuar no Arduino IDE modularizando com `.h`*: não resolve testes nem dependências.
- *ESPHome*: elimina o código customizado, mas a máquina de gestos, a calibração por display
  e a lógica de rega parcelada seriam difíceis ou impossíveis. Descartado.

### Estrutura proposta

```
src/firmware/
├── platformio.ini
├── include/
│   └── config.h              // tipos e defaults, sem segredos
├── src/
│   └── main.cpp              // só setup/loop e wiring dos módulos
├── lib/
│   ├── Config/               // schema + persistência NVS + defaults
│   ├── Sensors/              // solo, DS18B20, BME280, VPD, deteccao de falha
│   ├── Irrigation/           // decisao de rega + execucao + guardas + fila
│   ├── Lighting/             // fotoperiodo, override, trava do breu
│   ├── Display/              // 3 paginas + sleep + bitmap
│   ├── Gestures/             // maquina de estados do BOOT
│   ├── Cloud/                // postGAS unico, payload, parsing, fila
│   ├── Clock/                // NTP + epoch + persistencia de tempo
│   └── Diag/                 // log estruturado, contadores, healthcheck
└── test/
    ├── test_irrigation/      // decisao de rega — o coracao
    ├── test_lighting/        // agendamento e trava do breu
    ├── test_sensors/         // VPD, mapeamento, deteccao de falha
    └── test_gestures/        // maquina de cliques
```

**Regra de dependência**: `lib/*` não conhece `main.cpp`, e a lógica de decisão
(`Irrigation`, `Lighting`, `Sensors`) **não chama nada de hardware diretamente** —
recebe leituras e devolve intenções. É isso que a torna testável em host.

---

## [ADR-02] Decisão de rega local, IA como camada de ajuste

**Decisão**: o firmware contém um **motor de decisão determinístico**. A IA, quando
disponível, ajusta os parâmetros desse motor — não o substitui.

```
                     ┌─────────────────────────┐
   sensores ────────▶│  Motor de decisão local │────▶ intenção de rega
                     │  (determinístico,       │
   parâmetros ──────▶│   testável, offline)    │
   (config NVS)      └─────────────────────────┘
        ▲
        │ atualiza (quando há rede, opcional)
        │
   ┌────┴──────────────────────────┐
   │  Nuvem: Gemini + histórico    │
   │  Devolve: limiares, volumes,  │
   │  frequência, diagnóstico      │
   └───────────────────────────────┘
```

**Motor local** (herda o R10 experimental, generalizado):
- Dispara quando a umidade média da zona ≤ `limiarUmidade` da fase.
- Executa o volume da fase **em parcelas** (N parcelas espaçadas), para o substrato absorver.
- Respeita `cooldown` entre ciclos, limite diário, nível do reservatório e bloqueio remoto.
- Parada por **tempo** (`vazao_mL_s` × folga), com pulso como telemetria.

**Papel da IA**: recebe o histórico e devolve um objeto de parâmetros com validade
(`valido_ate`). O firmware aplica **apenas se** os valores caírem dentro de faixas seguras
codificadas localmente. Fora da faixa → ignora e registra o evento.

> Isso inverte a relação de confiança: hoje o firmware obedece a IA cegamente.
> No 2.0, o firmware é o adulto na sala.

---

## [ADR-03] Modelo de N zonas configuráveis

**Contexto**: "dois vasos" está hardcoded em firmware, backend e dashboard. Quando a planta 1
morreu, foi preciso desativar features com valores absurdos (`LIMIAR_DESEQUILIBRIO = 200`).

**Decisão**: modelar **zonas**, não vasos.

```cpp
struct Zona {
  char     nome[16];
  uint8_t  pinoSensorSolo;
  uint8_t  canalBomba;
  uint8_t  pinoFluxo;      // 0xFF = sem sensor de fluxo
  float    vazao_mL_s;     // calibrado POR zona
  bool     ativa;
};
```

Configuração persistida em NVS. Firmware, payload, planilha e dashboard iteram sobre zonas.
Desativar a planta 1 vira `zona[0].ativa = false`. Adicionar uma terceira zona é configuração.

**Consequência no backend**: as abas DADOS e REGAS precisam de esquema por zona
(linhas em vez de colunas fixas, ou colunas geradas). Ver ADR-05.

---

## [ADR-04] Configuração persistida, editável em runtime

**Decisão**: um `struct ConfigSistema` versionado, persistido em NVS, com defaults no código
e migração por número de versão.

Contém: credenciais WiFi, URL do backend, zonas, fases (horários e limiares), calibrações
dos sensores, parâmetros de rega, intervalos.

Editável por: gesto no display (subconjunto essencial), portal WiFi no primeiro boot,
e comando remoto do dashboard (subconjunto seguro).

**Nada de recompilar para trocar de fase ou de rede.**

---

## [ADR-05] Backend: manter Apps Script, com contrato explícito

**Decisão**: **manter Google Apps Script + Sheets** nesta fase.

**Justificativa**: já funciona, é grátis, o Sheets é uma ferramenta de análise excelente para
esse volume, e trocar o backend agora dobraria o escopo da reescrita. Com a decisão de rega
migrada para o firmware (ADR-02), o backend deixa de ser crítico — então sua fragilidade
deixa de ser um risco de cultivo.

**Mas com**:
- `API_CONTRACT.md` versionado, com schema de cada rota e campo `v` no payload.
- Chave do Gemini em **Script Properties**, fora do fonte.
- Prompt em constante própria, com `PROMPT_VERSION` gravado junto de cada decisão
  (para poder correlacionar mudanças de prompt com mudanças de comportamento).
- Escrita por **nome de coluna** resolvido do cabeçalho, não por índice fixo.
- Funções de teste cobrindo cada rota.

**Porta de saída deliberada**: toda comunicação passa por um módulo `Cloud` no firmware com
interface abstrata. Trocar Apps Script por MQTT/Supabase depois é reescrever um módulo,
não o sistema. **Reavaliar quando**: o histórico passar de ~50k linhas, ou surgir necessidade
de múltiplos grows.

---

## [ADR-06] Dashboard: mesma stack, código organizado

**Decisão**: manter HTML/CSS/JS puro + Chart.js, sem framework e sem build pesado.

**Justificativa**: GitHub Pages, zero infraestrutura, deploy é `git push`. O problema do
dashboard v1 é organização, não tecnologia.

**Mudanças**:
- Separar em módulos ES (`app.js`, `charts.js`, `api.js`, `zones.js`, `state.js`).
- Painel de **saúde do sistema**: online/offline, última rega, fila pendente no ESP32,
  falhas de envio, estado de cada sensor.
- Renderização dirigida pela configuração de zonas.
- Constantes compartilhadas (fases, fórmula de autonomia) geradas de uma fonte única.

---

## [ADR-07] Segredos fora do repositório

**Decisão**:
- Firmware: `include/secrets.h` no `.gitignore`, com `secrets.example.h` versionado.
- Backend: `PropertiesService.getScriptProperties()`.
- Documentação: `docs/10-credenciais-e-endpoints.md` **não vai para o GitHub público**.

> As chaves atuais (Gemini, WiFi) estão em texto puro em vários arquivos versionados.
> **Rotacionar a chave do Gemini** antes de tornar qualquer coisa pública.

---

## [ADR-08] Segurança de operação

Invariantes que o código deve garantir por construção, não por convenção:

1. **A bomba nunca fica ligada indefinidamente.** Timeout absoluto em hardware-watchdog +
   verificação em cada iteração do loop. Se o loop travar, o watchdog reseta e o `setup()`
   desliga todos os relés antes de qualquer outra coisa.
2. **Estado seguro no boot**: todos os relés OFF como primeira instrução do `setup()`,
   antes de qualquer inicialização.
3. **Trava do breu**: durante o período escuro da floração, **nenhum caminho de código**
   pode acender a luz — nem override manual, nem comando remoto.
4. **Guarda de reentrância** da rega, mantida da v1.
5. **Faixas seguras** para todo parâmetro vindo da nuvem; fora da faixa, ignora e registra.
6. **Falha segura**: sensor inválido, relógio não sincronizado ou config corrompida →
   não rega, alerta.

---

## Fluxo de dados do 2.0

```
        ┌──────────────────────── ESP32 ────────────────────────┐
        │                                                        │
 sensores ─▶ Sensors ─▶ estado ─▶ Irrigation.decidir() ─▶ Irrigation.executar() ─▶ bomba
        │                  │              ▲                          │
        │                  │              │ parâmetros               │ registra
        │                  │         Config (NVS)                    ▼
        │                  │              ▲                     fila NVS
        │                  ▼              │                          │
        │              Display        Cloud.aplicarConselho()        │
        │              Gestures            ▲                         │
        │                                  │                         │
        └──────────────────────────────────┼─────────────────────────┘
                                           │ (quando há rede)
                              ┌────────────┴─────────────┐
                              │  Apps Script + Gemini    │
                              │  + Google Sheets         │
                              └────────────┬─────────────┘
                                           │ gviz
                                    ┌──────┴───────┐
                                    │  Dashboard   │
                                    └──────────────┘
```

---

## O que muda na prática

| Aspecto | v1 | 2.0 |
|---|---|---|
| Arquivos de firmware | 1 (`.ino`, 1650 linhas) | ~10 módulos, nenhum > 300 linhas |
| Testes | 0 | Decisão de rega, luz, VPD, gestos |
| Rega sem internet | Não funciona | Funciona indefinidamente |
| Trocar de fase | Recompilar e regravar | Configuração |
| Adicionar zona | Reescrever meio firmware | Configuração |
| Trocar WiFi | Recompilar e regravar | Portal / display |
| Trocar backend | Reescrever os envios | Trocar um módulo |
| Segredos | No fonte, versionados | Fora do repositório |
| Diagnóstico | Serial Monitor + foto do display | Log estruturado + painel de saúde |

---

## Riscos e mitigações

| Risco | Mitigação |
|---|---|
| Reescrita nunca termina e a v1 apodrece | Roadmap em fases entregáveis; a v1 continua rodando até a v2 passar em bancada |
| Regressão silenciosa em regra agronômica | Testes derivados da tabela de `docs/06-agronomia.md`, escritos **antes** do código |
| Perder conhecimento embutido no código v1 | `legacy/` congelado + estes docs; toda dúvida se resolve lendo o R09 |
| PlatformIO atrapalhar o fluxo do Pedro | Fase 0 valida o toolchain com um blink antes de mover qualquer lógica |
| Trocar a planta viva por uma reescrita instável | Bancada separada (segundo ESP32 + bomba em copo d'água) antes de tocar no grow |
