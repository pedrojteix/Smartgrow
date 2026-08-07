# 05 — Dashboard v1

Arquivo: `legacy/dashboard/index.html` — 1606 linhas, arquivo único (HTML + CSS + JS).
Publicado em GitHub Pages a partir do repositório `pedrojteix/Smartgrow`.
**Deploy = commit + push no `main`.**

---

## Stack

- HTML/CSS/JS puro, sem framework nem build.
- **Chart.js 4.4.1** via CDN. Fontes DM Sans / DM Mono.
- **Leitura**: gviz do Google Sheets (`/gviz/tq?...`) — sem autenticação.
- **Escrita**: POST ao Apps Script com `mode:'no-cors'` e `Content-Type: text/plain`
  (ou `redirect:'manual'` aceitando `opaqueredirect` como sucesso).
- Estado local: `localStorage` (`sg_ciclo_p1`, `sg_ciclo_p2`, `sg_notas`, histórico de ciclos).

---

## Seções, na ordem da página

1. **Autonomia** — dias restantes de galão
2. **Diagnóstico IA** — texto livre do Gemini
3. **Motivo da decisão**
4. **Fator de Rega** — barra 0–2 colorida
5. **Sensores** — scroll horizontal: U1, U2, Média, Vol/vaso, Vol total, TempAr, UmidAr, VPD, TempTerra
6. **Iluminação** — estado e horário da fase
7. **Plantas** — P1/P2 com pills de fase, formulário inline (germinação, vaso1, altura, entrada no SmartGrow), botão colher → PDF
8. **Gráficos**
9. **Reservatório** — anel SVG
10. **Hidráulica** — análise de frequência e resposta do solo
11. **Análises Visuais** — foto → Gemini Vision
12. **Últimos Eventos**
13. **Tabela REGAS**
14. **Nota do Cultivador**
15. **Rega Remota + Bloqueio de bomba**
16. **Histórico de Ciclos**

---

## Gráficos

- 5 tipos selecionáveis via `setTipoChart`: `solo` · `umid_ar` · `temp_ar` · `pressao` · `temp_solo`.
  Configuração centralizada em `CHART_CONFIGS` (datasets, opções de eixo Y, cores, nomes).
  Legenda dinâmica em `#chart-legend`.
- Janelas via `setJanela`: **Hoje (96 pontos)** · 7d (672) · 14d (1344) · Tudo (9999).
  "Hoje" é **janela deslizante de 24 h**, não desde a meia-noite (corrigido em jun/26).
- `formatarLabel(d, janela)`:
  - Hoje → `HH:MM`
  - 7d / 14d → `DD/MM`
  - Tudo → `MM/AAAA`
  Todos extraídos de `d.dataCompleta` (timestamp completo da coluna A via `.f || .v`).
- Grades verticais no X e horizontais no Y ativadas (`rgba(0,0,0,0.05–0.07)`).

---

## Estado global relevante

```js
tipoChart, janelaAtual
todosOsDados   // parse da aba DADOS:
               // u1, u2, media, vbase, vtotal, galaoMl, fase, fator,
               // diag, motivo, alerta, tempAr, umidAr, pressao, vpd,
               // tempTerra, dataCompleta
dadosCicloP1, dadosCicloP2   // localStorage sg_ciclo_p1 / sg_ciclo_p2
```

---

## Comandos remotos

| Função | Payload | Comportamento |
|---|---|---|
| `enviarRegaRemota()` | `{tipo:'rega_remota', volume_ml}` (50–1000 mL) | Executa em até 15 min (próximo ciclo de nuvem do ESP32) |
| `bloquearBomba()` / `desbloquearBomba()` → `enviarComandoBloqueio(1\|0)` | `{tipo:'bloqueio_bomba', bomba_bloqueada}` | Bloqueio persiste na CONFIG até ser lido pelo ESP32 |
| `enviarNotaCultivador()` | `{tipo:'evento', evento:'nota_cultivador', descricao}` (máx. 500 chars) | **Entra no prompt da IA** com alta prioridade nas 48 h seguintes. Últimas 5 em `localStorage.sg_notas`, renderizadas por `renderNotasRecentes()` |
| `resetGalao()` | evento `galao_cheio` | Espelha o gesto 1+segurar do BOOT |

---

## Status do ESP32 no header

`iniciarMonitorStatusESP32()` + `atualizarSubtituloConexao()`:

| Tempo desde a última leitura | Exibição |
|---|---|
| < 2 min | "Atualizado agora" |
| 2–20 min | "Última leitura: X min" |
| > 20 min | Badge **offline** + banner de aviso |

Dados antigos **permanecem visíveis** mesmo offline — nunca esvazia a tela.

---

## Ícones de eventos

`start_ciclo` 🌱 · `galao_cheio` 💧 · `toggle_luz` 💡 · `desequilibrio` ⚠️ · `dados_ciclo` 🌿 ·
`envio_manual` 📡 · `analise_visual` 🔬 · `bomba_on` 💧 · `bomba_off` ⏹ · `sensor_falho` ⚠️ ·
`rega_remota` 📱 · `alerta_rega` 🚨 · `bomba_bloqueada` 🔒 · `nota_cultivador` 📝

---

## Relatório de colheita

`colherPlanta(n)` → `gerarRelatorioPDF(n, ciclo, dados)` → `montarHTMLRelatorio(...)`:
gera um HTML de relatório e imprime como PDF, com resumo do ciclo, diagnósticos acumulados
e pontos de destaque. O ciclo encerrado vai para o histórico local.

---

## Cuidados conhecidos

1. **Encoding**: o HTML em produção já teve emojis e acentos corrompidos (`?`).
   Ao editar, preservar UTF-8 ou usar entities HTML.
2. **gviz e hora**: usar sempre `.f` para hora formatada; `.v` vem como `Date(1899,...)`.
3. **Autonomia**: usa a mesma fórmula do ESP32 (`vtotal` → `vbase*2` → fallback 600 mL).
   Se mudar num lado, mudar no outro.
4. `mode:'no-cors'` significa que **o site não consegue ler a resposta** do POST — o feedback
   ao usuário é otimista. Toda confirmação real vem depois, pela releitura do Sheets.

---

## O que o 2.0 precisa mudar

| Problema | Direção |
|---|---|
| 1606 linhas em arquivo único | Separar em módulos ES; opcionalmente um build simples |
| Duplicação de lógica com o firmware (autonomia, fases, ícones) | Contrato compartilhado — constantes derivadas de uma fonte só |
| `no-cors` cego | Rota GET de confirmação, ou proxy que permita CORS |
| Sem healthcheck real | Painel de saúde: última rega, falhas de envio, fila pendente, estado dos sensores |
| Estado de ciclo em `localStorage` | Fonte de verdade no backend; `localStorage` só como cache |
| Acoplado a "duas plantas" | Renderização dirigida por configuração de N zonas |
