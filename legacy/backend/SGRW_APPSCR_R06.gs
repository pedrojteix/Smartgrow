// ============================================================
// SMARTGROW — Apps Script R06
// Abas: DADOS (23 colunas A→W) e EVENTOS (6 colunas)
// Prompt agronômico revisado para mudas recém-transplantadas
// Transplante para vasos 11L: 30/04/2026
// + Suporte a rega remota via site (aba CONFIG)
// + Sensor falho registrado nos dados
// + Prompt IA atualizado: ignora sensor 0% por queda abrupta
// ============================================================

const GEMINI_API_KEY = "AIzaSyDj9usPFgeSXayaHzh3xRGNOcAqcbb-jgc";
const GEMINI_URL     = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + GEMINI_API_KEY;

const ABA_DADOS   = "DADOS";
const ABA_EVENTOS = "EVENTOS";
const ABA_CONFIG  = "CONFIG";  // chave:valor -- rega_remota_ml

// Cabeçalhos — configurar linha 1 manualmente na planilha
// DADOS (23 colunas A→W):
// A: Timestamp | B: Hora | C: Umidade1(%) | D: Umidade2(%) | E: Média(%)
// F: Diferença(%) | G: Desequilíbrio | H: AlertaBoia | I: Vol/Vaso(mL)
// J: Vol Total(mL) | K: Galão(mL) | L: Fase | M: Dias Ciclo | N: Luz
// O: Fator Rega | P: Diagnóstico IA | Q: Motivo Decisão | R: Alerta Longo Prazo
// S: Temp Ar(°C) | T: Umidade Ar(%) | U: Pressão(hPa) | V: VPD(kPa) | W: Temp Terra(°C)
//
// EVENTOS (6 colunas):
// A: Timestamp | B: Hora | C: Evento | D: Descrição | E: Fase | F: Dias Ciclo

// ============================================================
// ENTRY POINT
// ============================================================
function doPost(e) {
  if (!e || typeof e.postData === "undefined" || !e.postData.contents) {
    return responderTexto("eco_ignorado");
  }
  var ss    = SpreadsheetApp.getActiveSpreadsheet();
  var dados = null;
  try {
    dados = JSON.parse(e.postData.contents);
    if (dados.tipo === "evento")         return processarEvento(ss, dados);
    if (dados.tipo === "dados_ciclo")    return salvarDadosCiclo(ss, dados);
    if (dados.tipo === "analise_visual") return analisarImagem(ss, dados);
    if (dados.tipo === "rega_remota")    return processarRegaRemota(ss, dados);
    return processarDados(ss, dados);
  } catch (err) {
    var msgErro = err.message + " | payload: " + (dados ? JSON.stringify(dados).substring(0, 200) : "null");
    gravarErro(ss, dados, msgErro);
    return responderJSON({ fator_rega: 1.0, volume_ml: 0, regar_agora: false });
  }
}

