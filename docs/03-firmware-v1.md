# 03 — Firmware v1 (baseline R09)

Arquivo: `legacy/firmware/SGRW_ESP32_R09.ino` — ~1650 linhas, arquivo único, 24 seções.
É o que está gravado no ESP32 hoje. Este doc descreve **o comportamento a preservar**, não o código a copiar.

---

## Anatomia do arquivo

| Seção | Conteúdo |
|---|---|
| 1 | Configurações (WiFi, URL do GAS, NTP) |
| 2 | Pinos |
| 3 / 3b | Calibração dos capacitivos, BME280, DS18B20, `calcularVPD()` |
| 4 | Struct `FasePlanta` e array `FASES[]` |
| 5 | Constantes e estado de rega, fluxo, calibração, sensor falho |
| 6 | Galão + persistência NVS |
| 7 / 7b | Ciclo de cultivo e `horasDesdeUltimaRega()` |
| 8 | Luz e override manual |
| 9 | Display, brilho, sleep |
| 10 | Estado dos sensores |
| 11 | Temporização (intervalos do loop) |
| 12 | Máquina de gestos do botão BOOT |
| 13 | Bitmap da folha (PROGMEM) |
| 14 | ISRs de fluxo |
| 15 | `setup()` |
| 16 | `loop()` |
| 17 | WiFi / NTP |
| 18 | Leitura de sensores + detecção de sensor falho |
| 19 | Controle de luz |
| 20 | Bomba (`iniciarRega`, `pararBomba`) |
| 21 | Display (3 páginas) |
| 22 | Gestos (`processarBotao`, `executarGesto`) |
| 23 | Envio principal (`postGAS`, fila de regas, `enviarParaNuvem`) |
| 24 | Envio de evento |

---

## Loop e temporização

```c
INTERVALO_SENSORES = 2000      // 2 s
INTERVALO_NUVEM    = 900000    // 15 min
INTERVALO_DIAS     = 60000     // 1 min (recalcula dias de ciclo)
```

Primeiro envio à nuvem: 30 s após o boot.
Durante a calibração (estados 2 e 3) o display é redesenhado a cada 100 ms.

---

## Comunicação com a nuvem

### `postGAS(payload, respOut)` — helper único (introduzido no R09)

O ESP32 **não segue redirect HTTPS do Google**. O fluxo é manual, em 2 passos:

```
POST text/plain → recebe 302 → extrai Location → GET na URL final → resposta
```

Configuração que resolveu o bug HTTP -11 (ver `docs/07-bugs-e-licoes.md`):

```c
WiFiClientSecure client;
client.setInsecure();
client.setTimeout(15);          // SEGUNDOS, não ms
HTTPClient http;
http.begin(client, URL_GAS);
http.setTimeout(15000);         // ms
http.addHeader("Content-Type", "text/plain");
http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
```

**A centralização em uma única função foi parte da solução** — reduziu fragmentação de heap
e eliminou a duplicação de configuração TLS entre os dois pontos de envio.

### Payload enviado (`tipo: "dados"`)

```json
{
  "tipo": "dados", "manual": false,
  "umidade1": 61, "umidade2": 18, "alertaBoia": 0,
  "volumeBaseML": 200, "volumeTotalML": 400,
  "fasePlanta": "floracao", "diasCiclo": 0, "galaoMl": 6800,
  "desequilibrio": 0, "luzAtiva": 1, "horasUltimaRega": 999,
  "sensor1Falho": false, "sensor2Falho": false,
  "tempAr": 23.4, "umidadeAr": 65.1, "pressaoHpa": 1012.3, "vpd": 1.01,
  "tempTerra": 21.8
}
```

Campos de ar/VPD só são enviados se `bmeOk`; `tempTerra` só se `ds18Ok`.
`diasCiclo` é **ignorado pelo GAS**, que calcula `diasReais` a partir da data cadastrada no site.

### Resposta esperada do GAS

```json
{ "fator_rega": 1.0, "volume_ml": 300, "regar_agora": true,
  "rega_remota_ml": 0, "bomba_bloqueada": -1 }
```

