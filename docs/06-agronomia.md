# 06 — Agronomia

Parâmetros de cultivo consolidados. Fontes: relatório de calibração
`referencias/relatorios/SGRW_DC_CAL_01.docx`, teste de iluminação
`REL_TESTE_HID&LUZ_24042026`, os 8 papers em `referencias/biblioteca-cientifica/`
e a operação real do sistema.

---

## Plantas do ciclo atual

| | Planta 1 | Planta 2 |
|---|---|---|
| Germinação | 11/04/2026 | 10/04/2026 |
| Primeiro vaso | 18/04/2026 | 19/04/2026 |
| Entrada no SmartGrow | 24/04/2026 | 23/04/2026 |
| Altura na entrada | 12 cm | 11 cm |
| Situação | **Macho — eliminada (ago/2026)** | Ativa, em floração |

- Fabric pots **11 L**, substrato **1:1:1** (perlita / húmus / fibra de coco).
- Anéis de irrigação com furos apontando para **FORA**.
- Sensores capacitivos posicionados **entre o anel e a borda do vaso**, do lado oposto aos furos.
  Consequência: medem **molhamento periférico** — a água seca de fora para dentro.
- **LST** (low stress training, amarração lateral) feito aos ~45 dias.
- As datas de entrada são cadastradas no site; o GAS calcula `diasReais` a partir delas.

---

## PPFD — Quantum Board LM281b (65 W reais)

Tabela medida com app Photone, em 24/04/2026.

| Distância | PPFD (µmol/m²/s) | Fase indicada |
|---|---|---|
| 10 cm | 900 | Floração intensa |
| 15 cm | 720 | Floração |
| 20 cm | ~650 | Floração / vegetação avançada |
| 25 cm | 575 | Vegetação |
| 30 cm | 490 | Vegetação |
| > 31 cm | 100–300 | Germinação (mudas a 45–50 cm) |

**Progressão adotada**: pós-LST 500–600 µmol (25–30 cm) por 5–7 dias →
subir para 700–800 antes de induzir floração → 800–900 durante a floração.

---

## VPD, umidade do solo e volume por fase

| Fase | VPD alvo | Solo mínimo | Alvo pós-rega | Volume por vaso |
|---|---|---|---|---|
| Germinação / Muda | 0,4–0,8 kPa | 40% | 65–70% | 75–150 mL |
| Vegetação | 0,8–1,2 kPa | 35% | 75% | 200–300 mL |
| Floração | 1,2–1,6 kPa | 40% | 75% | 500–900 mL |

- **Capacidade de campo** do substrato: ~75%.
- **Saturação de risco**: acima de 80% (asfixia radicular).

### Cálculo do VPD

```
SVP = 0.61078 * exp( (17.27 * T) / (T + 237.3) )     // kPa, T em °C
VPD = SVP * (1 - RH/100)                              // kPa
```

Implementado em `calcularVPD(tempC, rhPct)` no firmware. Depende do BME280 (atualmente morto).

---

## Frequência de rega por fase (tabela usada no prompt da IA)

| Fase | Dias | Volume/vaso | Intervalo mínimo |
|---|---|---|---|
| Muda | 0–14 | 75–150 mL | 48 h |
| Vegetação | 15–28 | 200–300 mL | 48–72 h |
| Floração inicial | 29–49 | 500 mL | 24 h |
| Floração plena | 50–70 | 700–900 mL | 24 h |
| Floração final | 71+ | 500–700 mL | 24–48 h |

---

## Temperatura do solo

| Fase | Faixa ideal |
|---|---|
| Germinação | 20–25 °C |
| Vegetação | 18–24 °C |
| Floração | 18–22 °C |
| Fim da floração | 16–20 °C |

Medida atual: **21–23 °C** (dentro da faixa).

---

## Fotoperíodo

| Fase | Horas de luz | Janela | Breu obrigatório |
|---|---|---|---|
| Germinação | 17 h | 07:00 → 00:00 | não |
| Vegetação | 17 h | 07:00 → 00:00 | não |
| **Floração** | **12 h** | **08:00 → 20:00** | **sim** |

Breu obrigatório na floração: qualquer vazamento de luz no período escuro pode reverter a
planta ou induzir hermafroditismo. **Nenhuma lógica de override deve ser capaz de acender a
luz durante o período escuro da floração** — restrição que o 2.0 deve tornar explícita no código.

---

## Ambiente típico medido

T.Ar 23–24 °C · Umidade do ar ~65% · VPD ~1,01 kPa — ideal para vegetação.

---

## Achados hidráulicos do relatório de calibração (24/04/2026)

1. O sensor de fluxo YF-S401 estava **instalado ao contrário** nos testes iniciais,
   gerando leituras irreais.
2. Fator K medido: **~46,7 pulsos/mL** naquele momento (fábrica: 5,88).
   Após instalar as válvulas de retenção, recalibrado para **173,92 pulsos/mL**
   (média de 167,11 e 180,73).
3. **Efeito sifão**: quando o fluxo vai para um lado só, a vazão total cai ~18%.
   O lado bloqueado não é só desviado — ele impõe resistência ao sistema todo.
4. Desequilíbrio entre vasos variou de 1,1% a 21,9% entre rodadas.
   **Não é assimetria física fixa** (comprimento de mangueira), é fenômeno dinâmico
   de pressão → a solução é hidráulica (válvulas de retenção), não de software.

> Conclusão que vale para o 2.0: **nunca compensar problema hidráulico com correção
> em software.** Corrigir o encanamento.

---

## Biblioteca científica

Em `referencias/biblioteca-cientifica/`:

| Arquivo | Tema |
|---|---|
| `Cannabis sativa L. Crop Management and Abiotic Factors...` | Manejo geral e fatores abióticos |
| `Effect of Light Intensity and Two Different Nutrient Solutions` | Intensidade luminosa × solução nutritiva |
| `Evaluation of substrates in the germination of Cannabis sativa L....V3` | Substratos na germinação até V3 |
| `High light intensity improves yield of specialized metabolites...` | Alta intensidade × metabólitos |
| `Integrating Hydraulic Properties into Irrigation Management` | Propriedades hidráulicas do substrato |
| `Optimizing simplified growing media to enhance cannabis cultivation` | Otimização de substrato |
| `Photoperiodic_Response_of_In_Vitro_Cannabis_sativa` | Resposta ao fotoperíodo |
| `Subsurface drip irrigation reduces weed` | Irrigação por gotejamento subsuperficial |

Estes papers são a base de qualquer mudança nos parâmetros agronômicos — e boa matéria-prima
para o prompt da IA no 2.0.