// ============================================================
// PROCESSAR DADOS
// ============================================================
function processarDados(ss, dados) {
  var abaD = obterOuCriarAba(ss, ABA_DADOS);

  var umidade1        = dados.umidade1        || 0;
  var umidade2        = dados.umidade2        || 0;
  var alertaBoia      = dados.alertaBoia      || 0;
  var volumeBaseML    = dados.volumeBaseML    || 0;
  var volumeTotalML   = dados.volumeTotalML   || 0;
  var fasePlanta      = dados.fasePlanta      || "vegetacao";
  var diasCiclo       = dados.diasCiclo       || 0;
  var galaoMl         = dados.galaoMl         || 7000;
  var desequil        = dados.desequilibrio   || 0;
  var luzAtiva        = dados.luzAtiva        || 0;
  var manual          = dados.manual          || false;
  var horasUltimaRega = dados.horasUltimaRega || 999;
  var tempAr          = dados.tempAr     != null ? parseFloat(dados.tempAr)    : null;
  var umidadeAr       = dados.umidadeAr  != null ? parseFloat(dados.umidadeAr) : null;
  var pressaoHpa      = dados.pressaoHpa != null ? parseFloat(dados.pressaoHpa): null;
  var vpd             = dados.vpd        != null ? parseFloat(dados.vpd)       : null;
  var tempTerra       = dados.tempTerra  != null ? parseFloat(dados.tempTerra) : null;

  var mediaUmidade = ((umidade1 + umidade2) / 2).toFixed(1);
  var diferenca    = Math.abs(umidade1 - umidade2);

  if (manual) {
    var abaEvt  = obterOuCriarAba(ss, ABA_EVENTOS);
    var agoraEvt = new Date();
    abaEvt.insertRowBefore(2);
    abaEvt.getRange(2, 1, 1, 6).setValues([[
      agoraEvt,
      pad2(agoraEvt.getHours()) + ":" + pad2(agoraEvt.getMinutes()),
      "envio_manual",
      "Dados enviados manualmente via BOOT",
      fasePlanta,
      diasCiclo
    ]]);
  }

  var resultadoIA = consultarGemini({
    umidade1, umidade2, mediaUmidade, alertaBoia,
    volumeBaseML, fasePlanta, diasCiclo, galaoMl,
    diferenca, desequil, luzAtiva, horasUltimaRega,
    tempAr, umidadeAr, pressaoHpa, vpd, tempTerra
  });

  var agora   = new Date();
  var horaFmt = pad2(agora.getHours()) + ":" + pad2(agora.getMinutes()) + ":" + pad2(agora.getSeconds());

  abaD.insertRowBefore(2);
  abaD.getRange(2, 1, 1, 23).setValues([[
    agora,
    horaFmt,
    umidade1,
    umidade2,
    parseFloat(mediaUmidade),
    diferenca,
    desequil === 1 ? "SIM" : "NAO",
    alertaBoia,
    volumeBaseML,
    volumeTotalML,
    galaoMl,
    fasePlanta,
    diasCiclo,
    luzAtiva === 1 ? "ON" : "OFF",
    resultadoIA.fator_rega,
    resultadoIA.diagnostico,
    resultadoIA.motivo_decisao,
    resultadoIA.alerta_longo_prazo,
    tempAr     !== null ? tempAr     : "",
    umidadeAr  !== null ? umidadeAr  : "",
    pressaoHpa !== null ? pressaoHpa : "",
    vpd        !== null ? vpd        : "",
    tempTerra  !== null ? tempTerra  : ""
  ]]);

  // Verifica se ha rega remota pendente na aba CONFIG
  var regaRemotaMl = 0;
  try {
    var cfgAba = ss.getSheetByName(ABA_CONFIG);
    if (cfgAba) {
      var cfgDados = cfgAba.getDataRange().getValues();
      for (var ci = 1; ci < cfgDados.length; ci++) {
        if (cfgDados[ci][0] === "rega_remota_ml" && parseFloat(cfgDados[ci][1]) > 0) {
          regaRemotaMl = parseFloat(cfgDados[ci][1]);
          cfgAba.getRange(ci+1, 2).setValue(0); // limpa apos ler
          Logger.log("[REGA_REMOTA] Comando enviado ao ESP32: " + regaRemotaMl + "mL");
          break;
        }
      }
    }
  } catch(e) { Logger.log("Erro ao ler CONFIG: " + e.message); }

  return responderJSON({
    fator_rega:     resultadoIA.fator_rega,
    volume_ml:      resultadoIA.volume_ml,
    regar_agora:    resultadoIA.regar_agora,
    rega_remota_ml: regaRemotaMl
  });
}


