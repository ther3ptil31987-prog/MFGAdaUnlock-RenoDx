# XeMFG R1.9.1 — estado do jogo e apresentação sem interpolação

20 de outubro de 2026. Continuação na branch isolada R1.9, base 1d007337be46ef0445b51544efa86b27506a3acb. A tentativa R1.9 no Pragmata **não passou**: o usuário informou espera pelo próximo frame e piscadas intensas, com FG habilitado nas opções do jogo.

## Evidência da tentativa

O PID 20980 carregou o par Intel validado a partir de _storage_, aplicou os ports e criou o output. Não houve execução Tensor ou interpolação. Na cena, um registro de admissão mostrou resources=5 (HUD-less e movimento, sem depth), constants=1, valid=1, markers=31, sleep=1 e failed=1. Os registros observados mostraram requested=0, apesar da opção habilitada relatada pelo usuário. Não havia log dos argumentos do setter nem dos bits de status na R1.9; portanto **não está demonstrado por que o jogo terminou solicitando desligamento**.

Outras tentativas também foram preservadas. No PID 22384, uma recriação de swapchain falhou com 0x80070005, registrado pelo Intel; isso é um evento separado, não uma explicação comprovada para todas as piscadas. O PID 7556 voltou a apresentar admissão sem depth e sem interpolação. A causa exata da recusa do depth não é identificável no registro antigo.

Os arquivos estão preservados em out/xemfg-r1.9-evidence/pragmata-failed-attempt/; o pacote anterior e seus recibos permanecem disponíveis. Resultados sintéticos anteriores não são validação em jogo.

## Correções desta candidata

1. **Present ID no passthrough.** A ponte anterior chamava xefgSwapChainSetPresentId somente quando todos os inputs de FG eram aceitos. Agora informa o ID real conhecido do próximo source Present também quando a interpolação é recusada ou desligada. IDs desconhecidos, repetidos ou antigos não autorizam FG. O [guia Intel](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md#integration-summary) associa SetPresentId ao Present; a identificação da apresentação não deve depender do sucesso da geração. Isso corrige uma omissão de integração; seu impacto nas piscadas do Pragmata ainda exige teste.
2. **Estado do backend selecionado.** Depois de confirmar NVIDIA FG desligado e output Intel criado, a ponte não repassa dois avisos exclusivos dos hooks NVIDIA: Reflex nativo ausente e ausência da chamada de backbuffer ao proxy NVIDIA. Esses avisos não descrevem a execução Intel. Erros de formato/HDR, resolução, constantes e códigos de retorno continuam preservados. A candidata não inventa capacidade nem habilita FG quando o jogo pede desligamento. A influência desses avisos na decisão do Pragmata é uma hipótese a verificar com os novos registros. Os bits são definidos no [header Streamline](https://github.com/NVIDIA-RTX/Streamline/blob/v2.8.0/include/sl_dlss_g.h).
3. **Motivo preciso e limitado de recusa.** O log identifica tipo, lifecycle, versões, estado, dimensões, extent, disponibilidade de command list e resultado Intel para os primeiros inputs recusados por output. Também registra mudanças de opções do jogo e estado NVIDIA/orientado à ponte. Não há readback de pixels, dump por frame ou relaxamento das verificações.
4. **Estado visível.** O painel distingue um pedido de desligamento vindo do jogo da espera por inputs compatíveis. Ausência de depth ou movimento também é distinguida no diagnóstico.

Não foram implementados ainda conversão de depth, retenção de recurso volátil, associação heurística entre frames ou outra tentativa de interpolar com input incompleto. É necessário identificar qual contrato falhou antes de escolher essa mudança.

## Verificação e limite

A fixture de Streamline passa a injetar os dois avisos NVIDIA e também erros reais de formato/resolução/constantes. O teste com Intel/ReShade reais verifica que somente os dois avisos do backend desativado são removidos; os outros erros sobrevivem. O runner também exige prova de que o frame com MV ausente recebe um Present ID aceito, sem autorizar interpolação desse frame.

A matriz de testes e os hashes acompanham validation-summary.json. Cobertura de CUDA continua explicitamente em nove de 17 programas. A candidata preserva os cartões compactos, Reflex/XeLL, UI Composition opcional e o par Intel admitido. Não há ganho de FPS, qualidade, latência ou correção das piscadas demonstrado por testes sintéticos.

A primeira instalação é no Pragmata, com backup da R1.9 e sem alterar suas opções. Confirmar primeiro imagem estável e ausência de erro de swapchain; depois conferir pedidos de FG, depth e inferência no novo PID. Não publicar esta candidata como correção geral antes disso.
