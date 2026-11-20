# 03: o jogo, versões e o que já se sabe

## 1. A pergunta

Qual versão do jogo usar, e o que já se sabe dos arquivos dele?

## 2. Como foi estudado

Bases públicas (PSXDataCenter, Wikipedia, TCRF). **O disco ainda não foi
analisado.** Nada aqui foi confirmado no executável.

## 3. Versões de PS2

| Serial | Região | Observação |
|---|---|---|
| SLUS-21661 | NTSC-U (EUA) | lançado em outubro de 2007 |
| SLES-54951 | PAL (Europa) | |
| SLES-54952 | PAL (Europa) | listado pelo PCSX2 Wiki como versão italiana/espanhola; o Redump lista uma versão "EnFrDeEsIt" |

Recomendação: **SLUS-21661**. Motivos: NTSC a 60 Hz (PAL costuma ser 50 Hz),
é a versão citada com códigos de trapaça/debug no TCRF, e os outros ports
de referência também usam versões SLUS. O nome provável do ELF é
`SLUS_216.61` (padrão dos discos americanos), **a confirmar** no
`SYSTEM.CNF` do disco.

Uma referência secundária cita `SLUS-21699` para o jogo; o PSXDataCenter e
anúncios de venda usam `SLUS-21661`. Tratar 21661 como correto até ver o
disco.

## 4. O que se sabe do jogo

- Desenvolvedora: High Voltage Software; publicadora: D3 Publisher.
- Motor: **Quantum3**, da própria High Voltage. Não há documentação pública
  dos formatos dele.
- As versões de PS2, Wii e PSP são praticamente iguais e foram feitas pela
  mesma equipe. Isso pode ajudar: símbolos ou strings da versão de Wii/PSP
  podem dar nomes a funções da versão de PS2 (a investigar, sem copiar nada
  para o repositório).
- Cooperativo local para 2 jogadores.
- Os arquivos do jogo ficam num pacote chamado **`game.dir`**. Segundo o
  TCRF, existe dentro dele um arquivo de texto com opções de debug (ON/OFF):
  trapaças de debug, texto de debug, modo de voo livre, seleção de fase,
  seleção de fase de demonstração, som de debug. Ligar exige reimportar o
  arquivo no `game.dir` e gerar outro disco.
- A seleção de fase de debug mostra versões inacabadas de algumas fases.

Por que isso importa para o port:

- O seletor de fase e o modo de voo seriam ferramentas de teste valiosas
  (chegar direto a uma fase, como o `--load_save` do MoR). Como **nunca
  modificamos o jogo original**, o caminho certo é achar a função que lê
  essas opções e responder a ela por um override, atrás de uma opção de
  desenvolvedor.
- O formato do `game.dir` precisa ser entendido para a Fase 4 (acesso a
  arquivos).

## 5. Confirmado no disco ([09](09-executavel.md))

- Serial SLUS-21661, ELF `SLUS_216.61`, revisão `VER = 1.01`, NTSC.
- Os arquivos não ficam só no `game.dir`: o disco tem `GAME.DIR` (30 KB,
  provavelmente o índice) e `GAME.WAD` (2,46 GB, os dados).
- As opções de debug citadas pelo TCRF existem como strings no executável.
- O código é compartilhado com a versão de Wii (string `ass_Wii...`).

## 6. Em aberto

- Comparar o SHA-1 do ISO com o Redump.
- Listar os IRX do disco: quais são da Sony (padrão) e quais são próprios.
- Ver se existe código carregado em tempo de execução (overlays) além do
  ELF principal.
- Ver se o ELF tem símbolos ou seção `.mdebug` (a extensão do Ghidra
  recupera tipos e funções quando tem).
- Identificar middleware (vídeo, áudio) pelo nome dos módulos e strings.
- Formato do `GAME.DIR` / `GAME.WAD`.
