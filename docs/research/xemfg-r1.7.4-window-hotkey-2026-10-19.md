# XeMFG R1.7.4 — Home e liberação dos comandos

19/10/2026, America/Sao_Paulo. Base R1.7.3 `35ea7e11b6be030e02bc90d135995f7c9db31932`. Branch isolada `feature/1.4.5-xemfg-window-hotkey-r1.7.4`; candidata local, sem publicação.

## Diagnóstico corrigido

O usuário confirmou que a Home ainda falhava ao abrir e ao fechar. Corrigiu depois o relato: **a imagem continua animada, mas mouse/teclado deixam de controlar o jogo; manter Home pressionada também falha**. Isso é compatível com captura de entrada pelo overlay e não demonstra um bloqueio da renderização. A linha de investigação de travamento/pilhas foi interrompida após essa distinção.

A sessão R1.7.3 PID 23888, addon SHA-256 `b1001ca8a48c8f0cfa46ca40f2b18b6d2789df4a5d89d342c93f2e44b8d23021`, terminou normalmente às 20:50:17 segundo ReShade.log. No trecho final há dois presses/releases da Home, ambos tratados pelo ReShade, sem bloqueio por widget/texto e sem recovery. Outros toques relatados não deixaram recibo nessa rota. **Isso não identifica qual componente reteve os eventos nem exclui outra rota de teclado.** Os recibos dependem da atualização do runtime, portanto sua ausência isolada também não prova entrega ausente. Não atribuímos o defeito ao Steam, ao Windows ou ao Intel SDK sem evidência causal.