// ============================================================
// REGA REMOTA — recebe comando do site e grava na aba CONFIG
// O ESP32 le CONFIG a cada ciclo (15min) e executa a rega
// ============================================================
function processarRegaRemota(ss, dados) {
  var volume = parseFloat(dados.volume_ml) || 0;
  if (volume <= 0 || volume > 7000) {
    return responderJSON({ status: "erro", msg: "Volume invalido: " + volume });
  }

  // Grava o comando na aba CONFIG (cria se nao existir)
  var cfg = ss.getSheetByName(ABA_CONFIG);
  if (!cfg) {
    cfg = ss.insertSheet(ABA_CONFIG);
    cfg.getRange(1,1,1,2).setValues([["chave","valor"]]).setFontWeight("bold");
  }

  // Busca linha existente de rega_remota_ml ou cria nova
  var dados_cfg = cfg.getDataRange().getValues();
  var linhaExistente = -1;
  for (var i = 1; i < dados_cfg.length; i++) {
    if (dados_cfg[i][0] === "rega_remota_ml") { linhaExistente = i + 1; break; }
  }
  if (linhaExistente > 0) {
    cfg.getRange(linhaExistente, 2).setValue(volume);
  } else {
    cfg.appendRow(["rega_remota_ml", volume]);
  }

  // Registra evento
  var abaE = obterOuCriarAba(ss, ABA_EVENTOS);
  var agora = new Date();
  abaE.insertRowBefore(2);
  abaE.getRange(2,1,1,6).setValues([[
    agora,
    pad2(agora.getHours())+":"+pad2(agora.getMinutes()),
    "rega_remota",
    "Rega remota solicitada via site: " + volume + "mL total",
    dados.fasePlanta || "vegetacao",
    0
  ]]);

  Logger.log("[REGA_REMOTA] Comando gravado: " + volume + "mL");
  return responderJSON({ status: "ok", volume_ml: volume, msg: "Comando enviado ao ESP32 no proximo ciclo (15min)" });
}

// ============================================================
// PROCESSAR EVENTOS
// ============================================================
function processarEvento(ss, dados) {
  var abaE = obterOuCriarAba(ss, ABA_EVENTOS);
  var agora = new Date();
  var hora  = dados.hora   || agora.getHours();
  var min   = dados.minuto || agora.getMinutes();

  abaE.insertRowBefore(2);
  abaE.getRange(2, 1, 1, 6).setValues([[
    agora,
    pad2(hora) + ":" + pad2(min),
    dados.evento    || "",
    dados.descricao || "",
    dados.fasePlanta|| "",
    dados.diasCiclo || 0
  ]]);

  if (dados.evento === "start_ciclo") {
    return responderJSON({ acao: "abrir_formulario_ciclo", ciclo_id: dados.descricao || "" });
  }

  return responderTexto("evento_registrado");
}

// ============================================================
// SALVAR DADOS DO CICLO DAS PLANTAS (via site)
// ============================================================
function salvarDadosCiclo(ss, dados) {
  var abaE  = obterOuCriarAba(ss, ABA_EVENTOS);
  var agora = new Date();

  if (dados.p1) {
    abaE.insertRowBefore(2);
    abaE.getRange(2, 1, 1, 6).setValues([[
      agora,
      pad2(agora.getHours()) + ":" + pad2(agora.getMinutes()),
      "dados_ciclo_P1",
      JSON.stringify(dados.p1),
      dados.fasePlanta || "vegetacao",
      0
    ]]);
  }
  if (dados.p2) {
    abaE.insertRowBefore(2);
    abaE.getRange(2, 1, 1, 6).setValues([[
      agora,
      pad2(agora.getHours()) + ":" + pad2(agora.getMinutes()),
      "dados_ciclo_P2",
      JSON.stringify(dados.p2),
      dados.fasePlanta || "vegetacao",
      0
    ]]);
  }

  return responderJSON({ status: "ok" });
}

