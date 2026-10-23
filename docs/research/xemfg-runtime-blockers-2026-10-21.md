# XeMFG — bloqueios reais depois da RC3

Base: `5b7b075bd9c15bf02f98b90a8adc5e82d2b805bb`, branch `feature/1.5.0-xemfg-capability-adapters`. Candidata local: **1.5.0-rc.4**. Nenhuma publicação ou instalação automática em jogos.

Os quatro addons instalados foram conferidos: SHA-256 `0ae438091d8d05fd77a11320bca4c37cb2585cc0c053d813c9ab435e5c981aa5`, a RC3 oficial deste laboratório. As falhas abaixo pertencem a essa base, não à árvore RD9 antiga. Os logs originais foram preservados em `build/rc4-validation/runtime-inputs/`; `blockers.json` guarda hashes e recibos sem caminhos pessoais. A captura desses logs é leitura de sessões já realizadas pelo usuário, não nova validação de gameplay da RC4.

## Mafia: recursos recusados antes do SDK

Sessão PID 20712. O jogo pede FG no viewport 1; os constants V2 passam. Depth, MV, cor sem HUD e UI chegam com `ResourceTag` V1 e **Resource version 0**. A RC3 exige Resource version 1 antes de examinar a textura, e por isso registra largura/formato zero e bloqueia em `depth_unavailable`. Não é prova de textura vazia: esses campos não haviam sido consultados.

A RC4 admite somente dois cabeçalhos V0 estreitos: GUID Resource conhecido ou GUID vazio, ambos sem extensão encadeada. V1 requer o GUID correto. Versões futuras e tipos diferentes continuam recusados. A admissão não altera o objeto do jogo nem a chamada Streamline. Ela ainda exige retorno upstream bem-sucedido, interface D3D12 válida, mesma identidade de dispositivo, textura 2D não MSAA, array unitário, estado explícito, extent válido e duração correta. Recursos voláteis exigem a command list real do produtor; o SDK realiza sua cópia ali.

O teste `xemfg_native_zero-resource` usa texturas D3D12 reais com cabeçalho vazio e exercita os dois métodos de tags, inputs voláteis, remoção/falha e retomada da inferência Tensor. Isso corrige uma rejeição reproduzível da ponte. **A aceitação dos recursos da sessão real do Mafia ainda precisa ser confirmada com a RC4.** O GUID do Resource não aparece no log antigo; a política não aceita qualquer GUID para contornar essa lacuna.

## Plague Tale: falta de espera antes dos markers

Sessão PID 23608, Streamline 2.11.1. Recursos 7, constants válidos e markers observados 31; sleep 0, markers aceitos 0, primeira recusa `source_wait_not_completed`. Nenhum `native_sleep_mode`/`native_sleep_association` aparece na sessão. Os sinais de imagem chegam; o primeiro bloqueio persistente é a tradução da latência. Isso não prova qual API privada o jogo usa para sua espera.

A RC4 pode usar a entrada do **marker real de SimulationStart** como fronteira XeLL quando oito inícios de simulação foram observados sem qualquer chamada explícita de `slReflexSleep` nessa saída. Exige FG solicitado, NVIDIA FG desligado com confirmação, Reflex desligado com confirmação e saída XeLL. Uma chamada explícita observada desabilita essa alternativa para a saída; sleeps já concluídos/in-flight não são repetidos. Não é aplicada à saída Reflex. Os markers mantêm seu ID real e são encaminhados no ponto em que chegam. Um InputSample anterior à espera é apenas omitido do XeLL; não é reemitido com timestamp inventado nem invalida toda a sequência obrigatória.

A espera ocorre sem a trava de bookkeeping. A saída fica viva por lease; o epoch é conferido antes do SDK e depois de sua volta. Resize entre decisão e execução descarta a espera antiga. Não há espera adicionada em Present, marker sintético, associação com token vizinho por heurística ou interceptação NVAPI nova.

O teste `xemfg_native_simulation-sleep` omite explicitamente os primeiros sleeps: sete frames passam sem FG, a oitava simulação recebe a espera correta e alcança inferência. Depois, chamadas explícitas voltam a ocorrer sem espera duplicada. A suíte mantém os testes de sleep antecipado, tail sleep, markers legados, Reflex, chamada em voo e resize. **Ainda não há medida de latência física ou gameplay RC4 de Plague Tale.** Uma integração que habilite outro limiter fora de Streamline exige auditoria própria.

