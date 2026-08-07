# Especificação dos módulos do firmware 2.0

Contrato de cada módulo antes de existir código. Escrito para ser lido pelo Claude Code
no início de cada fase do roadmap.

**Regra de dependência**: `lib/*` não conhece `main.cpp`. Os módulos de decisão
(`Irrigation`, `Lighting`, `Sensors`) **não chamam hardware diretamente** — recebem
leituras e devolvem intenções. É isso que os torna testáveis no ambiente `native`.

```
main.cpp
   │ compõe e conecta
   ▼
┌──────────┬──────────┬────────────┬──────────┬──────────┬─────────┬───────┐
│  Config  │  Clock   │  Sensors   │Irrigation│ Lighting │ Display │ Cloud │
│          │          │            │          │          │Gestures │  Diag │
└──────────┴──────────┴────────────┴──────────┴──────────┴─────────┴───────┘
     ▲            ▲                     │
     └────────────┴─────── lidos por ───┘
```

---

## Config

**Responsabilidade**: única fonte de verdade dos parâmetros. Persistência em NVS,
defaults no código, migração por versão de schema.

```cpp
struct Zona {
  char     nome[16];
  uint8_t  pinoSensorSolo;
  uint8_t  canalBomba;
  uint8_t  pinoFluxo;        // 0xFF = sem sensor
  uint16_t soloAr;           // calibracao ADC 0%
  uint16_t soloAgua;         // calibracao ADC 100%
  float    vazao_mL_s;       // calibrado POR zona
  bool     ativa;
};

struct ParamFase {
  char     nome[16];         // "germinacao" | "vegetacao" | "floracao"
  uint8_t  horaLigar;
  uint8_t  horaDesligar;     // 0 = meia-noite
  bool     exigeBreu;
  uint8_t  limiarUmidade;    // % que dispara o ciclo de rega
  uint16_t volumeCiclo_mL;   // total por zona
  uint8_t  numParcelas;
  uint16_t intervaloParcela_s;
  uint32_t cooldown_s;       // entre ciclos
};

struct ConfigSistema {
  uint16_t  versaoSchema;
  Zona      zonas[MAX_ZONAS];
  uint8_t   numZonas;
  ParamFase fases[3];
  uint8_t   faseAtual;
  uint16_t  galaoTotal_mL;
  uint8_t   limiteAlertaRegas;    // 2
  uint8_t   limiteBloquearRegas;  // 3
  float     timeoutFolga;         // 1.3
  uint32_t  intervaloNuvem_s;     // 900
};
```

**API**: `carregar()`, `salvar()`, `restaurarDefaults()`, `migrar(versaoAntiga)`,
`aplicarConselho(ParamFase, faixasSeguras)`.

**Testes**: round-trip de serialização, migração de versão, rejeição de valores fora de faixa.

---

## Clock

**Responsabilidade**: tempo confiável e persistente.

- NTP com **espera de até 10 s no boot** antes de qualquer decisão de luz (lição B4).
- `epochAgora()`, `sincronizado()`.
- **`horasDesde(epoch)` que sobrevive ao reboot** — corrige a lição E2 da v1
  (`tUltimaRega_ms` usava `millis()` e zerava, deixando a IA cega).
- Persiste em NVS os epochs críticos: última rega por zona, início do ciclo, início da
  contagem diária de regas.

**Falha segura**: relógio não sincronizado → `Irrigation` não rega e registra alerta.

---

## Sensors

**Responsabilidade**: leituras e detecção de falha. **Não decide nada.**

```cpp
struct LeituraZona { uint8_t umidadePct; bool sensorFalho; };
struct LeituraAmbiente {
  float tempAr, umidadeAr, pressaoHpa, vpd, tempTerra;
  bool  bmeOk, ds18Ok;
};
```

- Capacitivos com `map(leitura, soloAr, soloAgua, 0, 100)` **por zona**.
- BME280 no **Wire2 dedicado, GPIO 16/17** (lição D5). DS18B20 no GPIO 4.
- `calcularVPD(t, rh)` — função pura, sem estado.
- **Detecção de sensor falho**: queda > 30% em uma leitura → suspeito;
  3 consecutivas → falho. **Subida nunca dispara** (rega faz o valor subir legitimamente).

**Testável em host**: `calcularVPD`, mapeamento, máquina de detecção de falha.

---

## Irrigation — o coração

**Responsabilidade**: decidir e executar a rega. **Decisão local, sem depender da nuvem.**

```cpp
struct EstadoRega {
  uint8_t  umidadeMedia;
  uint32_t epochUltimoCiclo;
  uint8_t  regasHoje;
  uint16_t galao_mL;
  bool     bloqueadaRemota;
  bool     nivelBaixo;
  bool     relogioOk;
};

struct IntencaoRega {
  bool     regar;
  uint8_t  zona;
  uint16_t volume_mL;      // desta parcela
  uint8_t  parcelasRestantes;
  const char* motivo;      // sempre preenchido, inclusive quando regar=false
};

IntencaoRega decidir(const EstadoRega&, const ParamFase&, const ConfigSistema&);  // PURA
```

### Guardas de `executar()` — todas obrigatórias, na ordem

