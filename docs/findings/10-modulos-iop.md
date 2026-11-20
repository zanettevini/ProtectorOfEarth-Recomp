# 10: módulos do IOP (IRX) e o `IOPRP300.IMG`

## 1. A pergunta

Quais módulos o jogo carrega no IOP? Isso decide o tamanho do trabalho de
serviços nativos (D2 em [DECISIONS](../DECISIONS.md)).

## 2. Como foi medido

Nome e versão de cada IRX tirados da seção `.iopmod` (campo de versão em
+0x18, nome em +0x1A), SHA-1 de cada arquivo, e busca do nome de cada
módulo dentro do `SLUS_216.61`.

## 3. Os 15 módulos do disco

| Arquivo | Nome no módulo | Versão | Citado pelo ELF? | O que é |
|---|---|---|---|---|
| `SIO2MAN.IRX` | sio2man | 3.01 | sim | barramento SIO2 (base de controle e memory card) |
| `MTAPMAN.IRX` | multitap_manager | 3.10 | sim | multitap |
| `PADMAN.IRX` | padman | 4.22 | sim | controles |
| `MCMAN.IRX` | mcman | 2.30 | sim | memory card (baixo nível) |
| `MCSERV.IRX` | mcserv | 2.10 | sim | memory card (servidor RPC) |
| `LIBSD.IRX` | Sound_Device_Library | 3.03 | sim | acesso ao SPU2 |
| `CDARB.IRX` | CD_Arbiter | 2.03 | sim | **não é da Sony pelo nome**: arbitra acesso ao disco (provavelmente entre streaming de áudio e carregamento) |
| `ALASD.IRX` | Argonaut_Audio_Driver | 4.06 | sim | **driver de áudio de terceiros** ("Argonaut") |
| `SN_ALASD.IRX` | Argonaut_Audio_Driver | 4.06 | não | mesmo tamanho do `ALASD.IRX`, conteúdo diferente (outro build) |
| `SN_CDARB.IRX` | CD_Arbiter | 2.03 | não | **idêntico** ao `CDARB.IRX` (mesmo SHA-1) |
| `CDVDSTM.IRX` | cdvd_st_driver | 2.02 | não | streaming de disco da Sony |
| `SCREAM.IRX` | Scream_Library | 2.00 | não | biblioteca de som da Sony |
| `STREAM.IRX` | multi_streamer | 1.02 | não | streaming de áudio ADPCM |
| `USBD.IRX` | USB_driver | 2.04 | não | USB |
| `USBKB.IRX` | USB_Keyboard_Driver | 2.02 | não | teclado USB |

"Citado pelo ELF" = o nome do arquivo aparece como string no executável
(a lista fica junta, perto de `0x0013BD90` no arquivo). Os não citados
podem ser sobras no disco ou ser carregados por outro caminho; confirmar no
Ghidra.

Além disso: `IOPRP300.IMG` (275 KB) é uma imagem de substituição de módulos
do IOP. O jogo reinicia o IOP com ela no boot (string
`cdrom0:\IOPRP300.IMG`). Com serviços nativos ela não precisa ser lida; com
o `ps2xIOP` (execução dos IRX) ela importa.

## 4. O que isso significa

- **Superfície pequena**: 8 módulos citados. Cinco são bibliotecas padrão
  bem documentadas (pad, multitap, memory card, SIO2, libsd).
- Os dois difíceis são os **próprios do jogo/motor**: `ALASD`
  (driver de áudio) e `CDARB` (arbitragem de disco). Para eles não há
  documentação: o protocolo de RPC vai ter que sair do lado do EE (as
  chamadas `sceSifCallRpc` do jogo) e, se preciso, do disassembly do IRX.
- Para serviços nativos, o trabalho é: arquivos do disco (`GAME.DIR` /
  `GAME.WAD`), controle, memory card, e o protocolo do `ALASD`/`CDARB`
  para áudio.

## 5. Em aberto

- Identificar os números de servidor RPC (SIF) que o EE chama e ligar cada
  um a um módulo.
- Confirmar que `SCREAM`, `STREAM`, `CDVDSTM`, `USBD`, `USBKB` nunca são
  carregados.
- Copiar `IOPRP300.IMG` e `GAME.DIR` na próxima coleta (o script já foi
  atualizado para isso).