// ============================================================
// PROMPT DA IA — R05
// Revisado para mudas recém-transplantadas (30/04/2026)
// Vasos 11L — substrato 1:1:1 (perlita/húmus/fibra de coco)
// ============================================================
function consultarGemini(d) {
  var faseTexto = {
    "germinacao": "Germinação/Muda",
    "vegetacao":  "Vegetação",
    "floracao":   "Floração"
  }[d.fasePlanta] || d.fasePlanta;

  var galaoL     = (d.galaoMl / 1000).toFixed(1);
  var deseqTexto = d.desequil === 1 ? "SIM ⚠️ (diferença: " + d.diferenca + "%)" : "NAO";
  var luzTexto   = d.luzAtiva === 1 ? "LIGADA" : "DESLIGADA";

  var climaTexto = "";
  if (d.tempAr !== null && d.umidadeAr !== null) {
    climaTexto =
      "- Temperatura do ar: " + d.tempAr + " °C\n" +
      "- Umidade relativa do ar: " + d.umidadeAr + "%\n" +
      (d.vpd        !== null ? "- VPD: "            + d.vpd        + " kPa\n" : "") +
      (d.pressaoHpa !== null ? "- Pressão atm.: "   + d.pressaoHpa + " hPa\n" : "") +
      (d.tempTerra  !== null ? "- Temp. da terra: " + d.tempTerra  + " °C\n"  : "");
  } else {
    climaTexto = "- Dados climáticos: não disponíveis (BME280 e DS18B20 pendentes de integração)\n";
  }

  var prompt =
    "Você é uma IA agronômica especializada em Cannabis sativa indoor.\n" +
    "Sua função é decidir irrigação a cada 15 minutos para um sistema com as seguintes características FIXAS:\n" +
    "- 2 vasos de fabric pot 11L com substrato 1:1:1 (perlita/húmus/fibra de coco)\n" +
    "- 1 bomba única alimenta os 2 vasos simultaneamente via conector T + anéis de irrigação\n" +
    "- Os anéis têm furos voltados para FORA do anel (irrigação periférica → solo umedece de fora para dentro)\n" +
    "- O campo 'volume_ml' que você retorna é POR VASO — o sistema dobra automaticamente\n" +
    "- Sensor capacitivo de solo: 0% = seco, 100% = saturado\n" +
    "- Capacidade de campo do substrato: ~75% (ponto seguro sem anaerobiose)\n" +
    "- Depleção máxima permitida (MAD): 60% da água disponível\n\n" +

    "⚠️ CONTEXTO CRÍTICO — MUDAS RECÉM-TRANSPLANTADAS:\n" +
    "As plantas foram transplantadas para os vasos de 11L em 30/04/2026.\n" +
    "Isso significa que nos primeiros 14 dias (até ~14/05/2026) elas estão em fase\n" +
    "de estabelecimento radicular — as raízes ainda NÃO preenchem o vaso.\n" +
    "Nesta fase:\n" +
    "  • Volume por vaso: 75–150mL (raízes pequenas não absorvem volume maior)\n" +
    "  • Frequência mínima entre regas: 48h (o substrato 11L seca lentamente sem raízes)\n" +
    "  • Alvo de umidade pós-rega: 65–70% (não saturar — risco de alagamento sem absorção)\n" +
    "  • Umidade mínima para regar: 40% (não deixar secar demais — estresse em muda)\n" +
    "  • NUNCA regar se a umidade média estiver acima de 65% nesta fase\n" +
    "  • Após 14 dias no vaso, a planta começa a colonizar o substrato e os volumes podem escalar\n\n" +

    "LIMITAÇÕES DO SISTEMA — leia com atenção antes de analisar:\n" +
    "- NÃO existe sensor de volume físico no reservatório. galaoMl é ESTIMATIVA calculada\n" +
    "  descontando volume bombeado. Reinicia em 7000mL no boot do ESP32.\n" +
    "- NUNCA use o termo 'sensor de volume'. A boia é o único sensor físico de nível.\n" +
    "- Se alertaBoia=1 e galaoMl alto: situação normal pós-reboot. Confie na boia.\n" +
    "- diasCiclo=0 é normal antes do cultivador iniciar o ciclo. Não é anomalia.\n" +
    "- Umidade 100%: pode indicar sensores acabaram de ser inseridos no substrato úmido.\n" +
    "  Não interpretar como saturação — aguardar estabilização nas próximas leituras.\n" +
    "- horasUltimaRega=999: sistema nunca regou nessa sessão, não é anomalia.\n" +
    "- Sensor capacitivo posicionado entre o anel e a borda do vaso — mede a frente\n" +
    "  de molhamento periférico. Umidade no centro do vaso pode ser menor.\n\n" +

    "DADOS ATUAIS DOS SENSORES:\n" +
    "- Umidade Vaso 1: " + d.umidade1 + "%\n" +
    "- Umidade Vaso 2: " + d.umidade2 + "%\n" +
    "- Média dos vasos: " + d.mediaUmidade + "%\n" +
    "- Diferença entre vasos: " + d.diferenca + "% | Desequilíbrio: " + deseqTexto + "\n" +
    "- Boia reservatório (0=ok, 1=vazio): " + d.alertaBoia + "\n" +
    "- Galão atual: " + galaoL + "L de 7L total\n" +
    "- Último volume bombeado por vaso: " + d.volumeBaseML + "mL\n" +
    "- Horas desde a última rega: " + d.horasUltimaRega + "h\n" +
    "- Luz: " + luzTexto + "\n" +
    climaTexto + "\n" +

    "CONTEXTO DA PLANTA:\n" +
    "- Fase atual (definida pelo cultivador): " + faseTexto + "\n" +
    "- Dias de ciclo (contados desde start_ciclo): " + d.diasCiclo + "\n" +
    "- Data do transplante para vaso 11L: 30/04/2026\n\n" +

    "TABELA DE DECISÃO BASE:\n" +
    "| Fase/Situação              | Dias vaso | Umid.mín | Alvo pós-rega | Vol/vaso   | Freq.mín |\n" +
    "| Muda pós-transplante       | 0–14      | 40%      | 65–70%        | 75–150mL   | 48h      |\n" +
    "| Vegetação estabelecida     | 15–28     | 35%      | 75%           | 200–300mL  | 48–72h   |\n" +
    "| Floração Inicial           | 29–49     | 40%      | 75%           | 500mL      | 24h      |\n" +
    "| Floração Plena             | 50–70     | 40%      | 75%           | 700–900mL  | 24h      |\n" +
    "| Floração Final             | 71+       | 35%      | 70%           | 500–700mL  | 24–48h   |\n\n" +

    "REGRAS DE BLOQUEIO (regar_agora = false OBRIGATÓRIO):\n" +
    "1. alertaBoia == 1: reservatório vazio, risco de queima da bomba\n" +
    "2. Fase muda (diasCiclo ≤ 14): mediaUmidade >= 65% — não saturar substrato sem raízes\n" +
    "3. Fase vegetação+: mediaUmidade >= 80% — saturação, risco de anaerobiose\n" +
    "4. horasUltimaRega < 48h quando diasCiclo ≤ 14 — frequência mínima pós-transplante\n" +
    "5. horasUltimaRega < frequencia_minima_da_fase nas demais fases\n" +
    "6. desequil == 1 E mediaUmidade > 50%: falha hidráulica, não agravar\n" +
    "   EXCEÇÃO: se ambos < 30%, regar mesmo com desequilíbrio\n" +
    "7. galaoMl < 500: preservar autonomia — só regar em emergência (< 30%)\n\n" +

    "REGRAS DE INTENSIFICAÇÃO (aumentar fator_rega):\n" +
    "- Temperatura do ar > 30°C: aumentar fator em 0.2 (NÃO aplicar em muda pós-transplante)\n" +
    "- VPD acima do ideal (Veg>1.2kPa, Flor>1.6kPa): aumentar fator em 0.2\n" +
    "- Floração plena com secagem < 12h de 75% para 40%: aumentar fator em 0.5\n\n" +

    "GESTÃO DO GALÃO:\n" +
    "- Galão > 3L: operar normalmente\n" +
    "- Galão 1–3L: reduzir volume_ml em 30% para preservar autonomia\n" +
    "- Galão < 1L: volume_ml máximo 100mL e alertar urgência de reabastecimento\n\n" +

    "ESTRESSE HÍDRICO CONTROLADO (só após estabelecimento — diasCiclo > 14):\n" +
    "- Final vegetação (dias 20–28): permita secagem até 30% para estimular raízes\n" +
    "- Final floração (dias 70+): permita secagem até 30% para concentrar canabinoides\n\n" +

    "DIAGNÓSTICO DE LONGO PRAZO:\n" +
    "- Desequilíbrio persistente entre vasos: provável obstrução em gotejador ou válvula assimétrica\n" +
    "- Umidade alta persistente sem rega recente: drenagem comprometida ou sensor com problema\n" +
    "- Risco de Botrytis: floração + luz desligada + umidade ar > 60% = alerta crítico de fungo\n" +
    "- Em mudas: umidade caindo muito rápido (< 12h) pode indicar substrato mal acomodado\n\n" +

    "Retorne APENAS JSON válido sem markdown, sem texto fora do JSON:\n" +
    "{\n" +
    "  \"regar_agora\": true,\n" +
    "  \"fator_rega\": 1.0,\n" +
    "  \"volume_ml\": 100,\n" +
    "  \"diagnostico\": \"frase curta de status em português\",\n" +
    "  \"motivo_decisao\": \"justificativa agronômica citando os dados que levaram à decisão\",\n" +
    "  \"alerta_longo_prazo\": \"vazio ou alerta de anomalia detectada no padrão dos dados\"\n" +
    "}";

  var options = {
    "method": "post",
    "contentType": "application/json",
    "payload": JSON.stringify({ "contents": [{ "parts": [{ "text": prompt }] }] }),
    "muteHttpExceptions": true
  };

  try {
    var res  = UrlFetchApp.fetch(GEMINI_URL, options);
    var code = res.getResponseCode();
    var text = res.getContentText();

    if (code === 200) {
      var json = JSON.parse(text);
      if (json.candidates && json.candidates.length > 0) {
        var raw   = json.candidates[0].content.parts[0].text;
        var match = raw.match(/\{[\s\S]*\}/);
        if (match) {
          var a = JSON.parse(match[0]);
          return {
            diagnostico:        a.diagnostico        || "Sem diagnóstico",
            fator_rega:         Math.max(0, Math.min(2, parseFloat(a.fator_rega) || 1.0)),
            volume_ml:          parseFloat(a.volume_ml) || 100,
            regar_agora:        a.regar_agora === true,
            motivo_decisao:     a.motivo_decisao     || "",
            alerta_longo_prazo: a.alerta_longo_prazo || ""
          };
        }
      }
    } else if (code === 429) {
      return _erroIA("Rate limit (429) — aguardando cota");
    }
    return _erroIA("Erro HTTP " + code);
  } catch (err) {
    return _erroIA("Erro: " + err.message);
  }
}

