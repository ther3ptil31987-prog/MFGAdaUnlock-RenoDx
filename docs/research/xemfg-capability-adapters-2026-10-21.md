# XeMFG — contratos de integração e candidata RC3

21/10/2026. Base: `4e0a651b173cd321cac34999c44365f6baf5531d`, RC2. Trabalho na branch `feature/1.5.0-xemfg-capability-adapters`, na worktree unificada. A árvore RD9 antiga, os pacotes selados e os addons instalados foram preservados. Esta entrega é local, sem publicação ou instalação automática.

## Resultado e escopo

A RC3 corrige dois defeitos reproduzíveis de integração: um consumidor externo de funções DLSS-G podia modificar o estado lógico reservado ao jogo; uma chamada de inputs que atravessasse um resize podia ser associada à nova sessão ao retornar. A admissão agora usa requisitos independentes e exporta o primeiro bloqueio, em vez de reduzir todos os casos a “aguardando”. XeMFG respeita o modo de janela do jogo, inclusive quando existe uma preferência Force Borderless antiga.

Isso não demonstra a correção de Plague Tale, Black Flag ou de todo conflito com Steam Overlay. A RC3 não acrescenta um adaptador completo de FidelityFX/FSR-FG ou de entradas NVAPI diretas. A comparação mostrou por que esses adaptadores são necessários para ampliar a cobertura; expô-los como implementados sem provar seus contratos repetiria o problema investigado.

## Arquitetura observada

| Etapa | Implementação | Propriedade e limites |
|---|---|---|
| Entrada / escolha | `addon.cpp`, `backend_selection.hpp`, `native_bridge.cpp::Attach` | DLSS-G mantém seu caminho; XeMFG inicializa sua ponte própria MIT. Não depende de OptiScaler |
| Modelo / runtime | `backend.cpp`, `vendor/runtime_payload.cpp`, `dependency_manager.cpp` | Par Intel admitido por identidade; publicação do modelo e CUDA por dispositivo/contexto. Módulo mapeado não equivale a feature ativa |
| Descoberta | `CaptureStreamlineStartup`, `DiscoverStreamline`, eventos D3D12, callbacks de factory/resolver | Bootstrap limitado a 80 passagens de 250 ms; depois, fronteiras existentes. Resolver capturado cedo. Não existe descoberta ilimitada garantida de qualquer DLL carregada tardiamente |
| Ownership | hooks `slInit`, `slSetD3DDevice`, `slSetFeatureLoaded` | Desabilita a apresentação NVIDIA na fronteira do host; mantém estado lógico exposto ao jogo. Não altera lista de hooks Streamline a cada Present |
| DXGI | factories HWND e legada, `native_factory.cpp`, `native_swapchain.cpp` | Um output selecionado, uma proxy Intel e a interface COM externa. A fila precisa ser D3D12 direta e pertencer ao mesmo dispositivo |
| Inputs | `native_streamline.cpp::Tags/ConstantsHook`, `native_constants.hpp`, `frame_store.hpp` | Tags explícitas, viewport/frame ID, estado, formato, região e dispositivo. Não encontra recursos por tamanho ou aparência |
| Validade | `UNTIL_PRESENT` e cópia SDK `ONLY_NOW` na command list produtora | AddRef não conserva conteúdo volátil. Ausência de lista válida não autoriza cópia em outra fila nem prolongamento arbitrário |
| Latência | `native_latency.cpp`, `latency_marker_policy.hpp` | Sleep sem trava de bookkeeping durante a espera; markers reais, ordem e ID exatos. XeLL ou Reflex selecionado na inicialização; nenhuma fabricação de timestamps |
| Geração | `BeforePresent` | Precisa de ownership, opções, inputs, constantes, espera e markers aceitos para o mesmo frame/epoch. Um requisito ausente mantém passthrough |
| Pacing | SDK Intel, `vendor/sdk_pacing.cpp` | Flip metering NVIDIA quando admitido; SDK como fallback. O modelo e os contadores não medem scanout |
| ReShade | `native_overlay.cpp` | Runtime final após a interpolação; esta mudança não adiciona outro sistema de teclado ou GUI |
| Resize / perda / fim | `native_swapchain.cpp`, `native_continuity.cpp`, `native_reframework.cpp` | Invalidação por epoch, desligamento da geração, protocolo opcional de aposentadoria. Referência externa não é prova de conclusão GPU |

