# Contribuindo com o Protector of Earth Recomp

Obrigado por ajudar. Código, engenharia reversa, testes, relatórios de bug,
correções e arte são bem-vindos, e todo colaborador aparece em
[CREDITS.md](CREDITS.md).

## Usando um agente de IA?

A maior parte deste projeto é escrita com ajuda de IA. Seu agente deve seguir
o **[AGENTS.md](AGENTS.md)**: a maioria lê o arquivo da raiz sozinha; senão,
cole-o nas instruções do agente. As regras completas estão lá. A versão curta
está abaixo.

## As regras, curtas

1. **Nenhum dado do jogo no repositório.** Sem imagens de disco, o ELF do
   jogo, módulos IRX, BIOS, nada de `game/` ou `generated/` (o C++ gerado é o
   código do jogo), texturas, áudio ou textos do jogo. Descreva, não cole.
2. **Escreva o que aprender.** Se a mudança exigiu entender uma parte do jogo
   ou do PS2, adicione um finding em [docs/findings/](docs/findings/README.md)
   (um arquivo novo numerado, ou uma seção num existente) e uma linha na
   tabela. Caminhos errados também entram.
3. **Comente bastante**, explicando o *porquê*. Todo arquivo novo tem
   cabeçalho; toda entrada do `ben10_poe.toml` tem um comentário "por que +
   como achamos".
4. **Confira no Ghidra** as fronteiras de uma função antes de mexer nela na
   config.
5. **Correções do PS2Recomp viram patch** em `patches/ps2recomp/`, com
   cabeçalho. Nunca edite o submódulo nem `generated/` como se fosse código
   nosso.
6. **Teste com referência.** Instrução nova ou corrigida ganha um caso em
   `tests/`, com o valor esperado citando a fonte (PCSX2 ou manual). Rode
   `ctest` antes do commit. Nunca mude um valor esperado para o teste passar.
7. **Sem emulação da CPU no executável final.** Interpretador só como
   oráculo de teste (as exceções temporárias estão em
   docs/KNOWN_ISSUES.md, T1/T2).
8. **Mantenha o original como era por padrão.** Comportamento novo fica atrás
   de uma opção.
9. **Nunca arrisque os saves dos jogadores.** Copie, não mova nem apague.
10. **Voz neutra** em docs, comentários e commits. Nome oficial desconhecido:
    **(nome a definir)**.

## Pull requests

- Um assunto por pull request.
- Diga o que mudou, por quê, **como testou e o que não testou** (Windows?
  Linux? comparado com o PCSX2?), e aponte o finding.
- Se um agente de IA ajudou, diga. Você é responsável por ter rodado a
  mudança.
- Contribuições são GPL-3.0 como o projeto. Adicione-se ao
  [CREDITS.md](CREDITS.md) com o que você fez.

Compilar: [docs/01-building.md](docs/01-building.md). Como tudo se encaixa:
[docs/02-how-the-port-works.md](docs/02-how-the-port-works.md).
