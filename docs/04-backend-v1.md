# 04 — Backend v1 (Google Apps Script R07)

Arquivo: `legacy/backend/SGRW_APPSCR_R07.gs` — 756 linhas.
Publicado como Web App (`/exec`). Banco de dados: Google Sheets.

> **Toda edição exige um novo Deploy** para entrar em vigor. Salvar não basta.

---

## Roteamento — `doPost(e)`

O roteamento é por campo `tipo` no JSON do corpo:

| `tipo` | Função | O que faz |
|---|---|---|
| `evento` | `processarEvento` | Grava linha na aba EVENTOS |
| `dados_ciclo` | `salvarDadosCiclo` | Grava `dados_ciclo_P1` / `dados_ciclo_P2` (JSON com germinacao, vaso1, altura, smartgrow) |
| `analise_visual` | `analisarImagem` | Gemini Vision sobre foto da planta |
| `rega_remota` | `processarRegaRemota` | Grava `rega_remota_ml` na CONFIG |
| `bloqueio_bomba` | `processarBloqueio` | Grava `bomba_bloqueada` na CONFIG |
| `rega` | `processarRega` | Grava linha na aba REGAS (fila store-and-forward do ESP32) |
| *(default)* | `processarDados` | Fluxo principal com IA |

Body vazio → responde `eco_ignorado` (evita o eco do próprio redirect).
Erro em qualquer rota → `gravarErro()` + resposta segura `{fator_rega:1.0, volume_ml:0, regar_agora:false}`.

---

## Fluxo principal — `processarDados`

1. Extrai o payload com defaults seguros; calcula `mediaUmidade` e `diferenca`.
2. Se `manual == true`, registra evento `envio_manual`.
3. **Busca as notas do cultivador**: últimas 3 do tipo `nota_cultivador` nas últimas 48 h.
   São injetadas no prompt como **alta prioridade, ANTES do contexto da planta**.
4. **`calcularDiasNoGrow(ss, diasCiclo)`**: varre a aba EVENTOS procurando
   `dados_ciclo_P1`/`P2`, extrai o campo `smartgrow` (`YYYY-MM-DD`), pega a data mais
   recente e calcula os dias até hoje.
   > **A IA recebe `diasReais`, não o `diasCiclo` do ESP32.** O ESP32 enviava `0`, o que
   > fazia a IA tratar uma planta de 36 dias como muda no dia 0.
5. Chama `consultarGemini(d)`.
6. Grava linha na aba DADOS com `insertRowBefore(2)` — **mais recente no topo** — 23 colunas.
7. Lê a CONFIG: `rega_remota_ml` (zera após ler) e `bomba_bloqueada` (grava `-1` após enviar).
8. Responde ao ESP32:

```json
{ "fator_rega": 1.0, "volume_ml": 300, "regar_agora": true,
  "rega_remota_ml": 0, "bomba_bloqueada": -1 }
```

---

## Abas da planilha

Planilha ID em `docs/10-credenciais-e-endpoints.md`. Todas inserem no topo (`insertRowBefore(2)`).

### DADOS — 23 colunas (A→W)

| Col | Campo | Col | Campo |
|---|---|---|---|
| A | Timestamp | M | Dias Ciclo (**diasReais**) |
| B | Hora | N | Luz |
| C | Umidade1 (%) | O | Fator Rega |
| D | Umidade2 (%) | P | Diagnóstico IA |
| E | Média (%) | Q | Motivo Decisão |
| F | Diferença (%) | R | Alerta Longo Prazo |
| G | Desequilíbrio | S | Temp Ar (°C) |
| H | AlertaBoia | T | Umidade Ar (%) |
| I | Vol/Vaso (mL) | U | Pressão (hPa) |
| J | Vol Total (mL) | V | VPD (kPa) |
| K | Galão (mL) | W | Temp Terra (°C) |
| L | Fase | | |

### EVENTOS — 6 colunas

`Timestamp | Hora | Evento | Descrição | Fase | Dias Ciclo`

Tipos de evento: `start_ciclo`, `galao_cheio`, `toggle_luz`, `desequilibrio_hidraulico`,
`envio_manual`, `dados_ciclo_P1`, `dados_ciclo_P2`, `analise_visual_P1`, `analise_visual_P2`,
`bomba_on`, `bomba_off`, `sensor_falho`, `rega_remota`, `bomba_bloqueada`, `alerta_rega`,
`nota_cultivador`, `rega_autonoma` *(introduzido no R10 experimental)*.

`start_ciclo` responde `{acao:"abrir_formulario_ciclo", ciclo_id:...}` — o site abre o formulário.

### REGAS — 10 colunas