`bomba_bloqueada`: `1` bloqueia, `0` desbloqueia, `-1` mantém.

### Fila de regas (store-and-forward) — R09

Estrutura `RegaPendente` persistida em NVS (`prefs`), máximo `FILA_REGAS_MAX`:

```c
struct RegaPendente { uint8_t hora, minuto; uint16_t vaso1_mL, vaso2_mL, duracaoS; char trigger[N]; };
```

- `enfileirarRega()` grava a rega executada; se a fila estiver cheia, descarta a mais antiga.
- `flushFilaRegas()` envia `tipo:"rega"` uma a uma; **se um envio falhar, para e mantém na fila** —
  não perde rega.

Este é o único subsistema da v1 que já respeita o princípio "a planta não depende da nuvem".

---

## Rega

### Constantes

```c
VAZAO_ML_S              = 8.4      // vazão real das 2 bombas juntas
TIMEOUT_FOLGA           = 1.3      // parada por tempo: vol*2/8.4*1.3
FATOR_CALIBRACAO_FLUXO  = 173.92   // SÓ telemetria/exibição
FATOR_DOIS_VASOS        = 2.0
LIMITE_ALERTA_REGAS     = 2        // dispara evento alerta_rega
LIMITE_BLOQUEAR_REGAS   = 3        // bloqueia rega no dia (janela 24h)
REGA_MANUAL_MAX_MS      = 60000
GALAO_TOTAL_ML          = 7000
LIMIAR_DESEQUILIBRIO    = 20       // %
```

### `iniciarRega(float volumeBase_mL, int numBomba = 1)`

> O argumento default vai **SÓ na forward declaration** (antes do `loop()`),
> NUNCA na definição — erro `-fpermissive`.

Guardas no início (todas obrigatórias):

1. `bombaHabilitada`
2. `nivelBaixo` (boia — hoje sempre `false`)
3. `executandoRega` — **guarda de reentrância, sagrada**
4. `bombaRemotaBloqueada`
5. Limite de regas por dia (`regasHoje` vs `LIMITE_BLOQUEAR_REGAS`, janela via `tInicioContagem`)

Execução: aciona `RELE_BOMBAS` (as duas juntas). Alvo = `volumeBase * 2`.
Para por **volume atingido**, **timeout de tempo** ou **nível baixo**.

### `pararBomba(motivo)`

- Desconta do galão a **soma real dos dois fluxos**.
- Zera `bombaAtiva`, envia evento `bomba_off`, atualiza `tUltimaRega_ms`.

### Ponto fraco conhecido

`tUltimaRega_ms` é volátil (`millis()`), zera no boot → `horasDesdeUltimaRega()` retorna `999`
("nunca regou nesta sessão") → a IA fica cega quanto ao histórico recente.
**O 2.0 deve persistir isso com epoch real.**

---

## Detecção de sensor de solo falho

- Queda **> 30% em uma única leitura** (2 s) → estado SUSPEITO.
- 3 leituras suspeitas consecutivas → FALHO.
- Quando um sensor está falho, o outro assume a leitura dos dois vasos.
- **Subidas rápidas não disparam** (rega faz o valor subir legitimamente).
- Emite evento `sensor_falho`.

Constantes: `LIMIAR_QUEDA = 30`, `LEITURAS_FALHO = 3`.

---

## Luz

```c
const FasePlanta FASES[] = {
  { "Germinacao", "germinacao", 17, 7,  0, false },
  { "Vegetacao",  "vegetacao",  17, 7,  0, false },
  { "Floracao",   "floracao",   12, 8, 20, true  },
};
int faseAtual = 2;   // FLORAÇÃO (estado atual do cultivo)
```

- `horaDesligar == 0` significa **meia-noite**:
  `deveAutomatico = (hora >= horaLigar)`.
  Caso contrário: `deveAutomatico = (hora >= horaLigar && hora < horaDesligar)`.
- **Override manual** (2 cliques + segurar): desligou → segura OFF por 3h;
  ligou → segura ON por 1h; repetir o gesto cancela o override.
  O display mostra o restante (`OFF 142min` / `ON 47min`).