O comportamento do [input do ReShade](https://github.com/crosire/reshade/blob/main/source/input.cpp) e de seu [GUI](https://github.com/crosire/reshade/blob/main/source/runtime_gui.cpp) explica a consequência possível: o overlay aberto pode bloquear os comandos do jogo, inclusive consultas de estado de teclas, enquanto a renderização continua. Se Home não chega ao seu tratamento, a rota anterior de recuperação por mensagens também não tem o que recuperar. Isso justifica experimentar um canal de atalho independente; não autoriza declarar a causa exata da sessão.

## Implementação

O controlador da janela usa [RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey) para a tecla/modificadores exatos configurados no ReShade, com `MOD_NOREPEAT`. **A API reserva um atalho de escopo global no desktop.** O addon restringe sua duração à janela alvo visível, não minimizada e em primeiro plano; não é uma garantia de escopo de processo fornecida pela API.

Registro e remoção ocorrem na thread proprietária do HWND, por mensagem privada. Um `WH_CALLWNDPROC` dessa thread observa `WM_ACTIVATEAPP`, `WM_ACTIVATE` e `WM_NCDESTROY` sem substituir o WndProc nem modificar essas mensagens. A desativação remove o registro antes de encaminhar a mensagem; a ativação agenda reavaliação após a mudança de foco. Durante edição de texto (`WantTextInput`), o registro é suspenso para preservar Home como tecla de edição. Rebind, desativação da tecla e aposentadoria também liberam o registro. Reset do runtime limpa o binding/fila; uma recriação imediata pode manter a mesma reserva de janela, sem prometer uma remoção síncrona durante resize.

IDs usam `GlobalAddAtom` por processo/geração, como exige o contrato para DLLs. `WM_HOTKEY` valida o atom, tecla, modificadores, horário de registro e foco antes de enfileirar uma única ação. Não aceita notificações de outra janela ou outro addon. Somente suas mensagens privadas/atalhos são consumidos. O callback não chama o renderer; a ação usa `effect_runtime::open_overlay` na thread de apresentação.

Enquanto o registro é dono do binding, uma eventual segunda notificação nativa da mesma tecla é recusada antes de atualizar o estado solicitado. A chamada pública feita pelo próprio controlador é identificada e não é recusada. As mensagens legadas não enfileiram uma cópia do mesmo press. Assim há um único dono da alternância. O veto de outros addons continua sendo respeitado.

Falha ou conflito no registro conserva o caminho normal do ReShade e a recuperação R1.7.3. Não tenta remover o atalho de outro software. F12, bindings desativados e a política de modificadores opcionais usam esse fallback; não são silenciosamente convertidos em outra tecla. Não há hook global de teclado, polling de todas as teclas, registro de Raw Input, histórico de texto nem mudança global de bloqueio de entrada do ReShade. Recibos limitados agora incluem estado/erro do registro e quantidade de `WM_HOTKEY`; a interface não ganha contadores contínuos.

O funcionamento depende de a thread da janela processar as mensagens e entregar sua desativação. Não é uma recuperação de uma thread realmente travada. Durante o registro, a tecla pertence ao overlay; hotkeys concorrentes e frontends com outras convenções precisam validação. A suspensão de texto é assíncrona entre threads e pode ter uma janela de uma atualização; não se promete suporte universal a todo editor/integração.

## Testes e evidências

O novo teste usa **uma janela própria em primeiro plano**, ReShade real e SDK Intel real. Antes de enviar qualquer tecla via `SendInput`, confirma que sua própria janela tem foco; caso contrário aborta sem envio. Não controla o Cyberpunk ou outro aplicativo. Um hook do próprio harness descarta apenas os `WM_KEYDOWN/UP` legados de Home dessa janela, simulando precisamente a falta desses eventos para ReShade e para o observador anterior. Isso reproduz uma classe de falha, não prova que o jogo real use o mesmo mecanismo.

Com o addon **selado R1.7.3**, os testes anteriores passam e o novo cenário falha ao abrir: `native-20261019-205959-570106`. Com a correção, o cenário completa quatro toques do atalho sem repetição: abre/fecha, desativa/reativa e abre/fecha de novo. Verifica também que um comando de teste chega à janela antes de abrir, é bloqueado enquanto o overlay está aberto e volta a chegar depois de fechar. Um registro concorrente confirma que Home foi liberada na desativação.

O teste Win32 adicional exercita suspensão de texto, reconfiguração de Ctrl+F2 solicitada de outra thread, liberação da tecla antiga, binding desativado, conflito real de registro com fallback e aposentadoria enquanto o atalho está ativo. Mantém os testes de janela, restore, callbacks reentrantes e ownership. Os testes escalares verificam a fila de notificações e seu bloqueio por captura.

Build Release x64/MSVC passou; CTest terminou com **66 aprovados, três opcionais ignorados e nenhuma falha**. Os casos SDK 2x/3x/4x/6x, a rodada 4x com XeLL 1.3.2.10 e os testes extras de foreground/Win32 usam as identidades finais indicadas em `validation-summary.json`. A tentativa inicial de foreground no sandbox abortou por falta de foco sem enviar teclas; a rodada no desktop real passou. Os testes extras de foreground são executados separadamente da suíte oculta, pois exigem um desktop interativo e foco na janela de teste.

Esses resultados validam os contratos examinados e a classe de perda simulada. Não comprovam ainda a correção completa no Cyberpunk. Pacing, waits/fences, kernels, modelos, qualidade, HUD, UI Composition, multiplicador e ponte Reflex→XeLL conservam o comportamento anterior. Não há nova medição de FPS, cadência exibida ou latência física.

## Entrega e validação no jogo

FileVersion `1.4.5.16`, ProductVersion `1.4.5-XeMFG-R1.7.4`. Manifesto e rollback R1.7.3 e oficial 1.4.5 acompanham o pacote separado. Instalação somente com processo fechado, preservando os INIs e os demais addons/runtimes.

Na cena, abrir/fechar com Home e conferir se câmera/comandos retornam depois do fechamento. Testar também uma perda de foco e edição de texto no painel. Se ocorrer captura persistente, correlacionar registro ativo/erro, `WM_HOTKEY`, veto/aceitação e estado solicitado. Não acrescentar outra camada de input antes de verificar essa evidência. Um travamento real de renderização exige outro protocolo; o usuário corrigiu explicitamente esse ponto nesta sessão.
