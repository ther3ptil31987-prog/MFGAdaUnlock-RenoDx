# XeMFG R1.7.5 ? painel em uma janela e entrada controlada pelo ReShade

Base: `5ef1af84f15454a013715b3e77856834b396266a` (R1.7.4), descendente da 1.4.5 oficial. Branch isolada `feature/1.4.5-xemfg-ui-input-r1.7.5`. Esta candidata n?o ? uma release publicada.

## Problema e evid?ncia

O usu?rio informou Home intermitente no Cyberpunk e aus?ncia do overlay com bloqueio dos comandos no Pragmata. A imagem continua animando: o relato ? de captura de entrada, n?o de congelamento da renderiza??o. As tentativas R1.7.1?R1.7.4 n?o resolveram o problema em jogo. O usu?rio associa o in?cio ? interface nova; a causa exata ainda n?o foi demonstrada.

Os logs e configura??es dos dois jogos foram preservados antes de qualquer substitui??o, em `out/xemfg-r1.7.5-evidence/game-reference-r174/`. Ambos carregavam o addon SHA-256 `1a9d67b86f76c7a4c97290dc55a3e64daf57667cf71c5ce7469a2793ae12ec3c` e ReShade 6.8.0.2155.

No Pragmata, `startup_rejected: unreviewed_Intel_runtime_pair` ocorreu antes de existir nosso runtime final e controlador de janela. Isso separa a falha de entrada desse caminho espec?fico de intercepta??o de Home. N?o demonstra que os widgets sejam a causa. O par Intel permanece recusado; esta mudan?a n?o inventa compatibilidade XeMFG com esse par.

## Mudan?a conservadora

- Os cart?es e resumos deixam de criar janelas filhas ImGui. Grupos, tabelas e separadores desenham o painel na janela fornecida pelo ReShade. Os controles, abas, tooltips e navega??o dos controles permanecem.
- A captura/reprodu??o de taps, o registro de Home com `RegisterHotKey`, o veto de toggles nativos e os hooks de ativa??o adicionados nas ?ltimas tentativas foram retirados do addon nativo. O controlador de janela volta ? implementa??o de borderless de R1.7, consumindo apenas sua mensagem privada com gera??o v?lida.
- O callback permanente `reshade_overlay` do addon foi retirado. O caminho de GUI fechada pode voltar ? sa?da antecipada do ReShade. N?o foi medido ganho de CPU/FPS decorrente disso.
- O runtime manual de apresenta??o final permanece, com cria??o, resize, cor e destrui??o. O observador de abertura/fechamento apenas registra at? 32 pedidos por runtime, sem veto nem altera??o de estado.
- O painel explica a indisponibilidade dos runtimes Intel quando a ponte n?o pode iniciar. UI Composition continua opt-in e desligado por padr?o.

N?o foram alterados modelos, kernels de gera??o, multiplicador, pol?tica de XeLL, flip metering, deadlines, recursos do jogo ou configura??es instaladas. A ABI antiga de diagn?stico de entrada ? preservada; os contadores de recupera??o e descarte retornam zero, pois esse mecanismo n?o est? ativo.

## Verifica??o e limites

Os testes agora selecionam explicitamente a aba **MFG Unlock** dentro do ReShade e exigem que o callback de produ??o tenha sido executado. Um contador de desenho somente de leitura permite distinguir isso de um teste que abriu apenas a aba Home. O preview separado verifica que nenhuma janela filha foi criada, inclusive em largura estreita, fontes maiores e navega??o por teclado.

O teste adicional `xemfg_reshade_panel_input_tests.py` usa uma janela pr?pria, ReShade real, o par Intel recusado do Pragmata e c?pia apenas das se??es INPUT/OVERLAY de sua configura??o. N?o carrega jogo nem escreve na configura??o original. S? envia teclado quando a janela pr?pria est? em primeiro plano. O crit?rio ? oito aberturas, oito fechamentos, comandos bloqueados enquanto o overlay est? aberto e nove comandos recebidos nos estados fechados. SDKs e configura??es t?m hashes no resultado.

As verifica??es com SDK aceito cobrem 2x/3x/4x/6x, execu??o real Tensor/CUDA e XeLL, UI Composition default/opt-in/off, filtragem de inputs, resize e destrui??o. A Home nativa ? verificada tamb?m ap?s resize/FG off e em janela filha. N?o se anuncia mais recupera??o de taps pelo addon; os nomes dos campos de evid?ncia foram atualizados para n?o confundir os contratos.

A su?te terminou com **66 aprovados, tr?s opcionais ignorados e nenhuma falha**, em 57,99 s. Os dois caminhos adicionais completaram oito aberturas, oito fechamentos, navega??o de controle com Tab e libera??o dos comandos. Os cinco casos SDK (2x/3x/4x/6x e XeLL alternativo em 4x) passaram. Tooltips, dropdown, se??es avan?adas e navega??o larga/estreita tamb?m passaram. A su?te, os testes adicionais, hashes e s?mbolos locais s?o registrados em `validation-summary.json` e no manifesto da candidata. Testes sint?ticos n?o representam valida??o no Cyberpunk ou Pragmata. O teste isolado anterior com a l?gica de UI R1.7.4 e somente um contador de desenho adicionado tamb?m passou: logo, a passagem da nova candidata n?o demonstra causalidade nem cura do defeito relatado.

As primeiras tentativas do smoke de infer?ncia em primeiro plano abortaram pela prote??o de foco, antes de enviar teclado f?sico. No teste dedicado final, a janela ? ativada ap?s criar a sa?da Intel; ambos os caminhos passaram. Esses abortos n?o foram classificados como valida??o de entrada nem como falha reproduzida do jogo.

**Limita??o conhecida:** a candidata devolve o atalho ? captura nativa do ReShade; n?o oferece recupera??o pr?pria de uma tecla pressionada e solta inteiramente entre atualiza??es. A retirada das intercepta??es ? uma simplifica??o para eliminar poss?veis interfer?ncias, n?o uma prova de resolu??o dos sintomas. ? necess?ria confirma??o do usu?rio nos dois jogos antes de anunciar corre??o.

## Reprodu??o

A partir da raiz desta worktree:

```powershell
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

O teste adicional recebe caminhos expl?citos para addon, execut?vel de teste, ReShade, SDK FG/LL e configura??o de refer?ncia. Todas as c?pias s?o feitas no diret?rio de evid?ncia. `tools/package_native_xemfg_ui.py` exige fonte limpa, su?te sem falhas e evid?ncias vinculadas ao hash exato do addon antes de gerar o pacote.

## Instala??o e rollback

Somente com o processo fechado. Guardar addon e INIs com hashes, substituir apenas `renodx-mfgunlock.addon64` e conferir que ReShade, RenoDX, DLLs Intel e configura??es n?o mudaram. O pacote inclui R1.7.4 e 1.4.5 oficial como rollback; n?o implica que a R1.7.4 resolva a falha. N?o houve altera??o da release 1.4.5 publicada.