- No boot, o firmware **aguarda o NTP até 10 s** antes de chamar `controlarLuz()` —
  sem isso a luz ficava apagada em horário de luz.

---

## Gestos do botão BOOT

Cliques contados na **DESCIDA** (pressionar), não na soltura.
`TEMPO_CLIQUE_MAX = 400 ms`, `TEMPO_SEGURAR = 2000 ms`.

| Gesto | Ação |
|---|---|
| 1 clique | Muda página / acorda display / **para** calibração ou rega manual |
| 1 + segurar | Reset do galão para 7L + evento `galao_cheio` |
| 2 + segurar | Toggle da luz com override (3h OFF / 1h ON); repetir cancela |
| 3 + segurar | **Calibração** (as 2 bombas juntas) |
| 4 + segurar | Envio manual à nuvem |
| 5 + segurar | `start_ciclo` |

### Máquina de calibração (`estadoCalib`)

```
0 inativo
  └─(3 cliques + segurar)─→ 2 RODANDO
        display a 100ms: tempo em decimais, pulsos e mL dos 2 fluxos ao vivo
        timeout 30s (CALB_TIMEOUT_MS)
  └─(1 clique)─→ 3 ESCOANDO
        bomba off; aguarda a inércia da água (3s sem pulso novo, CALB_PULSO_TIMEOUT)
        mostra "PRONTO"
  └────────────→ 4 RESULTADO CONGELADO
  └─(1 clique)─→ 0
```

Log final no Serial: `[CALB] FINAL | T_ativo | B1=Np(mL) | B2=Np(mL) | FatorK`.

> **Armadilha recorrente**: zerar `contadorCliques` e `gestoConcluido` **imediatamente**
> antes de qualquer `delay`/feedback dentro de um gesto. Ver `docs/07-bugs-e-licoes.md` item 5.

---

## Display OLED (3 páginas)

`DISP_X = 58` — o bitmap da folha ocupa os primeiros 58 px à esquerda.

| Página | Conteúdo |
|---|---|
| 0 — Rega | Autonomia, mL por rega, alertas REGANDO / RESERV / HIDRO |
| 1 — Luz | ON/OFF, horário, override restante, PPFD alvo e distância da fase |
| 2 — Sensores | 6 linhas (Y = 0, 11, 22, 33, 44, 55): S1 / S2 / Um.Ar / T.Ar / Terra / VPD — **S1 e S2 SEMPRE em linhas separadas** |

Sleep: 2 min → dim (brilho 40/255); 30 min → apaga. 1 clique acorda (200/255).
Alertas de reservatório e desequilíbrio aparecem **apenas na página 0**.
O display **nunca trava** num estado de alerta — sempre navega.

---

## Autonomia (mesma fórmula no ESP32 e no site)

```c
consumoPorRega = volumeTotalML_ultimo > 0 ? volumeTotalML_ultimo
               : volumeBaseIA_mL > 0      ? volumeBaseIA_mL * 2
               : 600.0;
diasAutonomia = (int)(galaoAtual_mL / consumoPorRega);
```

---

## Persistência (NVS via `Preferences`)

- `galaoAtual_mL` — sobrevive a reboot e queda de energia.
- `filaN` + `fila` — fila de regas pendentes.

**Não persistido (e deveria ser):** `tUltimaRega_ms`, `regasHoje`, `faseAtual`,
`cicloId`, contadores de falha.

---

## O que o 2.0 herda

**Preservar integralmente:**
- Rega por tempo com `VAZAO_ML_S` + folga.
- Guarda de reentrância e todas as guardas de segurança.
- Redirect manual em 2 passos e o helper único de HTTP.
- Fila store-and-forward.
- Máquina de gestos e a calibração por display (é a ferramenta de trabalho do Pedro).
- Detecção de sensor falho.
- Espera do NTP antes de controlar a luz.

**Refazer:**
- Arquivo único → módulos.
- Duas zonas hardcoded → N zonas configuradas.
- Constantes em `#define` → configuração persistida e editável em runtime.
- Decisão de rega na nuvem → decisão local, nuvem como conselho.
- `millis()` para estado de longo prazo → epoch persistido.
