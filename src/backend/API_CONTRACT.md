# Contrato da API — SmartGrow 2.0

**Status**: rascunho. Escrever este contrato **antes** do código do backend (Fase 5).
Versão: `v2` (o campo `v` no payload identifica a versão).

Transporte: POST ao Web App do Apps Script, `Content-Type: text/plain`,
resposta seguindo redirect 302 manualmente com GET.
Ver `docs/07-bugs-e-licoes.md` seção C para o porquê.

---

## Princípio

> O backend **aconselha**, não comanda.
> Ele não pode ligar uma bomba. Ele pode sugerir parâmetros que o firmware aplica
> **se e somente se** caírem dentro de faixas seguras locais.

Isso é o ADR-02. Uma rota que devolva `regar_agora: true` violaria a arquitetura.

---

## Envelope comum

```json
{
  "v": 2,
  "device_id": "sgrw-01",
  "tipo": "<rota>",
  "ts": 1786060800,
  "...": "campos da rota"
}
```

| Campo | Tipo | Obrigatório |
|---|---|---|
| `v` | int | sim — rejeitar payload sem versão |
| `device_id` | string | sim — prepara multi-grow |
| `tipo` | string | sim |
| `ts` | int (epoch) | sim — o backend nunca confia no próprio relógio para a hora do evento |

---

## Rotas

### `telemetria` — substitui o `dados` da v1

**Request**
```json
{
  "v": 2, "device_id": "sgrw-01", "tipo": "telemetria", "ts": 1786060800,
  "fase": "floracao",
  "zonas": [
    { "id": 0, "nome": "planta2", "umidade": 42, "sensor_falho": false,
      "ultimo_volume_ml": 200, "epoch_ultima_rega": 1785974400 }
  ],
  "ambiente": { "temp_ar": 23.4, "umid_ar": 65.1, "pressao": 1012.3,
                "vpd": 1.01, "temp_terra": 21.8, "bme_ok": true, "ds18_ok": true },
  "sistema": { "galao_ml": 6800, "luz_ativa": 1, "regas_hoje": 1,
               "fila_pendente": 0, "uptime_s": 86400, "heap_livre": 212000 },
  "manual": false
}
```

**Response**
```json
{
  "ok": true,
  "conselho": {
    "valido_ate": 1786147200,
    "prompt_version": "2.0.1",
    "fase": {
      "limiar_umidade": 35, "volume_ciclo_ml": 800,
      "num_parcelas": 4, "intervalo_parcela_s": 300, "cooldown_s": 72000
    },
    "diagnostico": "...", "motivo": "...", "alerta_longo_prazo": "..."
  },
  "comandos": { "rega_remota_ml": 0, "bomba_bloqueada": -1 }
}
```

**Faixas seguras** que o firmware valida antes de aplicar `conselho.fase`
(rejeitar fora da faixa e registrar evento):

| Parâmetro | Mín | Máx |
|---|---|---|
| `limiar_umidade` | 20 | 60 |
| `volume_ciclo_ml` | 50 | 1200 |
| `num_parcelas` | 1 | 8 |
| `intervalo_parcela_s` | 60 | 1800 |
| `cooldown_s` | 21600 (6 h) | 259200 (72 h) |

`comandos.bomba_bloqueada`: `1` bloqueia · `0` desbloqueia · `-1` nada pendente.

---

### `rega` — registro de rega executada (fila store-and-forward)

```json
{ "v": 2, "device_id": "sgrw-01", "tipo": "rega", "ts": 1786060800,
  "trigger": "auto|manual|remota|calibracao",
  "zona": 0, "volume_ml": 200, "duracao_s": 48,
  "pulsos": 34784, "galao_ml": 6600, "fase": "floracao" }
```

Resposta: `{ "ok": true }`. Qualquer coisa diferente disso → o firmware **mantém na fila**.

---

### `evento`

```json
{ "v": 2, "device_id": "sgrw-01", "tipo": "evento", "ts": 1786060800,
  "evento": "galao_cheio", "descricao": "..." }
```

Tipos: `start_ciclo`, `galao_cheio`, `toggle_luz`, `envio_manual`, `bomba_on`, `bomba_off`,
`sensor_falho`, `rega_remota`, `bomba_bloqueada`, `alerta_rega`, `nota_cultivador`,
`dados_ciclo`, `analise_visual`, **`conselho_rejeitado`** (novo — quando o firmware ignora
um parâmetro fora da faixa), **`boot`** (novo — registra reinícios).

---

### `comando` — do dashboard para o sistema

```json
{ "v": 2, "device_id": "sgrw-01", "tipo": "comando", "ts": 1786060800,
  "comando": "rega_remota|bloqueio_bomba|reset_galao",
  "valor": 200 }
```

Grava na aba CONFIG. Entregue ao ESP32 no próximo ciclo de telemetria e limpo após entrega.

---

### `analise_visual`

Imagem base64 → Gemini Vision → evento com diagnóstico. Porte direto da v1.

---

## Mudanças em relação à v1

| v1 | v2 | Motivo |
|---|---|---|
| `regar_agora` na resposta | **Removido** | A decisão é do firmware (ADR-02) |
| `fator_rega`, `volume_ml` | `conselho.fase.*` com `valido_ate` | Conselho expira; firmware não fica preso a parâmetro velho |
| Sem versão | Campo `v` obrigatório | Permite evoluir sem quebrar dispositivo em campo |
| Sem `device_id` | Obrigatório | Prepara multi-grow |
| `umidade1`/`umidade2` | Array `zonas[]` | N zonas configuráveis (ADR-03) |
| Chave do Gemini no fonte | Script Properties | ADR-07 |
| Colunas por índice fixo | Escrita por nome de coluna | Evolução de schema sem quebrar |
| Sem versão de prompt | `prompt_version` gravado com a decisão | Correlacionar mudança de prompt com mudança de comportamento |

---

## Compatibilidade durante a migração

Durante a Fase 5–6 o backend deve aceitar **v1 e v2 simultaneamente** (roteamento pela
ausência/presença do campo `v`), para que o R09 continue funcionando enquanto o 2.0 é
validado em bancada. Remover o suporte a v1 só depois da Fase 6 concluída e observada.
