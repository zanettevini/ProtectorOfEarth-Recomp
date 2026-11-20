# Scripts do Ghidra

| Arquivo | Origem | Para que serve |
|---|---|---|
| `ApplyScriptApiNames.java` | deste projeto | cria e nomeia as funções da tabela da API de script do motor (findings/12 s.4) |
| `ExportPS2Functions.java` | cópia sem modificação do PS2Recomp (`ps2xRecomp/tools/ghidra/`, commit `e563698`), GPL-3.0 | exporta o CSV de funções e o TOML de config que o `ps2_recomp` lê |

Ordem de uso: análise automática → `ApplyScriptApiNames.java` → reanálise →
`ExportPS2Functions.java`. O `tools/ghidra_analisar.ps1` faz tudo em
sequência.
