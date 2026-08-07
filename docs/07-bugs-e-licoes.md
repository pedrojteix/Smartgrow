# 07 — Bugs Resolvidos e Lições

> **Leia este arquivo antes de escrever qualquer código.**
> Cada item aqui custou horas de debug, hardware queimado ou dias de cultivo perdidos.
> Reintroduzir qualquer um deles é o erro mais caro possível neste projeto.

---

## A. Compilação (recorrentes no firmware)

**A1. `expected unqualified-id before else` por volta da linha 360**
`}` extra no `if(!dispOk){...} else{...}` do setup do display. **Ocorreu 3+ vezes.**
Sempre conferir o balanceamento de chaves nesse trecho antes de entregar.

**A2. Argumento default duplicado**
`iniciarRega(float volumeBase_mL, int numBomba = 1)` — o default vai **SÓ na forward
declaration**, NUNCA na definição. Erro: `-fpermissive: default argument given for parameter`.

**A3. Variáveis que precisam ser globais**
`tInicioRega_ms` e `tRegaTimeoutMs` já causaram `not declared in this scope` ao virarem locais.

**A4. Caractere não-ASCII quebra a compilação silenciosamente**
Acento em comentário do `.ino` produz erro sem relação aparente. **Limpar sempre.**

**A5. Forward declarations obrigatórias antes do `loop()`**
`iniciarRega`, `pararBomba`, `exibirFeedback`, `acordarDisplay`, `postGAS`,
`flushFilaRegas`, `enfileirarRega`, `salvarFilaRegas`, `carregarFilaRegas`.

---

## B. Lógica (recorrentes)

**B1. Clique residual na calibração — aconteceu 3 vezes em formas diferentes**
`exibirFeedback()` tem um `delay` interno de 1500 ms. Durante esse delay o `contadorCliques`
residual era reprocessado como "parar bomba" → a bomba ligava e desligava em 1 segundo.

> **Fix definitivo**: zerar `contadorCliques = 0; gestoConcluido = false;` **imediatamente
> antes** de qualquer delay ou feedback dentro do gesto. E, em gestos que ligam bomba,
> desenhar direto no display sem chamar `exibirFeedback`.

**B2. "Timeout 60s" disparando com 0 segundos**
`tRegaManualInicio` não era setado no início da calibração → `millis() - 0 > 60000` era
verdadeiro na hora. **Sempre setar `tRegaManualInicio = millis()` ao ligar a bomba em
modo manual ou calibração.**

**B3. Bypass da boia incompleto**
`nivelBaixo = false` foi forçado em `lerSensores()`, mas 3 outros pontos liam
`digitalRead(PINO_BOIA) == HIGH` direto (no `loop`, em `conectarWiFi` e em `exibirFeedback`)
→ a bomba parava sozinha. **Todo ponto deve usar a variável, nunca o `digitalRead` direto.**

**B4. Luz apagada no boot em horário de luz**
O NTP não sincronizava antes de `controlarLuz()`. **Fix**: aguardar o NTP até 10 s no `setup()`.

**B5. `gestoConcluido` zerado dentro do `case` com o botão ainda pressionado**
Re-disparava o gesto. **Não zerar `gestoConcluido` dentro dos cases.**

**B6. `diasCiclo = 0` do ESP32 fazia a IA tratar planta de 36 dias como muda**
**Fix**: o GAS calcula `diasReais` a partir da data cadastrada no site (`calcularDiasNoGrow`),
e ignora o valor do ESP32.

---

## C. Rede — o bug HTTP -11 (mai–jul/2026)

**Sintoma**: todo POST do firmware ao GAS retornava `HTTP -11`
(`HTTPC_ERROR_CONNECTION_REFUSED`). A IA nunca respondia → `regar_agora` nunca chegava →
**rega automática parada por quase um mês**, com solo seco e a IA "querendo" regar.

### Cadeia de diagnóstico (o método que funcionou)

| Etapa testada | Resultado |
|---|---|
| WiFi | OK (status 3, NTP sincroniza) |
| Heap no momento do POST | ~212 KB livres — não era memória |
| TCP/TLS connect a `script.google.com:443` | OK (retorna 1) no exato momento do POST |
| GET isolado em sketch de teste | **Funciona** (recebe 302) |
| POST do firmware | **Falha -11 sempre** (manual e automático) |
| Rega manual/calibração via BOOT | Funciona — hardware OK |

