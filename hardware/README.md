# hardware/

Especificação completa em `docs/02-hardware.md`. Aqui ficam os arquivos.

---

## kicad/SGRW_Controller/

Projeto KiCad do controlador — esquemático e PCB.

| Arquivo | Conteúdo |
|---|---|
| `SGRW_Controller.kicad_sch` | Esquemático |
| `SGRW_Controller.kicad_pcb` | Layout da PCB |
| `SGRW_Controller.kicad_pro` | Projeto |
| `SGRW_Controller.pdf` | Esquemático exportado — abre sem KiCad |
| `.history/` | Histórico local do KiCad (ignorado no `.gitignore`) |

O sistema roda hoje em **protoboard**. Migrar para esta PCB é item de backlog
(`docs/09-roadmap.md`), mas o esquemático já reflete a intenção do circuito.

### Ao revisar a PCB, conferir

1. **Diodo 1N4007 em paralelo com cada bomba** — catodo (faixa) no positivo.
   Sem isso, o pico reverso do motor DC solda o contato do relé. **Já custou 2 canais.**
2. **Alimentação separada** para os sensores I2C: 3.3V vindo do ESP32, nunca de reguladora
   de protoboard (que entrega 3,6–3,8V e já matou 2 BME280).
3. **Wire2 dedicado** (GPIO 16/17) para o BME280, isolado do barramento do display.
4. **GPIO14 não usar** para interrupção de fluxo — conflito com SPI flash. Usar GPIO13.
5. Relés: canal DC precisa de **relé mecânico ou SSR DC**. SSR AC (triac) não chaveia DC.

---

## imagens/

| Arquivo | Uso |
|---|---|
| `JPEG Folha.jpg` | Origem do bitmap da folha exibido no display OLED (`epd_bitmap_Folha[]`, PROGMEM, ocupa os primeiros 58 px à esquerda) |
| `pngegg.png` | Arte auxiliar |
