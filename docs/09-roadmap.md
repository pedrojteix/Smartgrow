# 09 — Roadmap da Reescrita

Ordem de execução. Cada fase é **entregável e testável isoladamente**.
A v1 (R09) continua rodando o cultivo até a Fase 6.

> **Regra de segurança**: nenhuma fase toca no ESP32 que está regando a planta até a
> Fase 6. Todo desenvolvimento acontece em bancada — segundo ESP32, bomba dentro de um
> copo d'água, sem planta envolvida.

---

## Fase 0 — Toolchain (½ dia)

**Objetivo**: provar que o PlatformIO funciona no ambiente do Pedro antes de mover qualquer lógica.

- [ ] Instalar PlatformIO (extensão do VS Code).
- [ ] `src/firmware/platformio.ini` com board `esp32dev`, **versão de plataforma travada**.
- [ ] Blink + "Hello" no OLED, compilado e gravado via PlatformIO.
- [ ] `pio test -e native` rodando um teste trivial (prova que o ambiente de host funciona).
- [ ] `.gitignore` e `secrets.example.h`.

**Critério de saída**: `pio run -t upload` grava e `pio test -e native` passa.

> Se o PlatformIO atrapalhar mais do que ajuda, **pare aqui e reavalie o ADR-01**.
> Não vale reescrever tudo num toolchain que atrita.

---

## Fase 1 — Config e Clock (1–2 dias)

**Objetivo**: a base sobre a qual todo o resto se apoia.

- [ ] `lib/Config`: `struct ConfigSistema` versionado + persistência NVS + defaults + migração.
- [ ] `struct Zona` conforme ADR-03; defaults reproduzindo o hardware atual.
- [ ] `lib/Clock`: NTP com espera de 10 s no boot (lição B4), epoch persistido,
      `horasDesde(epoch)` que **sobrevive ao reboot** (corrige a lição E2).
- [ ] `include/secrets.h` fora do versionamento.
- [ ] Testes: serialização/desserialização da config, migração de versão, cálculo de horas.

**Critério de saída**: gravar config, reiniciar o ESP32, ler de volta idêntica.
`horasDesdeUltimaRega` correto após reboot.

---

## Fase 2 — Sensors (1–2 dias)

- [ ] Leitura dos capacitivos com mapeamento calibrado por zona.
- [ ] DS18B20 e BME280 (Wire2 dedicado — lição D5), com `bmeOk`/`ds18Ok`.
- [ ] `calcularVPD()` — função pura, testada contra a tabela de `docs/06-agronomia.md`.
- [ ] Detecção de sensor falho: queda > 30% em uma leitura → suspeito; 3 consecutivas → falho.
      **Subida não dispara.**
- [ ] Testes em host: mapeamento, VPD, máquina de detecção de falha (sequências sintéticas).

**Critério de saída**: leituras coerentes com o R09 rodando no mesmo hardware.
Bateria de testes de detecção de falha verde.

---

## Fase 3 — Irrigation (3–4 dias) — **o coração**

- [ ] `decidir(estado, config) → IntencaoRega` — **função pura, sem hardware**.
      Implementa o motor local: limiar por fase, parcelamento, cooldown, limite diário.
- [ ] `executar(intencao)` — aciona relé, parada por **tempo** (lição E1), guardas da v1:
      `bombaHabilitada`, `nivelBaixo`, `executandoRega`, `bombaRemotaBloqueada`, limite diário.
- [ ] Timeout absoluto + watchdog (ADR-08 item 1).
- [ ] Fila store-and-forward em NVS (porte direto do R09, generalizado para N zonas).
- [ ] Desconto do galão por tempo estimado, com fluxo como telemetria.
- [ ] **Testes escritos ANTES do código**, derivados de `docs/06-agronomia.md`:
      cada linha da tabela de fases vira um caso de teste. Mais os casos de bloqueio.

**Critério de saída**: bomba em copo d'água executa um ciclo parcelado completo
(4×200 mL, 5 min de intervalo) com o **WiFi desligado**. Volume medido bate com o alvo ±10%.

---

## Fase 4 — Lighting, Display e Gestures (2–3 dias)

- [ ] `Lighting`: fotoperíodo por fase, `horaDesligar == 0` = meia-noite, override
      (3h OFF / 1h ON, repetir cancela), espera de NTP no boot.