Descartados por evidência: créditos Google/Gemini (seria 429/403 na resposta, não -11 no
transporte), DNS/conectividade, heap, tamanho do payload (~260 bytes).

### O que resolveu (R09)

Centralizar **todo** o TLS num helper único `postGAS()`, com:

```c
client.setInsecure();
client.setTimeout(15);              // SEGUNDOS no WiFiClientSecure
http.setTimeout(15000);             // MILISSEGUNDOS no HTTPClient
http.addHeader("Content-Type", "text/plain");
http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
// POST → 302 → getLocation() → GET manual na URL final
```

Três fatores combinados: `text/plain` em vez de `application/json`, `setTimeout` explícito
no `WiFiClientSecure` (em segundos!), e **um único ponto de criação de contexto TLS**
— o que reduziu a fragmentação de heap causada pelos dois pontos de envio duplicados.

> **Lição para o 2.0**: nenhuma duplicação de contexto TLS. Uma função, um lugar.
> E o mais importante: **isso nunca deveria ter parado a rega.**
> A decisão de rega não pode viver na nuvem.

---

## D. Hardware — lições pagas em componentes

**D1. GPIO14 não serve para interrupção de fluxo** (conflito com SPI flash) → usar GPIO13.

**D2. SSR AC não chaveia DC.** O triac precisa do zero-crossing da rede.
Já se comprou um SSR acreditando que serviria para as bombas.

**D3. Motor DC sem diodo flyback solda o contato do relé mecânico.**
Sintoma: o LED do canal acende, o contato não abre/fecha; um peteleco destrava
temporariamente. **Dois canais perdidos assim (CH3 e CH4).**
**Pendência: 1N4007 em paralelo com cada bomba.**

**D4. Reguladora de protoboard rotulada "3.3V" entrega 3,6–3,8V** → matou **dois** BME280
(limite absoluto 3,6V). Sensor I2C sensível só no 3.3V do próprio ESP32.

**D5. Display e BME280 no mesmo barramento I2C corrompem a imagem** → `Wire2` dedicado
em GPIO 16/17.

**D6. O sensor de fluxo foi instalado ao contrário** nos primeiros testes, produzindo
leituras irreais e semanas de conclusões erradas. **Verificar o sentido da seta.**

---

## E. Dados e decisão

**E1. Sensores de fluxo não são confiáveis.**
Fator K não-linear: 148–251 pulsos/mL no sensor 1; 59–122 no sensor 2, **crescendo com o
tempo de ativação**. O sensor 2 é inútil em regas menores que 15 s.
**Decisão: rega para por TEMPO** (`VAZAO_ML_S = 8.4` mL/s, folga 1,3). Pulso é só telemetria.

**E2. `tUltimaRega_ms` usa `millis()` e zera no boot** → `horasUltimaRega = 999` →
a IA perde o histórico recente a cada reinício. **Persistir em epoch real no 2.0.**

**E3. Desequilíbrio hidráulico não é problema de software.**
Varia de 1,1% a 21,9% entre rodadas — é dinâmica de pressão, não assimetria fixa.
Corrigir com válvula de retenção, não com fator de compensação no código.

**E4. gviz: hora vem em `.f`, nunca em `.v`.**
O `.v` retorna `Date(1899,...)` para células formatadas como hora.

---

## F. Meta-lições do projeto

1. **Nenhum bug caro veio de algoritmo.** Todos vieram de hardware, de configuração de
   biblioteca ou de acoplamento entre subsistemas. O 2.0 deve investir em **isolamento** e
   **degradação segura**, não em sofisticação de lógica.

2. **A dependência da nuvem foi o erro arquitetural mais caro.** Um bug de biblioteca HTTP
   parou a irrigação de um cultivo real por um mês.

3. **Sketch isolado é a ferramenta de debug definitiva.** Toda vez que se testou uma etapa
   sozinha, a resposta apareceu. Toda vez que se tentou raciocinar sobre o sistema inteiro,
   perdeu-se tempo. **Manter isso como prática de primeira classe no 2.0** — inclusive com
   uma pasta de sketches de diagnóstico versionada.

4. **O display foi mais útil que o Serial Monitor.** Debug com o hardware montado, longe do
   computador, exige saída local. Preservar a calibração por gesto + display.

5. **Documentar entre sessões salvou o projeto.** O handoff de 08/05 e a skill do SmartGrow
   são o que permitiu continuidade. Manter `docs/` sempre à frente de `src/`.
