# lib/ — Módulos do firmware 2.0

Um diretório por módulo. Contrato de cada um em `../MODULOS.md`.

```
lib/
├── Config/       schema + persistencia NVS + defaults + migracao
├── Clock/        NTP + epoch persistido + horasDesde()
├── Sensors/      solo, DS18B20, BME280, VPD, deteccao de falha
├── Irrigation/   decisao (pura) + execucao + guardas + fila NVS
├── Lighting/     fotoperiodo, override, trava do breu
├── Display/      3 paginas + sleep + bitmap
├── Gestures/     maquina de estados do BOOT + calibracao
├── Cloud/        postGAS unico, payload, aplicarConselho
└── Diag/         log estruturado, contadores, healthcheck
```

## Regras

1. **Nenhum módulo conhece `main.cpp`.**
2. **Módulos de decisão não chamam hardware.** `Irrigation`, `Lighting` e a parte de
   cálculo de `Sensors` recebem leituras e devolvem intenções — é isso que permite
   `pio test -e native`.
3. **Nenhum arquivo passa de ~300 linhas.** Se passar, falta uma separação.
4. Cada módulo tem `Nome.h` (interface) e `Nome.cpp` (implementação).
5. Sem caractere acentuado em nenhum arquivo `.h`/`.cpp`.

## Ordem de construção

Fase 1 `Config`, `Clock` → Fase 2 `Sensors` → Fase 3 `Irrigation` →
Fase 4 `Lighting`, `Display`, `Gestures` → Fase 5 `Cloud`, `Diag`.

Ver `docs/09-roadmap.md`.