A retenção/reutilização de swapchain nativa com referências externas continua uma extensão delicada da integração. O [contrato Intel](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md#initialization) exige condições específicas para criar uma proxy. Não se deve generalizar sucesso sintético para qualquer composição de hooks de terceiros.

## Comparação dirigida com OptiScaler

Referência local somente para leitura: commit `c2e44a8bc911010d7ed3e4fa63843185fbb55d03` (19/10/2026). Licença GPLv3. Nenhum arquivo GPL foi incorporado ao addon MIT. A comparação considerou os contratos e as responsabilidades de cada subsistema, além das opções visíveis no painel.

| Área examinada no OptiScaler | Diferença relevante | Decisão nesta candidata |
|---|---|---|
| `hooks/Streamline_Hooks.cpp`, resolução de `slDLSSGSetOptions/GetState` | Entrega a função original ao chamador Steam e o wrapper ao jogo | Implementada separação no resolver público da ponte; corpos dessas duas funções deixam de receber nosso detour global |
| `inputs/FG/DLSSG_Dx12*`, variantes Streamline | Mais variantes de integração e metadados | Mantidos callbacks existentes; descoberta expõe capacidade por API, sem aceitar ABI desconhecida |
| `inputs/FG/XeFG_Dx12*` | Preserva contexto, XeLL e ciclo de vida da integração XeFG nativa | Nosso `backend.cpp` já rastreia contextos host/jogo, mas isso não é um adaptador nativo completo no painel ReShade. Não criar uma segunda proxy sobre a proxy do jogo |
| `inputs/FG/FSR3_Dx12_FG.cpp`, `FfxApi_Dx12_FG.cpp` | Rotas de FG existentes além de Streamline | Avaliadas como extensão independente, não como fallback de textura encontrado por heurística. Não implementadas nesta candidata |
| Camada low latency / NVAPI / XeLL | Entrada e saída de latência independentes; NVAPI direto não depende de `slReflexSleep` | Preservado Reflex/Streamline→XeLL ou Reflex já validado. Sleep NVAPI sem frame ID não será associado por suposição a um token SL |
| Representação comum de inputs / cópias | Normaliza convenções e transporte entre backends | RC3 vincula a chamada ao output/epoch da entrada e registra recibos de validação. Conversão de formatos/convenções ainda requer contrato próprio |
| `misc/GameConfigs.cpp` | Também usa perfis por jogo: restauração de root signature, spoofing, HUD-less e preservação de swapchain | Não transplantar números/perfis. Investigar a condição genérica e reproduzi-la antes de mudar nosso caminho |

A compatibilidade ampla do OptiScaler vem de múltiplos adaptadores, gerenciamento de recursos, latência e perfis acumulados. Não resulta apenas do unlock do modelo ou do flip metering. Algumas permissões de fallback daquela implementação são mais amplas que o contrato adotado aqui; não copiamos associação a frame corrente ou extensão de lifetime sem evidência.

## Falhas separadas por primeira evidência

| Caso | Evidência disponível | Conclusão e próxima discriminação |
|---|---|---|
| TLOU2 | RC1 recebia sleep após a simulação do mesmo token. RC2 corrigiu a associação ao próximo token; usuário informou que passou a interpolar | Preservar a correção e seus testes XeLL/Reflex. Não anunciar como correção nova da RC3 |
| Plague Tale | RC2 exata instalada; recursos e constantes válidos chegam. Frame 880: resources=7, markers=0, sleeping=0, failed=1 | O primeiro bloqueio observado é a admissão da latência. Ainda falta saber se não há sleep, se a associação/ordem é diferente ou se há recusa SDK. RC3 registra primeiro marker recusado, máscara observada e resultado, separadamente dos inputs de imagem. NVAPI direto é hipótese, não causa confirmada |
| Steam em Capcom | Histórico de bloqueio na aposentadoria/criação; sucesso variou com overlay/REFramework. Comparação encontrou também interferência por funções compartilhadas | Correção do resolver reproduzida com observador independente, sem código Steam. Não comprova resolução de deadlock em Steam real; referências retidas, detours de terceiros e teardown continuam caminhos distintos |
| Requiem / Onimusha, stalls | Ring de CPU não cobria execução GPU e não localizou a pausa de segundos. Usuário observou melhora com fork REFramework; houve evidência anterior de monitor reinstalando hooks | Não atribuir a XeLL ou remover waits. Preservado protocolo cooperativo V1 e detecção do monitor conhecido. Um fork desconhecido não recebe garantia. Nova candidata não altera política de pacing |
| Capcom boot | Usuário informou que RC2 abriu e interpolou nos testes recentes | Preservação como baseline informado; não certificação de boot repetido pela RC3 |
| Black Flag Resynced | Present falhou com `0x887A0005`, motivo específico `0x887A002B`; FG desligado e zero Tensor launches nesse ponto | Perda do dispositivo precede geração. Motivo é `ACCESS_DENIED`, sem comando/recurso responsável identificado. RC3 bloqueia novas admissões nesse dispositivo, conserva HRESULT e consulta metadados DRED existentes. Não recupera/recria dispositivo que pertence ao jogo |
| Death Stranding 2 | Log enviado é de build antiga; não contém motivo interno de recusa XeMFG | Não classificar como regressão RC2/RC3 nem aplicar perfil DS2 do OptiScaler sem reproduzir sua rota de inputs |
| Gears of War: E-Day | Segundo log enviado é ReShade, também antigo; dispositivos transitórios e unload/reload não estabelecem causa de ausência de interpolação | Requer relatório de capacidades da sessão atual. Sem confirmação da rota de FG/latência, não selecionar adaptador por nome do executável |

A [lista Microsoft de erros DXGI](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error) distingue perda de dispositivo e acesso negado. Os contadores de [DRED](https://learn.microsoft.com/en-us/windows/win32/direct3d12/use-dred) não identificam necessariamente a instrução que iniciou uma falha; a RC3 não habilita instrumentação global de DRED nem exporta nomes ou endereços GPU.

## Mudanças implementadas

1. **Admissão por capacidades.** `compatibility_policy.hpp` centraliza prioridades de falha. Ausência de depth, motion, constantes, espera, markers, ownership ou modo de apresentação recebe classificação própria. Disponível/inferido/confirmado/inválido não são tratados como equivalentes. Nenhuma mudança automática para DLSS-G.
2. **Isolamento do observador.** Wrappers de opções/estado são entregues ao jogo pelo resolver; um chamador identificado como módulo Steam conserva as funções originais. A seleção usa identidade do módulo chamador, não apenas uma string enviada por ele. Não desabilita Steam Overlay. Funções armazenadas pelo jogo antes da captura do resolver continuam uma limitação explícita.
3. **Lease de sessão nos inputs.** Tags e constantes capturam `Output` e epoch antes da chamada original. Retorno após resize/recriação/aposentadoria não pode alimentar outra sessão. Resultado original é conservado. Inputs descartados têm contador limitado ao processo, sem guardar conteúdo.
4. **Respeito ao modo do jogo.** Preferência antiga Force Borderless é ignorada e sanitizada; controle público removido de XeMFG. Desc, ResizeTarget e pedidos de fullscreen conservam parâmetros. Exclusivo não ativa XeMFG; mudança de modo invalida inputs em voo. DLSS-G conserva sua política própria.
5. **Falha de dispositivo.** Registra Present/motivo específico e metadados DRED limitados, sem percorrer nomes, comandos ou endereços. Suspende admissão de tags, espera, markers e status SDK no dispositivo perdido. Não transforma erro em sucesso nem repete Present.
6. **Diagnóstico exportável.** Botão `Copy compatibility report`, JSON v1 e export `MFGAXeCompatibilityReport`. Contém hashes de arquivos de módulos selecionados, geração/epoch, capacidades independentes, primeiro bloqueio, recibos de recursos, constantes e markers. Hash de arquivo em disco não prova memória carregada imutável. Buffer pequeno retorna tamanho necessário e não é escrito. Snapshot ocupado é informado sem aguardar a trava de inputs.
7. **Inspeção de módulos transitórios.** O teste inicial Debug do companion legado falhou com `module_path` durante a descoberta, antes dos hooks SDK. A enumeração consultava versões/caminhos sem uma referência temporária à DLL. `module_snapshot.hpp` agora adquire essa referência, recusa identidade diferente e ignora entradas que já desapareceram. Uma falha de caminho em módulo comprovadamente vivo continua sendo erro, não autorização para ignorar outro loader. O teste determinístico carrega uma fixture, inspeciona, descarrega e reprocessa o handle antigo, conferindo também ausência de pin permanente. A falta de dump impede provar qual módulo causou a falha original; o defeito de lifetime foi demonstrado separadamente.

O contrato de [EnumProcessModules](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-enumprocessmodules) admite mudanças na lista, e [GetModuleHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) fornece a referência usada durante a inspeção. Isso conserva a imagem atual durante a leitura; não transforma um endereço numa identidade histórica de alocação.

O relatório é construído apenas por solicitação. Hashing e JSON não executam por frame. Recibos de recursos são estruturas fixas no FrameStore; não têm pixels, nomes de recursos, caminhos pessoais ou endereços. Diagnóstico de CPU/GPU existente continua opcional. Não houve alteração de modelo, CUDA, kernels de imagem, waits, filas, limitador ou deadlines nesta candidata.

## Estratégia que continua necessária para ampliar cobertura

Cada adaptador deve provar origem, formato, convenção, dispositivo, frame/viewport, duração e sincronização antes de oferecer um `FramePacket` ao output. O selecionador deve favorecer uma integração nativa completa já existente, sem criar outra proxy; uma rota Streamline completa pode usar a ponte atual. A mera presença de DLL ou textura não é admissão.

FSR-FG/FidelityFX moderno, XeFG nativo completo e latência NVAPI direta requerem contratos e fixtures próprios. Não é seguro fazer equivalência implícita entre um ID NVAPI de 64 bits, um token SL e um contador local de Present. Entradas somente de upscaler e jogos sem FG continuam fora desta etapa por escolha do usuário. Não há trabalho Vulkan.

Outros limites mantidos: um output/viewport ativo, formatos UNORM admitidos, convenções de depth/jitter fixadas no init com defaults históricos, ausência de caminho FP16/scRGB, APIs de criação parciais e módulos carregados muito tarde. Tags legadas sem FrameToken dependem do RenderSubmit observado; isso não lhes dá uma identidade universal de frame. Alterar convenção depois de publicar a proxy exige um protocolo separado de reconfiguração; não se permite silenciosamente um frame incompatível.

## Validação e aceitação

Os recibos finais de Release/Debug, hashes e teste negativo da RC2 são registrados no manifesto local. A suíte usa processos isolados, D3D12/DXGI, ReShade e SDKs Intel reais, com caller Streamline sintético. Inclui inferência Tensor, multiplicadores, resize/recriação, variáveis de lifetime, espera em voo, markers históricos, Reflex/XeLL, UI default/opt-in e rejeições conservadoras.

Ambiente desta rodada: RTX 4070 SUPER, driver 617.42, Windows x64 e MSVC/VS 2022. A porta MFG/CUDA desta candidata tem alvo NVIDIA; o teste nesta placa não certifica outras arquiteturas ou vendors.

Novos testes exigem que um observador envie OFF sem desligar a geração do jogo, e que constantes bloqueadas no upstream atravessem resize sem contaminar a nova sessão. O exportador é analisado como JSON, verifica hashes, graus de evidência, contadores e buffer pequeno; falha do processo invalida o teste mesmo se houve PASS anterior no stdout.

Esses testes não medem FPS exibido, input-to-photon, qualidade em movimento, compatibilidade Steam real nem stalls de gameplay. Não houve nova captura comercial nesta etapa. A lista de jogos corrigidos não será ampliada a partir dos smokes.

### Evidência final

- Implementação principal: commit `77530fd`, a partir da RC2 preservada.
- Lease de inspeção de módulos e sua regressão: commit `1d8144e`.
- Primeira rodada Release, antes do ajuste de inventário: 131 testes passaram, 3 opcionais ignorados, nenhuma falha, 401,44 s. A primeira Debug teve 130 aprovados, 3 ignorados e uma falha em `xemfg_loader_startup`, com `module_path`. Essa falha e seus logs foram preservados separadamente; os binários e resultados finais são revalidados após o ajuste.
- O controle negativo usou a RC2 selada de SHA-256 `4495326c100fe70972e32376a14bff02600c333c387c898fe07f1e5727c2cc47`. Terminou com exit 1 e `observer received replacement frame state`, antes de qualquer declaração de sucesso.
- Os smokes adicionais de relatório durante um frame ativo são registrados separadamente da suíte de 134 casos. Exigem token 48 ativo (ID interno 49), capacidades confirmadas e três recibos de recursos aceitos com dimensões corretas. O teste não confunde o relatório final de FG desligado com cobertura do relatório durante inferência.
- Os três casos opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`, sem seus inputs históricos explícitos. Não foram contados como aprovados.
- Debug e os recibos adicionais devem constar aprovados no manifesto antes de o empacotador aceitar a entrega. O selo recusa binário, rollback ou resultado que não corresponda às evidências.
- Release final após o ajuste: **132 aprovados, 3 opcionais ignorados, zero falhas**, 446,32 s. Os dois smokes adicionais de relatório ativo, Release e Debug, também terminaram com exit 0 e os recibos esperados.
- Debug final após o ajuste: **132 aprovados, 3 opcionais ignorados, zero falhas**, 499,42 s. O teste de startup que falhou inicialmente passou na rodada final. Isso verifica a implementação e o cenário de teste; não prova a ausência de toda corrida de loader em jogos ou loaders diferentes.
