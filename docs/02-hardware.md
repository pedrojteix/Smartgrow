# 02 — Hardware

Referência: firmware `legacy/firmware/SGRW_ESP32_R09.ino` (seções 2 e 3) e
esquemático em `hardware/kicad/SGRW_Controller/`.

---

## Pinagem do ESP32 (30 pinos, ESP32-D0WD-V3)

| GPIO | Função | Observação crítica |
|---|---|---|
| 21 | `I2C_SDA` (Wire) | Display OLED SSD1306, endereço 0x3C, 400 kHz |
| 22 | `I2C_SCL` (Wire) | idem |
| 16 | `I2C2_SDA` (Wire2) | **BME280 0x76 — barramento DEDICADO**, 100 kHz, `bme.begin(0x76, &Wire2)` |
| 17 | `I2C2_SCL` (Wire2) | Nunca juntar com o display: corrompe a imagem |
| 32 | `PINO_SOLO1` | Capacitivo analógico, alimentado no 3.3V do ESP32 |
| 33 | `PINO_SOLO2` | idem |
| 4 | `PINO_DS18B20` | OneWire, temperatura da terra. Resistor 4,7 kΩ entre VCC e DATA |
| 25 | `PINO_FLUXO1` | YF-S401 bomba 1, interrupção `FALLING` |
| 13 | `PINO_FLUXO2` | YF-S401 bomba 2. **GPIO14 NÃO funciona** (SPI flash) |
| 26 | `PINO_BOIA` | **DESATIVADA no firmware** (`nivelBaixo = false` fixo) |
| 0 | `PINO_BOOT` | Botão de gestos |
| 18 | `RELE_LUZ` (CH1) | Luz 220V via SSR AC |
| 23 | `RELE_BOMBAS` (CH2) | **As DUAS bombas juntas** (CH3/CH4 queimados) |
| 19 | ex-IN3 (CH3) | Canal do relé mecânico queimado. **No R10 experimental foi reaproveitado com o SSR novo para a bomba 2** |
| 27 | ex-IN4 (CH4) | Canal queimado |

### Lógica dos relés — atenção, mudou

```c
// R09 em diante (SSR novo):
#define RELE_ON   HIGH
#define RELE_OFF  LOW

// R08 e anteriores (relé mecânico optoacoplado):
// RELE_ON = LOW, RELE_OFF = HIGH
```

Ao ler firmware antigo ou documentação anterior a julho/2026, a lógica está **invertida**.

---

## Relés

### Relé mecânico BLUTU 4 canais 5V optoacoplador
- **Somente CH1 e CH2 funcionam.** CH3 e CH4 com contato soldado.
- Sintoma do contato soldado: o LED do canal acende normalmente, mas o contato não abre/fecha.
  Um peteleco no relé destrava temporariamente — confirmação de contato soldado, não de driver.
- **Causa**: motor DC sem diodo flyback. O pico reverso na desenergização solda os contatos.
- **Pendência crítica**: instalar diodo **1N4007** em paralelo com cada bomba —
  catodo (faixa) no positivo, anodo no negativo.

### SSR 4 canais 5V nível baixo (240VAC 2A)
- **Só chaveia AC** (é triac, precisa do zero-crossing). Serve para a luz 220V.
- **NUNCA usar para as bombas DC.** Já se comprou um SSR achando que serviria.

---

## Bombas

- 2× submersível 3–6V DC, bobina ~7,4 Ω, alimentadas em 6V.
- Ligadas juntas no CH2 desde o R08.
- **Vazão real das duas juntas: 8,4 mL/s** (desvio 7,7% entre rodadas — consistente).
- Uma bomba sozinha: ~4,2 mL/s (valor usado no R10 experimental; **recalibrar** antes de confiar).
- Continuidade elétrica entre os polos da bomba é normal (é a bobina do motor), não é curto.

### Hidráulica

```
Galão 7L (com furo de ventilação no topo)
  → Bomba submersível
    → Sensor de fluxo YF-S401
      → Conector T
        → Válvula de retenção (saída 1) → Anel de gotejamento Vaso 1
        → Válvula de retenção (saída 2) → Anel de gotejamento Vaso 2
```

