# XeMFG R1.7.2 — foco dos cartões informativos

19/10/2026, America/Sao_Paulo. Base R1.7.1 `e864c263475c68b56f5004b9953734f5d76f1e11`; branch `feature/1.4.5-xemfg-passive-card-focus-r1.7.2`. Candidata local, sem publicação.

## Evidência e diagnóstico

O usuário relatou que a Home continuava falhando às vezes na R1.7.1 e enviou uma imagem com um contorno azul no cartão de status “4x / Multiplier”. Esse contorno é compatível com o cursor de navegação ImGui; não indica mudança do multiplicador. Os cartões não contêm botões ou seletores.

`panel_ui.hpp::Hero` criava os cartões como child windows com `NoScrollbar|NoScrollWithMouse`, sem excluir navegação/foco. Isso deixava o contêiner elegível para navegação, inclusive quando pequenos excessos de conteúdo o tornavam uma região rolável. `EndChild` do ImGui também submete um item no pai. Portanto, tornar só os textos passivos não basta.

O [header primário do ImGui](https://github.com/ocornut/imgui/blob/master/imgui.h) distingue `NoNavInputs`, `NoNavFocus`, `NoMouseInputs` e `ImGuiItemFlags_NoNav`. O [EndChild](https://github.com/ocornut/imgui/blob/master/imgui.cpp) explica o item do contêiner e seu cursor. O código local dessas dependências foi conferido; não foi modificado.

O ReShade usa o estado de itens ativos para suspender atalhos enquanto se interage com a interface. Contudo, foco de navegação (`NavId`) e item ativo (`ActiveId`) são estados diferentes. **O print não demonstra sozinho que o cartão bloqueou a Home.** A implementação R1.7.1 de recuperação de toques permanece; a causa completa da falha em jogo continua aberta.

## Correção

- Os três cartões largos e o resumo compacto passam a usar `ImGuiWindowFlags_NoInputs`: são somente leitura e não capturam foco por clique, teclado ou gamepad.
- Uma região RAII `NoNavigation` também marca os itens do resumo como `ImGuiItemFlags_NoNav`, incluindo o item que `EndChild` submete no pai.
- `Card` aceita flags adicionais somente para esse resumo. Os cartões que contêm controles mantêm o comportamento anterior.
- O helper é compartilhado por DLSS-G e XeMFG. Não se desabilita a navegação global do ReShade, dos controles reais ou de outros addons.

Não houve mudança de modelo, HUD, composição, multiplicador, pacing, waits ou política de input global. Não se alterou o tratamento de Home durante edição de texto ou widgets ativos.

## Verificação e limites

Dois cenários adicionais usam os componentes de produção com status sintético: 930 px/font 20 e 460 px/font 22. Clicam no resumo e usam Tab, Enter e Home. Exigem que o foco não entre em resumo algum e que a navegação alcance os controles reais (combo e checkbox), evitando um teste que apenas desliga toda a navegação. Os primeiros ensaios não entravam no child de controles: faltava Enter depois de focar seu contêiner. O harness foi corrigido para exercitar essa interação, sem afrouxar a condição de sucesso.

As imagens `navigation.png` e `navigation-compact.png` são evidência de layout/comportamento dos componentes, não capturas do jogo. A imagem larga foi inspecionada: o foco termina no seletor real; os cartões de status ficam sem contorno de seleção. Build MSVC Release passou e o CTest terminou com **66 testes aprovados, três opcionais ignorados e nenhuma falha**, em 49,60 s. Os testes reais 2x/3x/4x/6x e a rodada 4x com XeLL 1.3.2.10 passaram, incluindo a recuperação de Home e resize da R1.7.1. Inputs, hashes e resultados estão no manifesto e no resumo de validação do pacote.

Não há confirmação de que a Home ficou confiável em Cyberpunk, nem benchmark novo de pacing. O próximo teste é repetir abrir/fechar, clique nos cartões e alt-tab na cena, preservando os mesmos ajustes. Persistindo a falha, correlacionar os eventos `native_overlay_request` e `native_overlay_key_recovery`, separando foco, captura por widget e ausência de mensagens. Não anunciar o defeito inteiro como resolvido com base no print.

ProductVersion `1.4.5-XeMFG-R1.7.2`, FileVersion `1.4.5.14`. Rollback exato R1.7.1 incluído, além das bases guardadas. Substituir somente com jogo fechado; nenhuma release é publicada automaticamente.
