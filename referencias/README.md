# referencias/

Material de apoio. Não é código, mas é o que fundamenta as decisões agronômicas e técnicas.

---

## biblioteca-cientifica/

8 papers sobre cultivo de Cannabis. Base para qualquer mudança de parâmetro agronômico
e boa matéria-prima para o prompt da IA no 2.0.

| Arquivo | Tema |
|---|---|
| `Cannabis sativa L. Crop Management and Abiotic Factors That...` | Manejo e fatores abióticos |
| `Effect of Light Intensity and Two Different Nutrient Solutions` | Intensidade luminosa × nutrição |
| `Evaluation of substrates in the germination... up to the V3 stage` | Substratos na germinação |
| `High light intensity improves yield of specialized metabolites...` | Alta intensidade × metabólitos |
| `Integrating Hydraulic Properties into Irrigation Management of...` | Hidráulica do substrato |
| `Optimizing simplified growing media to enhance cannabis cultivation` | Otimização de substrato |
| `Photoperiodic_Response_of_In_Vitro_Cannabis_sativa` | Fotoperíodo |
| `Subsurface drip irrigation reduces weed` | Gotejamento subsuperficial |

---

## relatorios/

| Arquivo | Data | Conteúdo |
|---|---|---|
| `SGRW_DC_CAL_01.docx` | 23/04/2026 | **Relatório de calibração.** Fator K do sensor de fluxo, efeito sifão, desequilíbrio hidráulico, proposta das válvulas de retenção. Fonte primária dos números de hidráulica |
| `REL_TESTE_HID&LUZ_24042026.docx` / `.pdf` | 24/04/2026 | Testes hidráulicos e **mapeamento de PPFD por distância** da quantum board |
| `BK_Claude_08-05-26.docx` | 08/05/2026 | **Documento de handoff completo** da v1. Primeiro esforço sistemático de documentação — é o antepassado direto desta pasta |
| `BackUp R04.pdf` | — | Snapshot do firmware R04 |
| `SGRW_MODELO_NEGOCIOS.pdf` | — | Modelo de negócios. Indica intenção de transformar o SmartGrow em produto — por isso o ADR-03 (N zonas) e o `device_id` no contrato de API |

> ⚠️ O `BK_Claude_08-05-26.docx` descreve a lógica de relé como `RELE_ON = LOW`.
> Isso valia até o R08. **Do R09 em diante é `RELE_ON = HIGH`.**

---

## api/

| Arquivo | Conteúdo |
|---|---|
| `API R00.pdf` | Documentação de API do projeto |
| `API Passower.pdf` | Credenciais/acesso |
