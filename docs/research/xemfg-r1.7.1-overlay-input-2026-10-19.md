# XeMFG R1.7.1 — recuperação da tecla do overlay

19/10/2026, America/Sao_Paulo. Base R1.7 `257cfd7656c3aa8336610c407a0b0e611dd68964`, branch `feature/1.4.5-xemfg-overlay-input-r1.7.1`. Candidata local; sem publicação.

## Diagnóstico e limite

O usuário confirmou que a R1.7 abriu o jogo, mas a Home continuou falhando. O processo 27888 carregava uma única cópia do ReShade (além do DXGI de sistema). O log confirmou criação do runtime manual, inferência 4x e metering aceito; esses resultados não validaram o teclado. Essa evidência da R1.7 está separada dos testes da candidata.

Foi acrescentado um teste de input ao harness com ReShade e Intel reais. Quatro toques: dois mantidos entre atualizações e dois pressionados/soltos entre atualizações. Com o addon R1.7 de SHA-256 `cce8fd873fa803bea7740379a86c3d472b58c0496bf48e8d27fc6db68b43f5fc`, houve **uma abertura e um fechamento**, em vez de dois de cada. Evidência negativa: `native-20261019-200829-194351/result.json`. O modelo/Tensor continuou funcionando.

O [input do ReShade](https://github.com/crosire/reshade/blob/main/source/input.cpp) guarda o estado da tecla; key-up pode substituir key-down antes de um runtime consultar `is_key_pressed`. O [GUI](https://github.com/crosire/reshade/blob/main/source/runtime_gui.cpp) consulta esse estado para alternar o overlay. O teste demonstra um caminho de perda de toques. **Não prova que todas as falhas da Home no jogo tenham essa única causa.**

## Implementação

O observador `WH_GETMESSAGE` existente de `WindowControl` agora registra toques completos do atalho configurado, em uma fila fixa de 16 elementos. Não altera as mensagens de teclado. Usa `KeyOverlay` e `ForceShortcutModifiers` lidos pela API pública do ReShade, incluindo modificadores, tecla personalizada e atalho desativado. A configuração é revisitada a cada 500 ms; não há leitura de arquivo por frame.

Quando a integração usa teclado Raw Input sem mensagens legadas, a observação também examina WM_INPUT de teclado. Essa rota só é usada após consultar os registros existentes; não registra nem redireciona dispositivos. Raw Input buffered sem WM_INPUT não é coberto por essa recuperação. Referências: [WH_GETMESSAGE](https://learn.microsoft.com/en-us/windows/win32/winmsg/getmsgproc), [GetRegisteredRawInputDevices](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getregisteredrawinputdevices), [GetRawInputData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdata).

`native_overlay.cpp` acompanha pedidos normais do ReShade durante a atualização do runtime final. Se o ReShade já tratou um toque, esse toque é consumido e não é repetido. Depois da atualização, toques completos ainda não tratados usam `effect_runtime::open_overlay`, a [API pública](https://github.com/crosire/reshade/blob/main/include/reshade_api.hpp). A abertura recuperada fica visível na próxima atualização do runtime. Retorno de veto é respeitado, sem forçar a chamada novamente.

Uma diferença encontrada no teste: o evento de abrir/fechar usa a última fonte de navegação ImGui, que pode ser mouse mesmo quando Home causou a mudança. A deduplicação consulta a tecla, não presume que o campo `source` seja sempre teclado. O primeiro teste da implementação acusou uma abertura extra por essa suposição; foi corrigido (`native-20261019-201241-068545`).

Captura de texto/widget ativo e Escape impedem recuperação indevida. A fila é descartada em reconfiguração, aposentadoria e perda de foco de uma janela visível/minimizada. Resize desativa a ligação antiga do atalho; a nova instância é configurada ao voltar a apresentar. O observador não chama APIs gráficas ou de mudança de janela na thread de mensagens. A recuperação ocorre somente na thread de apresentação do runtime.

Os contadores novos ficam no relatório copiado, não em listas ao vivo. Os logs de pedidos/recuperações são limitados. A API anterior `MFGAXeOverlayStatus` conserva seu layout; a observação de teclas usa uma nova exportação, `MFGAXeOverlayInputStatus`.

## Verificação

O teste real exige duas aberturas e dois fechamentos, exatamente duas recuperações e nenhuma perda da fila. Após resize e FG off, exige mais uma abertura e um fechamento. Ele é executado nos caminhos 2x/3x/4x/6x da suíte, junto de inferência, Tensor, XeLL, metering, lifetimes, HUD e política de pacing. Um teste separado cobre deduplicação, múltiplos toques, autorepeat, modificadores, rebinding, desativação, captura de texto, cancelamento e overflow limitado.

Build MSVC Release x64 passou. CTest terminou com **64 testes aprovados, três opcionais ignorados e nenhuma falha**, em 50,01 s. Uma rodada adicional 4x passou com XeLL 1.3.2.10. Hashes e resultados finais constam do manifesto/`validation-summary.json` do pacote. Evidências intermediárias preservam a falha R1.7 e a falha inicial de deduplicação. Também houve falha de link na primeira compilação: o include ImGui precisava preceder `reshade.hpp` para usar a tabela pública de funções. Não se alterou a dependência ReShade para resolvê-la.

O callback público de GUI é usado para respeitar widgets ativos e texto; isso pode acrescentar processamento ImGui quando o overlay está fechado. O custo em gameplay e a Home com foco/alt-tab reais ainda precisam validação. Nenhuma melhora de frame pacing/FPS/latência é anunciada nesta correção. As políticas, modelos, kernels, multiplicadores, waits e UI Composition permanecem os da R1.7.

## Entrega e próximo teste

ProductVersion `1.4.5-XeMFG-R1.7.1`, FileVersion `1.4.5.13`. Pacote novo com hashes e rollback R1.7/R1.6; o pacote anterior R1.7 permanece preservado. Instalação exige jogo fechado. Validar Home várias vezes na cena, inclusive toque curto e após alt-tab, antes de continuar o A/B de pacing. Se ainda falhar, os eventos novos distinguem pedido de overlay, toque recuperado e ausência de observação. Não tratar o sucesso sintético como confirmação antecipada do usuário.
