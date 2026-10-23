# XeMFG R1.7.3 — cobertura de teclado do overlay

19/10/2026, America/Sao_Paulo. Base R1.7.2 `48f57da67fb924ce0ed18490b03c90996e1f294f`, branch `feature/1.4.5-xemfg-overlay-routing-r1.7.3`. Candidata local, sem publicação.

## Resultado e limite

O usuário confirmou que os cartões passivos ficaram corrigidos na R1.7.2, mas Home ainda falhava. A sessão PID 24344 registrou 16 pedidos alternados de abrir/fechar e nenhuma recuperação no trecho registrado. O log de pedidos tem limite de 16: **não permite contar todos os toques nem determinar por que um toque falhou**. Não havia registro da chegada do teclado ou dos motivos de descarte. Preservamos esses dados como referência da R1.7.2; não são validação da candidata.

Há uma lacuna reproduzível de cobertura: o observador do addon procurava somente o HWND exato da janela de renderização. O [input do ReShade](https://github.com/crosire/reshade/blob/main/source/input.cpp) também procura uma janela conhecida no pai do destino. Um toque mantido numa janela filha pode funcionar pelo ReShade, enquanto um toque completo entre atualizações se perde tanto no estado do ReShade quanto no observador anterior.

O teste real, com **addon selado R1.7.2** de SHA-256 `8c9bbbf03e14bd4e33b19b1da8010991e71ebc78b17ac6d60782408928eb1a32`, confirmou quatro aberturas e apenas três fechamentos, em vez de quatro de cada. As etapas anteriores na janela principal continuaram passando. Artefato negativo: `native-20261019-203938-959538/result.json`. Isso demonstra um defeito do observador, **não demonstra que o Cyberpunk encaminhou todos os toques problemáticos para uma filha**.

## Alteração

`WindowControl::MessageHook` continua observando `WH_GETMESSAGE`, sem modificar mensagens de teclado, registrar dispositivos, usar hotkey global ou trocar a tecla configurada. Além do HWND exato, aceita uma filha da janela de renderização na mesma thread, somente quando há um controlador inequívoco. Janelas de topo sem relação continuam excluídas. Mensagens privadas de configuração de janela conservam o endereçamento exato e a identidade de geração.

A recuperação continua pela API pública `effect_runtime::open_overlay`, após a atualização do runtime final. Uma chamada nativa consome o próprio toque; o teste verifica que a ampliação de cobertura não cria uma segunda alternância. Captura por widget/texto, Escape, configuração de modificadores, perda de foco e aposentadoria mantêm suas políticas. Os cartões continuam passivos. Não alteramos a dependência ReShade.

Acrescentamos recibos limitados do atalho ao log assíncrono existente, produzidos na thread de apresentação, somente quando há atividade do atalho ou uma tecla correspondente no ReShade. São no máximo **64 registros por runtime**. Não há texto digitado nem histórico de outras teclas. O observador da thread de mensagens atualiza apenas contadores sob sua trava; não grava arquivo nem chama o renderer.

`native_overlay_input` distingue contagem de presses/releases, correspondência dos modificadores, tratamento nativo, toques completos enfileirados, consumidos, bloqueados e recuperáveis, cancelamento e mensagens vindas de filhas. `blockers` é uma máscara: Escape=1, widget ativo=2, entrada de texto=4, janela sem foco/minimizada=8 e aposentadoria=16. `requested_open` é estado **solicitado**, não uma medida de pixels exibidos nem confirmação agregada de todos os callbacks externos. Ausência de recibo não prova defeito do ReShade: pode indicar uma rota de entrega ainda não observada. Raw Input buffered sem WM_INPUT e janelas de outra thread continuam fora da recuperação ampliada.

Esses recibos não aparecem como contadores contínuos na interface. As exportações anteriores conservam ABI. Pacing, waits, modelos, qualidade, HUD, UI Composition e ponte Reflex→XeLL não foram modificados. O custo incremental dos registros limitados não foi medido em gameplay; nenhum ganho de FPS/latência é anunciado.

## Validação

O harness com ReShade real e SDK Intel exige agora quatro aberturas/quatro fechamentos: dois toques mantidos e dois completos na janela principal; dois completos após resize/FG off; um mantido e um completo na filha. Exige cinco recuperações no total e zero overflow; os três toques mantidos não podem ser recuperados de novo. O parser exige também um recibo da filha que corresponda à recuperação. O teste Win32 separado verifica que uma janela sem relação não altera o contador e que o motivo de bloqueio por widget permanece disponível.

Os testes da máquina de estados distinguem tratamento nativo, captura por widget, modificador incompatível e idle sem logs contínuos, além dos contratos anteriores. A suíte mantém os testes de navegação wide/compact da R1.7.2, inferência/Tensor, tagging, HUD, XeLL, aplicação/restauração de metering e política de deadlines. Janelas sintéticas/ocultas não validam foco real ou cadência do monitor.

Build Release x64 com MSVC e CTest: **66 aprovados, três opcionais ignorados, nenhuma falha**. Inferência 2x/3x/4x/6x passou, e uma rodada adicional 4x passou com XeLL 1.3.2.10. Resultados e hashes estão no manifesto e `validation-summary.json`. Os três ignorados são os probes/manifests históricos opcionais, não os testes novos de teclado.

## Próxima verificação

FileVersion `1.4.5.15`, ProductVersion `1.4.5-XeMFG-R1.7.3`. Pacote separado, com rollback R1.7.2 e oficial 1.4.5. Instalar somente com o jogo fechado e preservar os INIs atuais.

No jogo, testar abrir a Home a partir do overlay fechado e fechar após interagir nos controles. Se ainda falhar, informar qual direção falhou. Os recibos permitem separar entrega ausente, modificadores, captura e recuperação, antes de propor outro patch. Não anunciamos a correção completa da Home antes dessa validação; a confirmação dos cartões pertence à R1.7.2.
