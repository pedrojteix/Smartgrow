# SmartGrow 2.0

Pasta de trabalho para a reescrita completa do SmartGrow — sistema de cultivo indoor
automatizado de Cannabis com ESP32, IA e dashboard web.

**Autor**: Pedro Teixeira · **Projeto v1**: abril–agosto/2026 · **Esta pasta**: agosto/2026

---

## Por onde começar

1. Abra esta pasta no **Claude Code**. Ele lê o `CLAUDE.md` automaticamente.
2. Leia `docs/00-visao-e-objetivos.md` (5 min) — o porquê do projeto.
3. Leia `docs/09-roadmap.md` — a ordem de execução da reescrita.
4. Comece pela Fase 0 do roadmap.

---

## Estrutura

```
Smartgrow 2.0/
├── CLAUDE.md                    Contrato de trabalho — lido pelo Claude Code
├── README.md                    Este arquivo
│
├── docs/                        Documentação (a fonte da verdade do projeto)
│   ├── 00-visao-e-objetivos.md
│   ├── 01-historico-do-projeto.md
│   ├── 02-hardware.md
│   ├── 03-firmware-v1.md
│   ├── 04-backend-v1.md
│   ├── 05-dashboard-v1.md
│   ├── 06-agronomia.md
│   ├── 07-bugs-e-licoes.md          ← ler antes de codar
│   ├── 08-arquitetura-2.0.md
│   ├── 09-roadmap.md
│   └── 10-credenciais-e-endpoints.md
│
├── legacy/                      v1 congelado — especificação viva, NÃO copiar
│   ├── firmware/                R04 → R10 + sketches de teste
│   ├── backend/                 Apps Script R05 → R07
│   └── dashboard/               index.html em produção + backups
│
├── hardware/
│   ├── kicad/SGRW_Controller/   Esquemático + PCB do controlador
│   └── imagens/                 Bitmaps usados no display OLED
│
├── referencias/
│   ├── biblioteca-cientifica/   8 papers sobre cultivo de Cannabis
│   ├── relatorios/              Calibração, testes hidráulicos, modelo de negócios
│   └── api/                     Documentação de API
│
└── src/                         O código do 2.0 nasce aqui
    ├── firmware/                PlatformIO
    ├── backend/
    └── dashboard/
```

---

## Sistema em produção (v1)

| Componente | Onde | Versão atual |
|---|---|---|
| Firmware ESP32 | Arduino IDE | R09 (18/07/2026) |
| Backend | Google Apps Script | R07 |
| Banco | Google Sheets (DADOS, EVENTOS, CONFIG, REGAS) | — |
| IA | Gemini 2.5 Flash | — |
| Dashboard | GitHub Pages `pedrojteix/Smartgrow` | jun/2026 |

Endpoints e chaves: `docs/10-credenciais-e-endpoints.md`.

---

## Convenções

- Documentação em **português com acento**. Firmware **sem acento nenhum**.
- Versionamento de firmware: `R<NN>`, incremental, com changelog no cabeçalho do arquivo.
- Todo doc em `docs/` é numerado e a ordem importa — é a sequência de leitura.