- Sem as válvulas de retenção, o **efeito sifão** direciona o fluxo para um lado só e a
  vazão total cai ~18% (medido). As válvulas forçam a pressurização simultânea dos dois caminhos.
- O furo de ventilação no galão equaliza pressão — sem ele o bombeamento gera vácuo.
- Anéis de irrigação com os **furos apontando para FORA**.
- **Sensores de solo posicionados entre o anel e a borda do vaso**, do lado oposto aos furos.
  Isso significa que eles medem **molhamento periférico** — a água seca de fora para dentro.
  A IA precisa saber disso (está no prompt).

---

## Alimentação

```
Fonte 9V 2A
  → Reguladora de protoboard
      Lado A: "3,3V"  ⚠️ mede 3,6–3,8V na prática
      Lado B: 5V / 6V  → relés e bombas
```

> **⚠️ A saída "3,3V" da reguladora matou dois BME280** (limite absoluto 3,6V).
> Todo sensor I2C sensível deve ser alimentado **direto no pino 3.3V do ESP32**.

- Os capacitivos foram calibrados com a referência do 3.3V do ESP32:
  `SOLO_AR = 3380` (0%), `SOLO_AGUA = 1250` (100%).
  Fórmula: `constrain(map(leitura, SOLO_AR, SOLO_AGUA, 0, 100), 0, 100)`.
  **Trocar a alimentação obriga a recalibrar.**
- Quantum Board LM281b (65W reais, "600W equivalente"): 220V direto na tomada, chaveada pelo CH1.

---

## Status dos sensores

| Sensor | Status | Nota |
|---|---|---|
| OLED SSD1306 128×64 | OK | Wire, GPIO 21/22 |
| **BME280** | **MORTO** | ACK no ping I2C mas todos os registros retornam 0xFF (chip ID 0xFF). Comprar novo — **BME**280, não BMP280 (o BMP não tem umidade). 4 pinos. Alimentar SÓ no 3.3V do ESP32. |
| DS18B20 | OK | GPIO 4, resistor 4,7 kΩ |
| Capacitivo 1 e 2 | OK | GPIO 32/33. Atualmente **ambos no mesmo vaso** (planta 2) |
| Fluxo 1 e 2 (YF-S401) | OK mas imprecisos | Fator K não-linear: 148–251 pulsos/mL no S1; 59–122 no S2, crescendo com o tempo de ativação. **Telemetria apenas** |
| Boia de nível | Desativada | Lógica HIGH/LOW nunca confirmada. `nivelBaixo = false` forçado no firmware |

---

## Receitas de diagnóstico

### Sensor I2C suspeito
Sketch de scan nos dois barramentos + leitura do registro de chip ID (`0xD0`):

| Valor lido | Significado |
|---|---|
| 0x60 | BME280 vivo |
| 0x58 | BMP280 (sem sensor de umidade!) |
| 0xFF | Chip morto |

Testar também a 10 kHz para descartar problema de timing.
**ACK no ping + 0xFF em todos os registros = controlador I2C vivo, core do chip morto.**

### Upload do firmware falhando
| Erro | Solução |
|---|---|
| `Wrong boot mode (0x17)` | Segurar BOOT, clicar Upload, soltar quando aparecer "Connecting..." |
| `The chip stopped responding` no meio | Cabo USB ruim, porta ruim, ou fonte externa conectada durante o upload |

---

## Pendências de hardware

| # | Item | Prioridade |
|---|---|---|
| 1 | Instalar diodo 1N4007 em cada bomba | **Crítica** — já custou 2 canais de relé |
| 2 | Comprar e instalar BME280 novo (no 3.3V do ESP32) | Alta — sem ele não há VPD |
| 3 | Fonte de alimentação decente no lugar da reguladora de protoboard | Alta |
| 4 | Definir e testar a lógica da boia, ou remover o sensor do projeto | Média |
| 5 | Migrar da protoboard para a PCB do `hardware/kicad/SGRW_Controller` | Média |
