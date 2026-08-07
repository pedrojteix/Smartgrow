# legacy/ — SmartGrow v1 congelado

> **Este código é especificação, não fonte.** Não copie para `src/`.
> Ele existe para você entender *o que o sistema faz e por quê* — inclusive as decisões
> contra-intuitivas, que quase sempre têm uma cicatriz de hardware por trás.

---

## Firmware

| Arquivo | Data | Papel |
|---|---|---|
| **`SGRW_ESP32_R09.ino`** | 18/07/2026 | **BASELINE — é o que está gravado no ESP32 hoje.** SSR ativo HIGH, helper `postGAS()` único (resolveu o HTTP -11), fila de regas em NVS |
| `SGRW_ESP32_R10_experimental.ino` | 06/08/2026 | **Não gravado.** Planta 1 eliminada, uma bomba (CH3/GPIO19), `VAZAO_ML_S=4.2`, desequilíbrio desativado, **rega autônoma local parcelada** — a ideia foi promovida a requisito do 2.0, o código não |
| `SGRW_ESP32_R08.ino` | 12/07/2026 | Duas bombas no CH2, calibração em 3 cliques. Relés ainda **ativo LOW** |
| `SGRW_ESP32_R07.ino` | 13/06/2026 | Bloqueio remoto, limites de rega, tentativas contra o HTTP -11 |
| `SGRW_ESP32_R06.ino` | 03/05/2026 | Detecção de sensor falho, rega remota, fix da luz no boot |
| `SGRW_ESP32_R05.ino` | 01/05/2026 | Primeira tríade completa versionada junta |
| `SGRW_ESP32_R04.ino` | 26/04/2026 | Primeiro firmware "de verdade" |
| `SGRW_ESP32_F3_R00.ino` | 18/04/2026 | Prova de conceito original |

> ⚠️ **Atenção à inversão de lógica**: do R09 em diante `RELE_ON = HIGH`.
> No R08 e anteriores era `RELE_ON = LOW`. Ler firmware antigo sem notar isso leva
> a conclusões invertidas.

### testes/

Sketches isolados — a ferramenta de debug padrão do projeto:

| Arquivo | Uso |
|---|---|
| `Testes_hidraulicos.ino` | Levantamento de vazão e fator K do sensor de fluxo |
| `Testes_conexaoIA.ino` | Prova de conceito ESP32 → Apps Script → Gemini |

---

## Backend (Google Apps Script)

| Arquivo | Papel |
|---|---|
| **`SGRW_APPSCR_R07.gs`** | **Publicado hoje.** 756 linhas. Vinha nomeado `SGRW_APPSCR_R00.js` no repositório — o cabeçalho diz R07, que é a versão real |
| `SGRW_APPSCR_R06.gs` | 03/05/2026 |
| `SGRW_APPSCR_R05.gs` | 01/05/2026 |
| `backup_apps_script_2026-04-30.txt` | Backup em texto puro |

---

## Dashboard

| Arquivo | Papel |
|---|---|
| **`index.html`** | **Em produção** no GitHub Pages. 1606 linhas |
| `index_2026-05-03.html` | Versão intermediária |
| `backup_index_2026-04-30.txt` | Backup em texto puro |

---

## Como usar isto durante a reescrita

1. Vá ao doc correspondente em `docs/` primeiro — ele já traduziu o código em requisito.
2. Consulte o R09 quando precisar do **detalhe exato** (um limiar, a ordem de uma guarda,
   o layout de uma página do display).
3. Se encontrar algo estranho, **assuma que há um motivo** e procure em
   `docs/07-bugs-e-licoes.md` antes de "corrigir".
4. Ao terminar um módulo do 2.0, compare o comportamento com o R09 rodando no mesmo hardware.