`Timestamp | Hora | Trigger | Vaso 1 (mL) | Vaso 2 (mL) | Total (mL) | Duração (s) | Galão (mL) | Fase | Dias Ciclo`

Alimentada pela fila store-and-forward do firmware (`tipo:"rega"`).
A coluna A é forçada para `dd/MM/yyyy HH:mm:ss` — sem isso o Sheets salva como Time-only
e o gviz devolve data quebrada no dashboard.

### CONFIG — chave | valor

| Chave | Semântica |
|---|---|
| `rega_remota_ml` | Volume pendente enviado pelo site. O ESP32 executa; o GAS zera após entregar. |
| `bomba_bloqueada` | `1` bloqueia · `0` desbloqueia · `-1` nada pendente (gravado após entregar ao ESP32). |

---

## `consultarGemini(d)` — o prompt

Modelo: **gemini-2.5-flash** via `generativelanguage.googleapis.com/v1beta`.

### Tabela de decisão embutida no prompt

| Fase | Dias | Volume por vaso | Frequência |
|---|---|---|---|
| Muda | 0–14 | 75–150 mL | 48 h |
| Vegetação | 15–28 | 200–300 mL | 48–72 h |
| Floração inicial | 29–49 | 500 mL | 24 h |
| Floração plena | 50–70 | 700–900 mL | 24 h |
| Floração final | 71+ | 500–700 mL | 24–48 h |

### Bloqueios (forçam `regar_agora = false`)

- Boia acionada (`alertaBoia == 1`).
- Muda com média ≥ 65%.
- Vegetação ou floração com média ≥ 80%.
- Frequência mínima da fase ainda não cumprida.
- Desequilíbrio com média > 50% (exceto se **ambos** os sensores < 30%).
- Galão < 500 mL (exceto emergência: média < 30%).

### Limitações que o prompt informa explicitamente à IA

- `galaoMl` é estimativa, não medição direta.
- `horasUltimaRega = 999` significa "nunca regou nesta sessão do ESP32", não "nunca regou".
- O sensor mede **molhamento periférico** (posição entre o anel e a borda do vaso).
- `diasCiclo` vem da data cadastrada no site, não do ESP32.

### Retorno esperado (JSON puro)

```json
{ "regar_agora": true, "fator_rega": 1.0, "volume_ml": 300,
  "diagnostico": "...", "motivo_decisao": "...", "alerta_longo_prazo": "..." }
```

`volume_ml` é **por vaso**. O firmware multiplica por 2.

### Fallback — `_erroIA(msg)`

Se a chamada ao Gemini falhar ou o parse quebrar: `fator_rega = 1.0`, `volume_ml = 0`,
`regar_agora = false`. **Falha segura: na dúvida, não rega.**

---

## Outras rotas

- **`analisarImagem`**: recebe imagem base64 do site, chama Gemini Vision, grava evento
  `analise_visual_P1`/`P2` com o diagnóstico.
- **`salvarDadosCiclo`**: recebe do site as datas de germinação, primeiro vaso, entrada no
  SmartGrow e altura; grava como JSON na descrição do evento. É a fonte de `calcularDiasNoGrow`.
- **`doGet`**: usado para healthcheck / retorno do redirect.
- **`testarPayloadCompleto` / `testarBloqueio`**: funções de teste manuais no editor do Apps Script.

---

## Armadilhas conhecidas

1. **Deploy obrigatório** após qualquer edição.
2. **gviz e datas**: a hora formatada vem no campo `.f`; o `.v` vem como `Date(1899,...)`.
   **Nunca usar `.v` para exibir hora.**
3. A planilha **não está pública** — leitura externa exige compartilhamento.
4. `insertRowBefore(2)` em toda escrita: barato agora, mas é O(n) na planilha e vai degradar
   conforme o histórico cresce.

---

## O que o 2.0 precisa mudar

| Problema | Direção |
|---|---|
| Chave do Gemini hardcoded no fonte | `PropertiesService` (Script Properties) |
| Prompt gigante embutido no meio da lógica | Prompt versionado em arquivo/constante própria, com número de versão gravado junto da decisão |
| Roteamento por `if` encadeado | Tabela de rotas + validação de schema do payload |
| Sem contrato de API | Documento de contrato versionado (`API_CONTRACT.md`), com versão no payload |
| Nenhum teste | Funções de teste reais no Apps Script cobrindo cada rota |
| Colunas fixas por posição | Escrita por nome de coluna, resolvido a partir do cabeçalho |
| Decisão de rega só aqui | Decisão migra para o firmware; o GAS passa a devolver **conselho** (ajustes de parâmetro), não comando |