function _erroIA(msg) {
  return {
    diagnostico:        msg,
    fator_rega:         1.0,
    volume_ml:          0,
    regar_agora:        false,
    motivo_decisao:     "Falha na consulta à IA — mantendo estado seguro",
    alerta_longo_prazo: ""
  };
}

// ============================================================
// UTILITÁRIOS
// ============================================================
function obterOuCriarAba(ss, nome) {
  var aba = ss.getSheetByName(nome);
  if (!aba) { aba = ss.insertSheet(nome); configurarCabecalho(aba, nome); }
  return aba;
}

function configurarCabecalho(aba, nome) {
  if (nome === ABA_DADOS) {
    aba.getRange(1, 1, 1, 23).setValues([[
      "Timestamp", "Hora", "Umidade1 (%)", "Umidade2 (%)", "Média (%)",
      "Diferença (%)", "Desequilíbrio", "AlertaBoia",
      "Vol/Vaso (mL)", "Vol Total (mL)", "Galão (mL)",
      "Fase", "Dias Ciclo", "Luz",
      "Fator Rega", "Diagnóstico IA", "Motivo Decisão", "Alerta Longo Prazo",
      "Temp Ar (°C)", "Umidade Ar (%)", "Pressão (hPa)", "VPD (kPa)", "Temp Terra (°C)"
    ]]).setFontWeight("bold");
  } else if (nome === ABA_EVENTOS) {
    aba.getRange(1, 1, 1, 6).setValues([[
      "Timestamp", "Hora", "Evento", "Descrição", "Fase", "Dias Ciclo"
    ]]).setFontWeight("bold");
  }
}