O ponto de integração segue o [contrato XeLL de sleep e markers](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md). A referência arquitetural do OptiScaler separa entrada de markers, entrada de sleep e saída de latência; sua implementação também trata integrações sem sleep explícito no começo da simulação. A implementação acima é própria e não copia seu código GPLv3.

## Death Stranding 2: especialização portátil ausente

Sessão PID 24404, executável `DS2.exe`: recursos 7, sleep aceito, markers aceitos, constants V2 presentes. O único bit de incompatibilidade é 16: **motionVectorsJittered true** contra saída inicializada false. Portanto não é ausência genérica de inputs.

Foi testada primeiro a flag de inicialização correta, `XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV`. O SDK falhou com -1000 antes da inferência. O rastreio de primeira exceção e o probe de parâmetros localizaram a falha ao resolver a chave **`35112441167526893749578`**. Ela não consta do conjunto portátil de 54 programas, e a tentativa de resolvê-la pelo decoder nativo também falha nesse caminho perfilado.

O probe compara a configuração funcional `35118300971948070613363` com a configuração jittered. Os parâmetros registrados são iguais exceto `d243`: 0 versus 1. Mesma ABI de parâmetros não prova equivalência do shader. **Não foi criado alias da chave nova para o shader sem jitter, nem falsificado o constant do jogo.** Também não foi reduzido o multiplicador ou mudado o modelo.

Foi conferido estaticamente também o `MFGUnlocker (2).asi` enviado pelo usuário: SHA-256 `f34abd14975a5328ae3baf613f681b75d3075a4ff2a80da20cfefbaa0e9fab7b`, 7.415.808 bytes. Seu manifesto de programas ainda tem 54 entradas e SHA-256 `d74bd783d29280ed372653701ce9b0395362e381faace1213206e0bdd9bfc9e5`, igual ao conjunto usado aqui; a chave jittered está ausente. Nenhum código do ASI foi carregado para essa verificação. Isso confirma a lacuna do conjunto disponível, não demonstra que todos os modos do outro mod falham no jogo.

A RC4 reconhece a convenção observada de `DS2.exe` na inicialização e informa explicitamente que falta esse shader. Retorna à apresentação original antes de criar um contexto Intel. Nesse caso também preserva as opções, limiter e espera Reflex originais: não desliga a latência nativa quando não há uma saída XeLL para substituí-la. O teste `xemfg_native_jittered-motion` verifica essa recusa, a política Reflex preservada, renderiza/apresenta quatro imagens reais pelo caminho original e exige zero contexto/inferência Intel. **DS2 continua sem suporte de interpolação nesta candidata.** Para fechá-lo é necessário implementar a especialização jittered ou uma conversão de MV comprovada, com contrato de unidades, jitter anterior/atual, duração e equivalência GPU. Uma simples troca de flag já foi demonstrada insuficiente.

O [header Intel](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/inc/xess_fg/xefg_swapchain.h) documenta a flag de remoção de jitter. Esse contrato não fornece o programa privado que falta à adaptação portátil. O probe `mfgunlock_native_xemfg_init_probe` é EXCLUDE_FROM_ALL e usa `XESS_INIT_PROBE`; registra somente dados de inicialização em processos próprios. Não entra no pacote, não roda em jogos e não é parte da validação de desempenho.

## Black Flag: perda do dispositivo anterior à interpolação

Sessão PID 19508. A saída anterior é aposentada e a chain nativa mantida por referência externa é reutilizada. O primeiro Present da saída seguinte falha com `0x887A0005`; a razão específica do dispositivo é `0x887A002B`. Nesse ponto: FG solicitado 0, FG habilitado 0, nenhuma inferência Tensor da sessão. Portanto atribuir essa falha ao modelo MFG seria incorreto. DRED retorna `0x887A0004`; seus contadores zero não demonstram ausência de trabalho incorreto.

