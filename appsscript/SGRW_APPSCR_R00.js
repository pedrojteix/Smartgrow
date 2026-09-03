// ============================================================
// SMARTGROW — Apps Script R07
// Abas: DADOS (23 colunas A→W) e EVENTOS (6 colunas)
// + Bloqueio remoto da bomba via site (aba CONFIG)
// + Limite de regas diarias retornado ao ESP32
// + Evento alerta_rega registrado nos eventos
// + Suporte a rega remota via site (aba CONFIG)
// ============================================================

const GEMINI_API_KEY = "AIzaSyDj9usPFgeSXayaHzh3xRGNOcAqcbb-jgc";
const GEMINI_URL     = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + GEMINI_API_KEY;

const ABA_DADOS   = "DADOS";
const ABA_EVENTOS = "EVENTOS";
const ABA_CONFIG  = "CONFIG";
const ABA_REGAS   = "REGAS";

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
    if (dados.tipo === "bloqueio_bomba") return processarBloqueio(ss, dados);
    if (dados.tipo === "rega")           return processarRega(ss, dados);
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

  // Busca notas recentes do cultivador (ultimas 3 das ultimas 48h)
  var notasCultivador = "";
  try {
    var abaEvtNotas = ss.getSheetByName(ABA_EVENTOS);
    if (abaEvtNotas) {
      var rowsNotas = abaEvtNotas.getDataRange().getValues();
      var limite48h = new Date(Date.now() - 48 * 3600 * 1000);
      var notasEncontradas = [];
      for (var ni = 1; ni < rowsNotas.length; ni++) {
        if (String(rowsNotas[ni][2] || "") === "nota_cultivador") {
          var tsNota = rowsNotas[ni][0];
          if (tsNota instanceof Date && tsNota >= limite48h) {
            notasEncontradas.push("[" + rowsNotas[ni][1] + "] " + String(rowsNotas[ni][3] || ""));
            if (notasEncontradas.length >= 3) break;
          }
        }
      }
      if (notasEncontradas.length > 0) notasCultivador = notasEncontradas.join("\n");
    }
  } catch(eNotas) { Logger.log("Erro notas: " + eNotas.message); }

  // Calcula dias reais no grow a partir da data inserida no site
  var diasReais = calcularDiasNoGrow(ss, diasCiclo);
  Logger.log("[IA] diasCiclo ESP32=" + diasCiclo + " | diasReais=" + diasReais);

  var resultadoIA = consultarGemini({
    umidade1: umidade1, umidade2: umidade2, mediaUmidade: mediaUmidade,
    alertaBoia: alertaBoia, volumeBaseML: volumeBaseML,
    fasePlanta: fasePlanta, diasCiclo: diasReais, galaoMl: galaoMl,
    diferenca: diferenca, desequil: desequil, luzAtiva: luzAtiva,
    horasUltimaRega: horasUltimaRega,
    tempAr: tempAr, umidadeAr: umidadeAr, pressaoHpa: pressaoHpa,
    vpd: vpd, tempTerra: tempTerra,
    notasCultivador: notasCultivador
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
    diasReais,
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

  // Le aba CONFIG: rega_remota_ml e bomba_bloqueada
  var regaRemotaMl   = 0;
  var bombaBloqueada = -1; // -1 = sem mudanca, 0 = desbloquear, 1 = bloquear
  try {
    var cfgAba = ss.getSheetByName(ABA_CONFIG);
    if (cfgAba) {
      var cfgDados = cfgAba.getDataRange().getValues();
      for (var ci = 1; ci < cfgDados.length; ci++) {
        var chave = cfgDados[ci][0];
        var valor = cfgDados[ci][1];

        if (chave === "rega_remota_ml" && parseFloat(valor) > 0) {
          regaRemotaMl = parseFloat(valor);
          cfgAba.getRange(ci+1, 2).setValue(0);
          Logger.log("[CONFIG] rega_remota_ml: " + regaRemotaMl + "mL");
        }

        if (chave === "bomba_bloqueada" && valor !== "" && valor !== -1) {
          bombaBloqueada = parseInt(valor);
          cfgAba.getRange(ci+1, 2).setValue(-1); // limpa apos enviar
          Logger.log("[CONFIG] bomba_bloqueada: " + bombaBloqueada);
        }
      }
    }
  } catch(e) { Logger.log("Erro ao ler CONFIG: " + e.message); }

  return responderJSON({
    fator_rega:       resultadoIA.fator_rega,
    volume_ml:        resultadoIA.volume_ml,
    regar_agora:      resultadoIA.regar_agora,
    rega_remota_ml:   regaRemotaMl,
    bomba_bloqueada:  bombaBloqueada
  });
}

// ============================================================
// BLOQUEIO REMOTO DA BOMBA — recebe comando do site
// ============================================================
function processarBloqueio(ss, dados) {
  var valor = parseInt(dados.bomba_bloqueada);
  if (valor !== 0 && valor !== 1) {
    return responderJSON({ status: "erro", msg: "Valor invalido: " + valor });
  }

  var cfg = ss.getSheetByName(ABA_CONFIG);
  if (!cfg) {
    cfg = ss.insertSheet(ABA_CONFIG);
    cfg.getRange(1,1,1,2).setValues([["chave","valor"]]).setFontWeight("bold");
  }

  // Busca linha existente ou cria nova
  var cfgDados = cfg.getDataRange().getValues();
  var linhaExistente = -1;
  for (var i = 1; i < cfgDados.length; i++) {
    if (cfgDados[i][0] === "bomba_bloqueada") { linhaExistente = i + 1; break; }
  }
  if (linhaExistente > 0) {
    cfg.getRange(linhaExistente, 2).setValue(valor);
  } else {
    cfg.appendRow(["bomba_bloqueada", valor]);
  }

  // Registra evento
  var abaE = obterOuCriarAba(ss, ABA_EVENTOS);
  var agora = new Date();
  abaE.insertRowBefore(2);
  abaE.getRange(2,1,1,6).setValues([[
    agora,
    pad2(agora.getHours())+":"+pad2(agora.getMinutes()),
    "bomba_bloqueada",
    valor === 1 ? "Bomba BLOQUEADA remotamente pelo site" : "Bomba DESBLOQUEADA remotamente pelo site",
    dados.fasePlanta || "vegetacao",
    0
  ]]);

  Logger.log("[BLOQUEIO] bomba_bloqueada=" + valor);
  return responderJSON({
    status: "ok",
    bomba_bloqueada: valor,
    msg: valor === 1 ? "Bomba bloqueada -- ESP32 aplicara no proximo ciclo (15min)" : "Bomba desbloqueada -- ESP32 aplicara no proximo ciclo (15min)"
  });
}

// ============================================================
// REGA REMOTA — recebe comando do site e grava na aba CONFIG
// ============================================================
function processarRegaRemota(ss, dados) {
  var volume = parseFloat(dados.volume_ml) || 0;
  if (volume <= 0 || volume > 7000) {
    return responderJSON({ status: "erro", msg: "Volume invalido: " + volume });
  }

  var cfg = ss.getSheetByName(ABA_CONFIG);
  if (!cfg) {
    cfg = ss.insertSheet(ABA_CONFIG);
    cfg.getRange(1,1,1,2).setValues([["chave","valor"]]).setFontWeight("bold");
  }

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
// LOG DE REGAS — registra cada rega real (por vaso) na aba REGAS
// Recebe do ESP32 (store-and-forward): trigger, vaso1_ml, vaso2_ml, etc.
// trigger: auto | remota | teste | manual
// ============================================================
function processarRega(ss, dados) {
  var aba   = obterOuCriarAba(ss, ABA_REGAS);
  var agora = new Date();
  var hora  = (dados.hora   != null) ? dados.hora   : agora.getHours();
  var min   = (dados.minuto != null) ? dados.minuto : agora.getMinutes();

  var vaso1 = parseInt(dados.vaso1_ml) || 0;
  var vaso2 = parseInt(dados.vaso2_ml) || 0;
  var total = (dados.total_ml != null) ? parseInt(dados.total_ml) : (vaso1 + vaso2);

  aba.insertRowBefore(2);
  aba.getRange(2, 1, 1, 10).setValues([[
    agora,
    pad2(hora) + ":" + pad2(min),
    dados.trigger    || "auto",
    vaso1,
    vaso2,
    total,
    parseInt(dados.duracao_s) || 0,
    (dados.galaoMl != null) ? parseInt(dados.galaoMl) : "",
    dados.fasePlanta || "vegetacao",
    dados.diasCiclo  || 0
  ]]);
  // Força formato DateTime na célula de Timestamp (evita que Sheets salve como Time-only)
  aba.getRange(2, 1).setNumberFormat("dd/MM/yyyy HH:mm:ss");

  Logger.log("[REGA] " + (dados.trigger || "auto") +
             " V1=" + vaso1 + " V2=" + vaso2 + " total=" + total + "mL");
  return responderJSON({ status: "ok" });
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
// PROMPT DA IA
// ============================================================
function calcularDiasNoGrow(ss, diasCicloESP32) {
  try {
    var abaE = ss.getSheetByName(ABA_EVENTOS);
    if (!abaE) return diasCicloESP32;
    var rows = abaE.getDataRange().getValues();
    var dataEntrada = null;
    for (var i = 1; i < rows.length; i++) {
      var tipo = String(rows[i][2] || "");
      if (tipo === "dados_ciclo_P1" || tipo === "dados_ciclo_P2") {
        try {
          var obj = JSON.parse(String(rows[i][3] || "{}"));
          if (obj.smartgrow) {
            var p = obj.smartgrow.split("-");
            if (p.length === 3) {
              var d = new Date(parseInt(p[0]), parseInt(p[1])-1, parseInt(p[2]));
              if (!dataEntrada || d > dataEntrada) dataEntrada = d;
            }
          }
        } catch(e) {}
      }
    }
    if (dataEntrada) {
      var diff = Math.floor((new Date() - dataEntrada) / 86400000);
      Logger.log("[CICLO] dias no grow (site): " + diff);
      return Math.max(0, diff);
    }
  } catch(e) { Logger.log("[CICLO] erro: " + e.message); }
  return diasCicloESP32;
}

function consultarGemini(d) {
  var faseTexto = {
    "germinacao": "Germinacao/Muda",
    "vegetacao":  "Vegetacao",
    "floracao":   "Floracao"
  }[d.fasePlanta] || d.fasePlanta;

  var galaoL     = (d.galaoMl / 1000).toFixed(1);
  var deseqTexto = d.desequil === 1 ? "SIM (diferenca: " + d.diferenca + "%)" : "NAO";
  var luzTexto   = d.luzAtiva === 1 ? "LIGADA" : "DESLIGADA";

  var climaTexto = "";
  if (d.tempAr !== null && d.umidadeAr !== null) {
    climaTexto =
      "- Temperatura do ar: " + d.tempAr + " C\n" +
      "- Umidade relativa do ar: " + d.umidadeAr + "%\n" +
      (d.vpd        !== null ? "- VPD: "            + d.vpd        + " kPa\n" : "") +
      (d.pressaoHpa !== null ? "- Pressao atm.: "   + d.pressaoHpa + " hPa\n" : "") +
      (d.tempTerra  !== null ? "- Temp. da terra: " + d.tempTerra  + " C\n"  : "");
  } else {
    climaTexto = "- Dados climaticos: nao disponiveis\n";
  }

  var prompt =
    "Voce e uma IA agronomica especializada em Cannabis sativa indoor.\n" +
    "Sua funcao e decidir irrigacao a cada 15 minutos para um sistema com as seguintes caracteristicas FIXAS:\n" +
    "- 2 vasos de fabric pot 11L com substrato 1:1:1 (perlita/humus/fibra de coco)\n" +
    "- 1 bomba unica alimenta os 2 vasos simultaneamente via aneis de irrigacao\n" +
    "- Os aneis tem furos voltados para FORA do anel (irrigacao periferica)\n" +
    "- O campo 'volume_ml' que voce retorna e POR VASO - o sistema dobra automaticamente\n" +
    "- Sensor capacitivo de solo: 0% = seco, 100% = saturado\n" +
    "- Capacidade de campo do substrato: ~75%\n" +
    "- Deplecao maxima permitida (MAD): 60% da agua disponivel\n\n" +

    "LIMITACOES DO SISTEMA:\n" +
    "- NÃO existe sensor de volume fisico no reservatorio. galaoMl e ESTIMATIVA.\n" +
    "- diasCiclo e calculado automaticamente pela data de entrada no SmartGrow informada no site.\n" +
    "- Umidade 100%: pode indicar sensores recém inseridos no substrato umido.\n" +
    "- horasUltimaRega=999: sistema nunca regou nessa sessao.\n" +
    "- Sensor capacitivo mede frente de molhamento periferico.\n\n" +

    "DADOS ATUAIS:\n" +
    "- Umidade Vaso 1: " + d.umidade1 + "%\n" +
    "- Umidade Vaso 2: " + d.umidade2 + "%\n" +
    "- Media dos vasos: " + d.mediaUmidade + "%\n" +
    "- Diferenca entre vasos: " + d.diferenca + "% | Desequilibrio: " + deseqTexto + "\n" +
    "- Boia reservatorio (0=ok, 1=vazio): " + d.alertaBoia + "\n" +
    "- Galao atual: " + galaoL + "L de 7L total\n" +
    "- Ultimo volume bombeado por vaso: " + d.volumeBaseML + "mL\n" +
    "- Horas desde a ultima rega: " + d.horasUltimaRega + "h\n" +
    "- Luz: " + luzTexto + "\n" +
    climaTexto + "\n" +

    (d.notasCultivador ? "OBSERVACOES DO CULTIVADOR (ultimas 48h -- alta prioridade):\n" + d.notasCultivador + "\n\n" : "") +
    "CONTEXTO DA PLANTA:\n" +
    "- Fase atual: " + faseTexto + "\n" +
    "- Dias de ciclo: " + d.diasCiclo + "\n" +
    "- Data do transplante para vaso 11L: 30/04/2026\n\n" +

    "TABELA DE DECISAO BASE:\n" +
    "| Fase/Situacao          | Dias vaso | Umid.min | Alvo pos-rega | Vol/vaso  | Freq.min |\n" +
    "| Muda pos-transplante   | 0-14      | 40%      | 65-70%        | 75-150mL  | 48h      |\n" +
    "| Vegetacao estabelecida | 15-28     | 35%      | 75%           | 200-300mL | 48-72h   |\n" +
    "| Floracao Inicial       | 29-49     | 40%      | 75%           | 500mL     | 24h      |\n" +
    "| Floracao Plena         | 50-70     | 40%      | 75%           | 700-900mL | 24h      |\n" +
    "| Floracao Final         | 71+       | 35%      | 70%           | 500-700mL | 24-48h   |\n\n" +

    "REGRAS DE BLOQUEIO (regar_agora = false OBRIGATORIO):\n" +
    "1. alertaBoia == 1\n" +
    "2. Fase muda (diasCiclo <= 14): mediaUmidade >= 65%\n" +
    "3. Fase vegetacao+: mediaUmidade >= 80%\n" +
    "4. horasUltimaRega < 48h quando diasCiclo <= 14\n" +
    "5. horasUltimaRega < frequencia_minima_da_fase\n" +
    "6. desequil == 1 E mediaUmidade > 50% (exceto se ambos < 30%)\n" +
    "7. galaoMl < 500 (exceto emergencia < 30%)\n\n" +

    "GESTAO DO GALAO:\n" +
    "- Galao > 3L: operar normalmente\n" +
    "- Galao 1-3L: reduzir volume_ml em 30%\n" +
    "- Galao < 1L: volume_ml maximo 100mL e alertar urgencia\n\n" +

    "Retorne APENAS JSON valido sem markdown:\n" +
    "{\n" +
    "  \"regar_agora\": true,\n" +
    "  \"fator_rega\": 1.0,\n" +
    "  \"volume_ml\": 100,\n" +
    "  \"diagnostico\": \"frase curta de status em portugues\",\n" +
    "  \"motivo_decisao\": \"justificativa agronomica\",\n" +
    "  \"alerta_longo_prazo\": \"vazio ou alerta de anomalia\"\n" +
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
            diagnostico:        a.diagnostico        || "Sem diagnostico",
            fator_rega:         Math.max(0, Math.min(2, parseFloat(a.fator_rega) || 1.0)),
            volume_ml:          parseFloat(a.volume_ml) || 100,
            regar_agora:        a.regar_agora === true,
            motivo_decisao:     a.motivo_decisao     || "",
            alerta_longo_prazo: a.alerta_longo_prazo || ""
          };
        }
      }
    } else if (code === 429) {
      return _erroIA("Rate limit (429)");
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
    motivo_decisao:     "Falha na consulta a IA -- mantendo estado seguro",
    alerta_longo_prazo: ""
  };
}