- [ ] **Trava do breu** (ADR-08 item 3): nenhum caminho acende a luz no escuro da floração.
      Teste dedicado tentando todos os caminhos.
- [ ] `Display`: 3 páginas do R09, sleep 2 min/30 min, bitmap da folha, `DISP_X = 58`.
      S1 e S2 sempre em linhas separadas.
- [ ] `Gestures`: máquina de cliques na descida, `TEMPO_CLIQUE_MAX = 400`, `TEMPO_SEGURAR = 2000`.
      **Zerar `contadorCliques` e `gestoConcluido` antes de qualquer delay** (lição B1).
      Não zerar `gestoConcluido` dentro do case (lição B5).
- [ ] Máquina de calibração completa (estados 0/2/3/4), com `tRegaManualInicio` setado ao
      ligar a bomba (lição B2).
- [ ] Testes: agendamento de luz em 24 h sintéticas, trava do breu, sequências de cliques.

**Critério de saída**: todos os gestos do R09 funcionam idêntico. Calibração produz o
mesmo resultado que a v1 no mesmo hardware.

---

## Fase 5 — Cloud e Backend (2–3 dias)

- [ ] `lib/Cloud` com interface abstrata; implementação Apps Script.
- [ ] `postGAS()` único, com a configuração exata que resolveu o HTTP -11 (lição C).
- [ ] Payload por zonas, com campo de versão `v`.
- [ ] `aplicarConselho()`: valida os parâmetros da IA contra faixas seguras locais;
      fora da faixa → ignora e registra evento.
- [ ] Backend: `API_CONTRACT.md`, chave em Script Properties, prompt versionado,
      escrita por nome de coluna, funções de teste por rota.
- [ ] `lib/Diag`: log estruturado, contadores de falha de envio, payload de healthcheck.

**Critério de saída**: ESP32 envia, recebe conselho, aplica dentro da faixa e **rejeita**
um conselho fora da faixa injetado propositalmente. Desligar o WiFi por 24 h não altera
o comportamento de rega.

---

## Fase 6 — Migração para produção (1 dia + observação)

- [ ] **Instalar os diodos 1N4007** nas bombas (pendência de hardware #1).
- [ ] Instalar o BME280 novo no 3.3V do ESP32.
- [ ] Rodar o firmware 2.0 em bancada por **7 dias** com o WiFi desligado, medindo volumes.
- [ ] Congelar o R09 como `rollback/` — gravável a qualquer momento.
- [ ] Migrar o ESP32 de produção. **Observar por 72 h com o galão cheio e checagem diária.**
- [ ] Só depois: migrar o dashboard.

**Critério de saída**: 72 h em produção sem intervenção manual, volumes corretos,
histórico completo no Sheets.

---

## Fase 7 — Dashboard 2.0 (2–3 dias)

- [ ] Separar em módulos ES.
- [ ] Painel de saúde do sistema (online/offline, última rega, fila pendente, falhas, sensores).
- [ ] Renderização dirigida pela configuração de zonas.
- [ ] Constantes compartilhadas com o firmware a partir de uma fonte única.

---

## Backlog (depois do 2.0 estável)

- Rotacionar a chave do Gemini e tornar o repositório público seguro.
- Migrar da protoboard para a PCB (`hardware/kicad/SGRW_Controller`).
- Definir e testar a lógica da boia — ou remover o sensor do projeto.
- Controle de nutrientes / pH.
- Exaustão e umidificação (fechar o laço de VPD, hoje só observado).
- Multi-grow / preparação para produto (ver `referencias/relatorios/SGRW_MODELO_NEGOCIOS.pdf`).

---

## Estimativa total

**12 a 18 dias de trabalho efetivo** até a Fase 6.
As fases 3 e 4 concentram o risco — são onde vive o conhecimento agronômico e as
armadilhas de gesto/display.

---

## Como trabalhar cada fase com o Claude Code

1. Abrir a pasta. O `CLAUDE.md` carrega o contexto.
2. Dizer qual fase e apontar o doc de referência (ex: "Fase 3, `docs/03-firmware-v1.md`
   seção Rega + `docs/06-agronomia.md`").
3. **Pedir os testes antes do código.** A tabela de fases da agronomia é a especificação.
4. Comparar o comportamento novo com o R09 em `legacy/` — ele é a especificação executável.
5. Ao fim de cada fase, atualizar `docs/` **antes** de passar para a próxima.