1. `bombaHabilitada`
2. `nivelBaixo`
3. `executandoRega` — **guarda de reentrância, sagrada**
4. `bloqueadaRemota`
5. Limite diário (`regasHoje` vs `limiteBloquearRegas`, janela de 24 h)
6. `relogioOk`

### Execução

- Parada por **TEMPO**: `volume / vazao_mL_s * timeoutFolga` (lição E1).
  Pulso de fluxo é **só telemetria**.
- **Timeout absoluto** + watchdog: a bomba nunca fica ligada indefinidamente (ADR-08).
- Todos os relés OFF como primeira instrução do `setup()`.
- Ciclo parcelado: N parcelas espaçadas; apenas a **primeira** conta no limite diário.
- `pararBomba(motivo)` desconta o galão e enfileira o registro.

### Fila store-and-forward (NVS)

Porte do R09, generalizado para N zonas. Se o envio falhar, **para e mantém na fila** —
nunca perde uma rega.

**Testes (escrever ANTES do código)**: cada linha da tabela de fases de
`docs/06-agronomia.md` vira um caso; mais todos os casos de bloqueio.

---

## Lighting

**Responsabilidade**: fotoperíodo.

- `horaDesligar == 0` significa meia-noite: `deve = (hora >= horaLigar)`.
  Caso contrário: `deve = (hora >= horaLigar && hora < horaDesligar)`.
- Override manual: OFF segura 3 h, ON segura 1 h, repetir o gesto cancela.
- Espera o NTP no boot antes da primeira decisão (lição B4).

### Trava do breu — invariante de segurança

> Durante o período escuro de uma fase com `exigeBreu`, **nenhum caminho de código** pode
> acender a luz. Nem override manual, nem comando remoto, nem recuperação de falha.

Vazamento de luz no escuro da floração pode reverter a planta ou induzir hermafroditismo.
Esta trava precisa de **teste dedicado que tente todos os caminhos**.

---

## Display

3 páginas, portadas do R09. `DISP_X = 58` (bitmap da folha nos primeiros 58 px).

| Página | Conteúdo |
|---|---|
| 0 Rega | Autonomia, mL/rega, alertas REGANDO / RESERV |
| 1 Luz | ON/OFF, horário, override restante, PPFD alvo da fase |
| 2 Sensores | 6 linhas Y = 0,11,22,33,44,55. **S1 e S2 sempre em linhas separadas** |

Sleep: 2 min → dim (40/255); 30 min → apaga; 1 clique acorda (200/255).
Durante a calibração, refresh de 100 ms. **Nunca trava num estado de alerta.**

---

## Gestures

Máquina de cliques contados na **DESCIDA**.
`TEMPO_CLIQUE_MAX = 400 ms`, `TEMPO_SEGURAR = 2000 ms`.

| Gesto | Ação |
|---|---|
| 1 clique | Muda página / acorda / **para** calibração ou rega manual |
| 1 + segurar | Reset do galão + evento `galao_cheio` |
| 2 + segurar | Toggle da luz com override |
| 3 + segurar | Calibração |
| 4 + segurar | Envio manual à nuvem |
| 5 + segurar | `start_ciclo` |

### Armadilhas obrigatórias (v1, 3 ocorrências)

- **Zerar `contadorCliques = 0; gestoConcluido = false;` IMEDIATAMENTE antes de qualquer
  delay ou feedback** dentro do gesto (lição B1).
- **Não zerar `gestoConcluido` dentro do case** (lição B5).
- **Setar `tRegaManualInicio = millis()` ao ligar a bomba** em modo manual/calibração (lição B2).

### Máquina de calibração

```
0 inativo →(3 cliques+segurar)→ 2 RODANDO (display 100ms, timeout 30s)
          →(1 clique)→ 3 ESCOANDO (3s sem pulso novo) → 4 RESULTADO
          →(1 clique)→ 0
```

**Testável em host**: sequências sintéticas de eventos de botão → gesto esperado.

---

## Cloud

**Responsabilidade**: falar com o backend. **Interface abstrata** — trocar Apps Script por
outro backend depois é trocar a implementação, não o sistema (ADR-05).

### `postGAS()` — configuração exata que resolveu o HTTP -11

```cpp
WiFiClientSecure client;
client.setInsecure();
client.setTimeout(15);            // SEGUNDOS
HTTPClient http;
http.begin(client, URL_GAS);
http.setTimeout(15000);           // MILISSEGUNDOS
http.addHeader("Content-Type", "text/plain");
http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
// POST -> 302 -> getLocation() -> GET manual na URL final
```

> **UM único ponto de criação de contexto TLS em todo o firmware.**
> A duplicação era parte do problema (fragmentação de heap).

### `aplicarConselho()`

Valida cada parâmetro vindo da IA contra faixas seguras codificadas localmente.
Fora da faixa → **ignora e registra evento**. O firmware é o adulto na sala.

---

## Diag

Log estruturado com nível e tag (`[REGA]`, `[NUVEM]`, `[CALB]`, `[LUZ]`),
contadores de falha de envio, tamanho da fila, uptime, heap livre.
Payload de healthcheck consumido pelo painel de saúde do dashboard.
