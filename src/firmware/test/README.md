# test/ — Testes de lógica pura

```bash
pio test -e native      # roda no PC, sem ESP32 conectado
```

Só módulos **sem dependência de hardware** podem ser testados aqui.

```
test/
├── test_irrigation/    decisao de rega  ← o mais importante
├── test_lighting/      agendamento + trava do breu
├── test_sensors/       VPD, mapeamento, deteccao de sensor falho
└── test_gestures/      maquina de cliques
```

---

## A especificação são os documentos, não o código v1

**`test_irrigation`**: cada linha da tabela de fases de `docs/06-agronomia.md` vira um caso.

| Fase | Dias | Volume/vaso | Intervalo mínimo |
|---|---|---|---|
| Muda | 0–14 | 75–150 mL | 48 h |
| Vegetação | 15–28 | 200–300 mL | 48–72 h |
| Floração inicial | 29–49 | 500 mL | 24 h |
| Floração plena | 50–70 | 700–900 mL | 24 h |
| Floração final | 71+ | 500–700 mL | 24–48 h |

Mais um caso para cada bloqueio: boia acionada · muda com média ≥ 65% · veg/flor com
média ≥ 80% · frequência mínima não cumprida · galão < 500 mL (e a exceção de emergência
com média < 30%) · bloqueio remoto · limite diário atingido · relógio não sincronizado.

**`test_lighting`**: 24 horas sintéticas para cada fase; `horaDesligar == 0` (meia-noite);
override de 3 h OFF e 1 h ON, e o cancelamento por repetição.
E o teste que mais importa: **tentar acender a luz por todos os caminhos possíveis durante
o breu da floração e provar que nenhum funciona.**

**`test_sensors`**: `calcularVPD` contra valores conhecidos (T 23 °C / RH 65% → ~1,01 kPa);
mapeamento ADR→% nos extremos e fora da faixa; sequências sintéticas de detecção de falha —
queda > 30% três vezes dispara, **subida nunca dispara**.

**`test_gestures`**: sequências de eventos de botão → gesto esperado.
Incluir regressão explícita para o **clique residual** (lição B1) — o bug que apareceu
três vezes em formas diferentes.

---

## Regra

Nas Fases 3 e 4, **escrever o teste antes do código**.
São as fases onde vive o conhecimento agronômico e as armadilhas de gesto — exatamente
onde uma regressão silenciosa custa uma planta.