function gravarErro(ss, dados, msg) {
  try {
    var aba     = obterOuCriarAba(ss, ABA_DADOS);
    var agora   = new Date();
    var horaFmt = pad2(agora.getHours()) + ":" + pad2(agora.getMinutes()) + ":" + pad2(agora.getSeconds());
    aba.insertRowBefore(2);
    aba.getRange(2, 1, 1, 18).setValues([[
      agora, horaFmt,
      dados ? (dados.umidade1 || "?") : "?",
      dados ? (dados.umidade2 || "?") : "?",
      "?", "?", "?", "?", "?", "?", "?",
      dados ? (dados.fasePlanta || "?") : "?",
      "?", "?", 1.0,
      "ERRO: " + msg.substring(0, 200), "", ""
    ]]);
  } catch(e) {
    Logger.log("gravarErro falhou: " + e.message);
  }
}

function responderTexto(t) {
  return ContentService.createTextOutput(t).setMimeType(ContentService.MimeType.TEXT);
}

function responderJSON(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

function pad2(n) { return n < 10 ? "0" + n : String(n); }

// ============================================================
// ANÁLISE VISUAL — Gemini Vision via foto do app
// ============================================================
function analisarImagem(ss, dados) {
  var imagemB64 = dados.imagem   || "";
  var mimeType  = dados.mimeType || "image/jpeg";
  var planta    = dados.planta   || 1;

  if (!imagemB64) return responderJSON({ erro: "Imagem não recebida" });

  var prompt =
    "Você é um especialista em cultivo indoor de Cannabis sativa. " +
    "Analise a foto desta planta e gere um relatório técnico. " +
    "Contexto: planta recém-transplantada para vaso 11L em 30/04/2026 — ainda em fase de muda. " +
    "Retorne APENAS JSON válido sem markdown:\n" +
    "{" +
    "\"fase_visual\": \"muda | vegetação | floração | desconhecido\"," +
    "\"altura_estimada_cm\": número ou null," +
    "\"numero_caules_principais\": número," +
    "\"numero_nos_visiveis\": número ou null," +
    "\"cor_folhas\": \"verde escuro | verde médio | verde claro | amarelado | outro\"," +
    "\"deficiencia_detectada\": \"nenhuma | nitrogênio | fósforo | potássio | magnésio | ferro | outro\"," +
    "\"deficiencia_descricao\": \"descrição breve ou vazio\"," +
    "\"pragas_ou_doencas\": true ou false," +
    "\"pragas_descricao\": \"descrição ou vazio\"," +
    "\"estado_geral\": \"ótimo | bom | atenção | crítico\"," +
    "\"resumo\": \"frase de 1-2 linhas sobre o estado atual\"," +
    "\"observacoes\": \"parágrafo detalhado com observações e recomendações\"" +
    "}";

  var payload = {
    "contents": [{
      "parts": [
        { "inline_data": { "mime_type": mimeType, "data": imagemB64 } },
        { "text": prompt }
      ]
    }]
  };

  var options = {
    "method": "post",
    "contentType": "application/json",
    "payload": JSON.stringify(payload),
    "muteHttpExceptions": true
  };

  try {
    var res  = UrlFetchApp.fetch(GEMINI_URL, options);
    var code = res.getResponseCode();
    var text = res.getContentText();

    if (code === 200) {
      var json = JSON.parse(text);
      if (json.candidates && json.candidates.length > 0) {
        var raw   = json.candidates[0].content.parts[0].text;
        var match = raw.match(/\{[\s\S]*\}/);
        if (match) {
          var resultado = JSON.parse(match[0]);
          var abaE = obterOuCriarAba(ss, ABA_EVENTOS);
          var agora = new Date();
          abaE.insertRowBefore(2);
          abaE.getRange(2, 1, 1, 6).setValues([[
            agora,
            pad2(agora.getHours()) + ":" + pad2(agora.getMinutes()),
            "analise_visual_P" + planta,
            JSON.stringify({
              ts:          pad2(agora.getDate()) + "/" + pad2(agora.getMonth()+1) + "/" + agora.getFullYear() + " " + pad2(agora.getHours()) + ":" + pad2(agora.getMinutes()),
              planta:      planta,
              estado:      resultado.estado_geral,
              fase:        resultado.fase_visual,
              altura:      resultado.altura_estimada_cm,
              caules:      resultado.numero_caules_principais,
              nos:         resultado.numero_nos_visiveis,
              cor:         resultado.cor_folhas,
              deficiencia: resultado.deficiencia_detectada,
              pragas:      resultado.pragas_ou_doencas,
              resumo:      resultado.resumo,
              obs:         resultado.observacoes,
            }),
            "vegetacao",
            0
          ]]);
          return responderJSON(resultado);
        }
      }
      return responderJSON({ erro: "Gemini não retornou JSON válido" });
    }
    return responderJSON({ erro: "Gemini HTTP " + code });
  } catch (err) {
    return responderJSON({ erro: err.message });
  }
}

// ============================================================
// doGet — site busca eventos da planilha
// ============================================================
function doGet(e) {
  var ss   = SpreadsheetApp.getActiveSpreadsheet();
  var abaE = ss.getSheetByName(ABA_EVENTOS);
  if (!abaE) return responderJSON([]);

  var tipo = e && e.parameter && e.parameter.tipo ? e.parameter.tipo : null;
  var rows = abaE.getDataRange().getValues();
  var resultado = [];

  for (var i = 1; i < rows.length; i++) {
    var r = rows[i];
    var tipoEvento = String(r[2] || "");
    if (!tipo || tipoEvento === tipo) {
      resultado.push({
        timestamp: r[0] ? r[0].toString() : "",
        hora:      r[1] || "",
        evento:    tipoEvento,
        descricao: r[3] || "",
        fase:      r[4] || "",
        diasCiclo: r[5] || 0
      });
    }
    if (resultado.length >= 50) break;
  }

  return ContentService
    .createTextOutput(JSON.stringify(resultado))
    .setMimeType(ContentService.MimeType.JSON);
}

// ============================================================
// TESTES MANUAIS — execute no editor do Apps Script
// ============================================================
function testarPayloadCompleto() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var dadosFake = {
    tipo: "dados", manual: true,
    umidade1: 55, umidade2: 58,
    alertaBoia: 0, volumeBaseML: 0, volumeTotalML: 0,
    fasePlanta: "vegetacao", diasCiclo: 1,
    galaoMl: 7000, desequilibrio: 0,
    luzAtiva: 1, horasUltimaRega: 999
  };
  try {
    var resultado = processarDados(ss, dadosFake);
    Logger.log("SUCESSO: " + resultado.getContent());
  } catch(e) {
    Logger.log("ERRO: " + e.message + " | linha: " + e.lineNumber);
  }
}

function testarGeminiDireto() {
  var r = consultarGemini({
    umidade1: 48, umidade2: 52, mediaUmidade: "50.0",
    alertaBoia: 0, volumeBaseML: 0, fasePlanta: "vegetacao",
    diasCiclo: 1, galaoMl: 7000, diferenca: 4, desequil: 0,
    luzAtiva: 1, horasUltimaRega: 999,
    tempAr: null, umidadeAr: null, vpd: null
  });
  Logger.log("Diagnóstico: "        + r.diagnostico);
  Logger.log("Fator rega: "         + r.fator_rega);
  Logger.log("Volume/vaso: "        + r.volume_ml + "mL");
  Logger.log("Regar agora: "        + r.regar_agora);
  Logger.log("Motivo: "             + r.motivo_decisao);
  Logger.log("Alerta longo prazo: " + r.alerta_longo_prazo);
}
