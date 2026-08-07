# src/ — O código do SmartGrow 2.0

Esqueleto. O código nasce aqui, seguindo `docs/09-roadmap.md`.

```
src/
├── firmware/
│   ├── platformio.ini          ← pronto (Fase 0)
│   ├── MODULOS.md              ← contrato de cada módulo, ler antes de codar
│   ├── include/
│   │   ├── secrets.example.h   ← copiar para secrets.h (fora do Git)
│   │   └── config.h
│   ├── src/main.cpp
│   ├── lib/                    ← Config, Clock, Sensors, Irrigation,
│   │                             Lighting, Display, Gestures, Cloud, Diag
│   └── test/                   ← testes de host (pio test -e native)
│
├── backend/
│   └── API_CONTRACT.md         ← contrato das rotas, escrever antes do código
│
└── dashboard/
```

## Comandos

```bash
cd "src/firmware"
pio run                  # compila
pio run -t upload        # grava no ESP32
pio device monitor       # serial a 115200
pio test -e native       # testes de lógica pura, sem hardware
```

## Antes da primeira linha de código

1. `docs/07-bugs-e-licoes.md` — o que não pode ser reintroduzido.
2. `src/firmware/MODULOS.md` — o contrato do módulo que você vai escrever.
3. `docs/09-roadmap.md` — a fase atual e seu critério de saída.

## Regras

- **Nenhum módulo de decisão chama hardware.** Recebe leituras, devolve intenções.
- **Nenhum arquivo passa de ~300 linhas.** Se passar, falta uma separação.
- **Testes antes do código** nas fases 3 e 4 (rega e luz) — a tabela de
  `docs/06-agronomia.md` é a especificação.
- **Firmware sem caractere acentuado.** Documentação com acento, código sem.
- **Comparar com o R09** em `legacy/` sempre que houver dúvida de comportamento.
