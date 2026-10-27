# Plano 1.5.8 — Fullscreen exclusivo como borderless (XeMFG)

Origem: relato de crash no Metro Exodus Enhanced Edition (addon 1.5.5, RTX 4080). A investigação
mostrou que o crash era do ambiente do usuário (não reproduziu com RenoDX HDR nem nos testes de
transição), mas o jogo usa fullscreen exclusivo, que o XeFG não suporta. Hoje o XeMFG só
desliga a geração nesse modo.

Objetivo: opção opcional (desligada por padrão) `[XeFG] ExclusiveAsBorderless=1` que entrega
borderless quando o jogo pede fullscreen exclusivo na saída XeMFG.

| # | Item | Estimativa | Status |
|---|------|-----------|--------|
| 1 | `SetFullscreenState` em tempo de execução: não repassa ao XeFG; janela borderless cobrindo o monitor; `FALSE` restaura | 1,5 h | feito |
| 2 | `GetFullscreenState`/`GetFullscreenDesc` mostram fullscreen ao jogo; admissão lê o estado real | 30 min | feito |
| 3 | `ResizeTarget` ignorado no modo borderless | 15 min | feito |
| 4 | Criação já em fullscreen: saída criada em janela e borderless | 45 min | feito |
| 5 | Alt+Enter do DXGI bloqueado com a opção ligada | 45 min | feito |
| 6 | Painel: opção, estado e aviso quando o jogo usa fullscreen exclusivo | 45 min | feito |
| 7 | Conformidade: transição, primeiro frame e criação em fullscreen, com e sem a opção | 1,5 h | feito |
| 8 | **[você]** Metro em fullscreen exclusivo com a opção; outro jogo sem a opção | 20 min | feito (Metro e Cyberpunk, 2026-10-26) |
| R | Gate, notas, `FILEVERSION` (parado em 1,5,2,0), merge, tag e release | 1 h | feito (2026-10-26) |

## Registro

- **2026-10-26 · investigação** Testes de transição para fullscreen exclusivo (DLSS-SR, XeFG 1.3.1.78
  e XeLL 1.3.2.10 com os mesmos hashes do usuário, HDR10, antes do primeiro frame e durante a
  geração): nenhum crash; a geração para em fullscreen e retoma em janela. Metro com RenoDX HDR
  no PC de teste: sem crash. Conclusão: crash do ambiente do usuário (Display Commander, RTSS ou
  outro componente).
- **2026-10-26 · itens 1–7** Opção `[XeFG] ExclusiveAsBorderless` (painel: *Exclusive fullscreen as
  borderless*). Conformidade nova: `exclusive-transition`, `exclusive-first` (sem a opção: geração
  para em fullscreen e retoma em janela) e `exclusive-borderless`, `-first`, `-create` (com a opção:
  o jogo vê fullscreen, a janela cobre o monitor, a geração continua, a janela volta ao sair e o
  Alt+Enter é bloqueado). Os casos com a opção não trocam o modo do monitor.
- **2026-10-26 · campo** Metro (fullscreen exclusivo, DLSS-SR, RenoDX HDR): com a opção ligada, o pedido de
  fullscreen foi atendido como borderless e a geração ativou. Alt+Tab/Alt+Enter não testados em campo
  (cobertos pelo caso automático). Cyberpunk (borderless, opção desligada): sem mudança.
- **2026-10-26 · release 1.5.8** Gate 192/192 (3 skipped de proveniência). `FILEVERSION` 1,5,8,0 e teste
  `xemfg_version_resource` para manter a versão numérica igual à de texto.