// ============================================================
// UTILITARIOS
// ============================================================
function obterOuCriarAba(ss, nome) {
  var aba = ss.getSheetByName(nome);
  if (!aba) { aba = ss.insertSheet(nome); configurarCabecalho(aba, nome); }
  return aba;
}

function configurarCabecalho(aba, nome) {
  if (nome === ABA_DADOS) {
    aba.getRange(1, 1, 1, 23).setValues([[
      "Timestamp", "Hora", "Umidade1 (%)", "Umidade2 (%)", "Media (%)",
      "Diferenca (%)", "Desequilibrio", "AlertaBoia",
      "Vol/Vaso (mL)", "Vol Total (mL)", "Galao (mL)",
      "Fase", "Dias Ciclo", "Luz",
      "Fator Rega", "Diagnostico IA", "Motivo Decisao", "Alerta Longo Prazo",
      "Temp Ar (C)", "Umidade Ar (%)", "Pressao (hPa)", "VPD (kPa)", "Temp Terra (C)"
    ]]).setFontWeight("bold");
  } else if (nome === ABA_EVENTOS) {
    aba.getRange(1, 1, 1, 6).setValues([[
      "Timestamp", "Hora", "Evento", "Descricao", "Fase", "Dias Ciclo"
    ]]).setFontWeight("bold");
  } else if (nome === ABA_REGAS) {
    aba.getRange(1, 1, 1, 10).setValues([[
      "Timestamp", "Hora", "Trigger", "Vaso 1 (mL)", "Vaso 2 (mL)",
      "Total (mL)", "Duracao (s)", "Galao (mL)", "Fase", "Dias Ciclo"
    ]]).setFontWeight("bold");
    // Garante que coluna Timestamp seja DateTime (não Time) para gviz retornar data completa
    aba.getRange("A2:A").setNumberFormat("dd/MM/yyyy HH:mm:ss");
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
// ANALISE VISUAL
// ============================================================
function analisarImagem(ss, dados) {
  var imagemB64 = dados.imagem   || "";
  var mimeType  = dados.mimeType || "image/jpeg";
  var planta    = dados.planta   || 1;

  if (!imagemB64) return responderJSON({ erro: "Imagem nao recebida" });

  var prompt =
    "Voce e um especialista em cultivo indoor de Cannabis sativa. " +
    "Analise a foto desta planta e gere um relatorio tecnico. " +
    "Retorne APENAS JSON valido sem markdown:\n" +
    "{" +
    "\"fase_visual\": \"muda | vegetacao | floracao | desconhecido\"," +
    "\"altura_estimada_cm\": numero ou null," +
    "\"numero_caules_principais\": numero," +
    "\"numero_nos_visiveis\": numero ou null," +
    "\"cor_folhas\": \"verde escuro | verde medio | verde claro | amarelado | outro\"," +
    "\"deficiencia_detectada\": \"nenhuma | nitrogenio | fosforo | potassio | magnesio | ferro | outro\"," +
    "\"deficiencia_descricao\": \"descricao breve ou vazio\"," +
    "\"pragas_ou_doencas\": true ou false," +
    "\"pragas_descricao\": \"descricao ou vazio\"," +
    "\"estado_geral\": \"otimo | bom | atencao | critico\"," +
    "\"resumo\": \"frase de 1-2 linhas sobre o estado atual\"," +
    "\"observacoes\": \"paragrafo detalhado com observacoes e recomendacoes\"" +
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
      return responderJSON({ erro: "Gemini nao retornou JSON valido" });
    }
    return responderJSON({ erro: "Gemini HTTP " + code });
  } catch (err) {
    return responderJSON({ erro: err.message });
  }
}

// ============================================================
// doGet
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
// TESTES MANUAIS
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

function testarBloqueio() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var r = processarBloqueio(ss, { bomba_bloqueada: 1 });
  Logger.log("Bloqueio: " + r.getContent());
}