Foi ampliado o teste de referência externa: ele agora renderiza e apresenta antes da aposentadoria, mantém duas referências de um observador independente durante a nova inferência e resize e cobre tanto CreateSwapChain quanto CreateSwapChainForHwnd. Isso expôs uma **outra** falha concreta da continuidade: uma requisição HWND com alpha UNSPECIFIED volta de DXGI como IGNORE; a comparação literal impedia sua reutilização. A RC4 permite somente essa equivalência opaca. Não trata PREMULTIPLIED/STRAIGHT como equivalentes e preserva os outros campos imutáveis.

Depois dessa correção, o teste chega a inferência real, apresentação, resize e destruição, com as referências externas preservadas. Ele **não reproduziu a perda de dispositivo 0x887A002B do Black Flag**. Logo essa correção não será anunciada como solução do jogo. A interação entre buffers/filas antigos, troca de saída e consumidores externos permanece aberta. Não foram desabilitados Steam Overlay, outros addons ou proteções DXGI; o HRESULT específico continua preservado.

Um processo isolado adicional carregou o addon RenoDX instalado para Black Flag junto da RC4. A sequência de factory legada, apresentação antes da aposentadoria, duas referências externas, inferência, resize e teardown terminou com exit 0. Isso afasta uma incompatibilidade universal entre os dois addons nesse cenário. Não executa os shaders/caminhos específicos do jogo e não carrega o Steam Overlay real; portanto não descarta uma interação específica da sessão problemática. Os arquivos do jogo foram apenas lidos/copiados para o laboratório, sem trocar sua instalação.

## Comparação e limites de escopo

Referências locais: OptiScaler `c2e44a8bc911010d7ed3e4fa63843185fbb55d03` (GPLv3, consulta arquitetural); MFG-Unlocker `a4cf2c4ab632e84c72254932793bb9e64b33d76e` (MIT, base já usada no port). O conjunto público consultado de shaders continua com o manifesto de 54 programas. Não foi incorporado código GPL.

A cobertura maior do OptiScaler inclui múltiplas rotas FG e de latência e normalização de recursos. A RC4 não equivale a esse conjunto: não adiciona FSR-FG, input XeFG nativo completo ou associação NVAPI64 → token Streamline. O caminho DLSS-G, UI Composition opcional/off, CUDA e política de pacing existentes foram mantidos. Nenhuma compatibilidade universal, ganho de FPS, fluidez ou latência física é alegada.

## Validação e entrega

Sete verificações direcionadas finais passaram. A suíte completa Release/Debug e os controles negativos contra a RC3 são registrados no manifesto da entrega, com SHA do addon efetivamente usado por processo. Falhas iniciais foram preservadas em `build/rc4-validation/`, incluindo a inicialização jittered falha e o teste de alpha rejeitado antes da correção. Não são contadas como verificações finais aprovadas.

Os três controles com a RC3 selada falham como esperado: o cabeçalho zerado é recusado como metadata antes de a asserção de composição detectar a consequência; a oitava simulação continua sem espera; a recriação com alpha opaco equivalente é recusada. Os recibos conferem o SHA da RC3 e os eventos que explicam as falhas. Não são testes aprovados de gameplay.

Debug final: **136 aprovados, 3 opcionais ignorados, zero falhas**, 482,55 s. Os três opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`, sem seus inputs históricos explícitos. Não foram contados como aprovados. A primeira suíte Release também passou antes do ajuste final que preserva Reflex no passthrough; seus registros ficaram separados, e a entrega exige a nova rodada Release com o binário final.

Release final, já com o ajuste de passthrough: **136 aprovados, 3 opcionais ignorados, zero falhas**, 419,55 s. SHA-256 do addon Release: `0c989c75e655260d40dec3fe14d13f94484a838cad64b3b4b41ae446a35c6e92`. Debug: `d082ce10f11404b7220d7be6ce07d7c44614578c8b83faaa818cbfe042fa0b5b`. A inspeção dos recibos exige que o addon de cada processo corresponda ao binário entregue. O teste com o RenoDX real usa o mesmo Release final.

Os testes gráficos usam DXGI/D3D12, ReShade, SDK Intel e CUDA reais, com chamador Streamline sintético e smokes Streamline reais. Eles não certificam a cena de cada jogo, todos os overlays ou scanout. O pacote local inclui addon Release/Debug, símbolos, proveniência, resultados e rollback RC3. Não inclui DLLs de runtime ou o probe de inicialização.
