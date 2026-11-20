# Próxima tarefa

## Do agente

1. **Primeira execução** (Linux): rodar `ben10_poe` com o ELF do disco e
   ver até onde o boot chega. Registrar o primeiro ponto de parada num
   finding novo, sem mascarar travamentos (master prompt Fase 7: um
   problema de cada vez). Ao terminar, atualizar os marcos em
   `tools/native_progress.py` e rodar `--readme`.
2. Mais testes por instrução para as instruções do jogo ainda sem teste
   (KNOWN_ISSUES R13), começando por RSQRT.S, MADD.S/MSUB.S, C.*.S,
   LDL/LDR/SDL/SDR, QFSRV, PMFHL e o resto do VU0 macro.
3. Enviar os patches 0002-0005 para o PS2Recomp original.

## Do dono do projeto (quando quiser testar no Windows)

1. Clonar o repositório com os submódulos (ou, na cópia entregue, rodar
   `git submodule update --init` na raiz).
2. Seguir [01-building.md](01-building.md), parte Windows. Nada disso foi
   testado no Windows ainda: se algum passo falhar, guardar a mensagem de
   erro inteira.
