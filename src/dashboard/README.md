# Dashboard 2.0

Fase 7 do roadmap — **só depois** do firmware 2.0 estar em produção e observado.

## Stack (mantida)

HTML/CSS/JS puro + Chart.js via CDN, publicado no GitHub Pages.
Sem framework, sem build. Deploy é `git push`.

O problema do dashboard v1 é organização de código, não tecnologia — ver ADR-06.

## Estrutura pretendida

```
dashboard/
├── index.html
├── css/style.css
└── js/
    ├── app.js         orquestracao e init
    ├── api.js         gviz (leitura) + POST ao GAS (escrita)
    ├── state.js       estado global e cache
    ├── zones.js       renderizacao dirigida pela config de zonas
    ├── charts.js      CHART_CONFIGS e janelas
    ├── health.js      painel de saude do sistema  ← NOVO
    └── shared.js      constantes compartilhadas com o firmware
```

## Novidade principal — painel de saúde

O que a v1 não mostra e deveria:

- ESP32 online/offline (já existe) **e há quanto tempo**
- Última rega efetiva por zona, com volume
- **Fila de regas pendente** no ESP32 (indica falha de envio)
- Contador de falhas de envio nas últimas 24 h
- Estado de cada sensor (ok / suspeito / falho / ausente)
- Última vez que um conselho da IA foi aplicado — e se algum foi **rejeitado** por
  estar fora da faixa segura
- Relógio sincronizado ou não

Se o dashboard tivesse isso em maio/2026, o bug HTTP -11 teria sido visto no primeiro dia
em vez de depois de um mês.

## Herdar da v1 sem mudança

- `CHART_CONFIGS` e as janelas Hoje(24h deslizante) / 7d / 14d / Tudo
- Formatação de label por janela (`HH:MM` / `DD/MM` / `MM/AAAA`)
- Nota do cultivador (entra no prompt da IA)
- Rega remota e bloqueio de bomba
- Relatório de colheita em PDF
- Comportamento offline: **nunca esvaziar a tela**, manter os últimos dados visíveis

## Armadilhas (ver `docs/05-dashboard-v1.md`)

- **gviz**: hora vem em `.f`, nunca em `.v` (o `.v` retorna `Date(1899,...)`)
- **Encoding**: preservar UTF-8; o arquivo v1 já teve emojis corrompidos em produção
- **`mode:'no-cors'`** impede ler a resposta do POST — confirmação real só na releitura
