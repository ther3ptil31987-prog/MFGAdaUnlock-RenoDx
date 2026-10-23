# Controles nativos do ReShade — XeMFG R1.2

Base: `318594e0c61df3e2968803934e0d0a537a933876`, branch `feature/1.4.5-xemfg-reshade`, derivada da 1.4.5 oficial. A aprovação de imagem e resposta da R1.1 permanece como referência dessa build; as adições desta candidata recebem validação própria. A release publicada e a árvore antiga não são alteradas.

## Comportamento entregue

O painel MFG Unlock passa a apresentar estado efetivo do XeFG, origem Streamline e saída XeMFG; modelo portado ou fallback nativo; rota CUDA, compute capability, layers reconhecidas, lançamentos Tensor e dispatches mantidos em DXIL. Disponibilidade de CUDA e execução observada são estados diferentes. Os contadores são acumulados do processo, não medidas de FPS.

Flip metering tem checkbox persistente e diagnóstico por contexto: função real da NVIDIA disponível, pacer instalado, fila registrada, pedido pendente, configuração, resultado e feedback do driver. `Snapshot(handle)` consulta apenas o owner vivo correspondente. Uma leitura concorrente com configuração retorna “updating” em vez de bloquear a thread de apresentação. O pedido do overlay só é aplicado na fronteira do próximo lote gerado; a desativação usa a forma original de desligamento da configuração. A resolução da NVAPI continua disponível quando o programa inicia com metering desmarcado, permitindo reativar o controle. Recusa latched do driver permanece em fallback; o checkbox não apaga a falha nem força suporte inexistente. Feedback aceito não comprova scanout físico ou metering dedicado de Blackwell.

Force Borderless fica desativado por padrão. Um hook `WH_GETMESSAGE`, específico da thread proprietária do HWND, recebe pedidos privados por `PostMessage`. A alteração de estilo e tamanho acontece nessa thread, sem segurar os mutexes da ponte ou do controller durante APIs que podem reentrar no WndProc. O controlador salva estilo, estilo estendido, placement e visibilidade; remove bordas e usa o retângulo do monitor atual, sem mudar seu refresh. A seleção também converte pedidos de fullscreen exclusivo para windowed, pois o XeFG não suporta geração exclusiva.

O estado exibido é calculado do HWND real: bordas, cobertura do monitor, pendência e erro. A restauração só escreve enquanto estilo e retângulo ainda correspondem ao que o controlador aplicou. Mudanças posteriores do jogo ou de outro addon retornam `ERROR_BUSY`, preservando essa nova propriedade. Um property do HWND e uma geração identificam o vínculo; reciclagem do handle não autoriza restaurar a janela antiga. Retirement durante uma notificação síncrona de resize agenda a restauração antes de retirar o hook. Se a janela não processar mensagens, a solicitação permanece pendente; não é anunciada como aplicada.

## Seleção e estado da latência

As consultas de modelo, CUDA e metering feitas pelo overlay usam tentativas não bloqueantes. Se instalação, configuração ou aposentadoria estiver segurando o respectivo mutex, o painel mostra “updating”. A thread de apresentação não espera por uma chamada do SDK que possa estar aguardando essa mesma apresentação.

As escolhas implementadas são **Input: Auto / Reflex (Streamline)** e **Output: Auto / XeLL**. Auto resolve a única rota atualmente suportada. A preferência explícita seleciona essa rota e é gravada em `[Latency] Input=Reflex, Output=XeLL`; não introduz outro fornecedor de timestamps. São recusados enums/versões desconhecidos antes de mudar a configuração. Não são oferecidos XeLL nativo ou Reflex via NVAPI como inputs sem seus adaptadores, nem Reflex como saída enquanto XeMFG está ativo.

O estado ativo é corroborado por chamadas de sleep recebidas e executadas nos últimos dois segundos, contadores de marcadores encaminhados e confirmação do desligamento do Reflex nativo. O pedido original de modo Reflex do jogo também aparece. A R1.2 preserva o encaminhamento dos marcadores e timestamps da R1.1; não adiciona outro sleep nem altera o timing de inputs, os kernels, HUD, motion vectors ou composição.

Essa restrição segue o [contrato de XeLL](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md): XeFG exige XeLL, e outras soluções de redução de latência precisam ser desabilitadas quando ele estiver ativo. O backend DLSS-G continua com Reflex nativo. Uma seleção independente de Reflex output com XeFG exigiria outra integração e validação; não é simulada por um controle visual.

## Validação e limites

Build completa e CTest são registrados em `build/ctest-native-r1.2.log` e incluídos no pacote: **51 passaram, três opcionais ignorados**, nenhum falhou. Os testes com SDK real em 2x/3x/4x/6x continuam cobrindo inferência CUDA/Tensor, tags, recursos temporários, bypass, recuperação, resize e teardown; agora também verificam preferências persistentes, rejeição de output desconhecido, status de latência, pedidos de metering durante inferência e aplicar/restaurar borderless pela mesma API usada no painel. A criação recebe um pedido inicial de fullscreen exclusivo, convertido para windowed pelo controle antes da inicialização do SDK. O runner confere os valores efetivamente gravados no INI ao sair.

O teste Win32 adicional solicita mudança de outra thread, verifica que ela aguarda o pump do HWND, confere cobertura do monitor, restauração exata de estilo/retângulo/visibilidade, preservação de mudança externa, retirement reentrante e janela destruída. Um teste inicial revelou que `GetWindowPlacement` de uma janela oculta podia levar à sua reexibição na restauração; o controller agora preserva explicitamente `SW_HIDE` nesse caso.

Os testes de inferência usam janelas sintéticas/ocultas. Não medem FPS, exibição física, latência física nem preferência visual do layout. A validação em jogo das novas opções ainda é uma etapa separada. O pacote R1.2 não deve herdar automaticamente a marca de gameplay validado do binário R1.1, cujo hash é diferente.

Todos os controles novos são implementação própria MIT. A fonte GPL do OptiScaler serve apenas como referência externa de comportamento; nenhuma implementação sua é incorporada. As regras de Win32 foram conferidas também no [contrato de SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos). O manifesto liga hashes do addon, fontes, entradas do compilador e evidências. O instalador preserva a build anterior com backup; não há publicação automática.
