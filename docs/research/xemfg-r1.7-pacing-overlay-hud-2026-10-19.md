# XeMFG R1.7 — pacing preexistente, overlay e HUD

19/10/2026, America/Sao_Paulo. Base R1.6 `67af1dae6859de1832b93fb473c795b3a24573d1`; branch isolada `feature/1.4.5-xemfg-pacing-overlay-hud-r1.7`. Release x64 local, sem publicação. **63 testes passaram, três opcionais foram ignorados.** Não há benchmark de exibição da R1.7 nem comprovação de HUD visual ou tecla Home em Cyberpunk nesta rodada.

## O problema é preexistente

A captura `20261019-192718-cp2077-xemfg-r16-4x-pacing-r1` é a referência para melhorar a entrega, não indício de regressão de versão. O usuário repetiu a cena e percebeu os microengasgos. Foram 4.976 registros, média 166,05 FPS exibidos, mediana 5,9735 ms, p99 10,6147 ms e máximo 14,4287 ms. Os 15 intervalos acima de 12 ms pertencem ao dwell dos frames rotulados Application. O máximo dos Intel XeSS-FG foi 7,4021 ms.

Isso localiza a cauda na passagem para o próximo grupo gerado. Não identifica sozinho uma espera removível ou defeito do modelo. A cadência CPU dos Application também variou: média 24,084 ms, p99 39,588 ms. Não substituir esse proxy por um contador de render/GPU ou latência física. Os dados e limites estão no relatório junto ao CSV em `benchmarks/`, na raiz histórica; o pacote incorpora JSON/gráfico/relatório da referência. Os hashes ligam esse dado à R1.6, nunca à candidata R1.7.

## Pesquisa e comparação de implementações

| Fonte primária / identidade | Resultado relevante | Aplicação aqui |
|---|---|---|
| [Intel XeFG guide](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md), SDK local `de0fb9c1…` | Pacer e filas internos; contrato de UI distingue interpolação/composição | Preservar ownership; criar comparação de deadlines, não outro scheduler |
| [Intel XeLL guide](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md) | Sleep/IDs/markers e exclusão de controladores concorrentes | Não portar controller Reflex independente sobre XeLL |
| [dashdogy V1.6.0](https://github.com/dashdogy/MFG-Unlocker/releases/tag/v1.6.0), fonte local `a4cf2c4ab632e84c72254932793bb9e64b33d76e` | Estimador robusto, lead/metering e correção de hitches já reaproveitados | Não anunciar de novo esses mecanismos como novidade. Detector de scene change continua estudo separado |
| [OptiScaler XeFG](https://github.com/optiscaler/OptiScaler/blob/c2e44a8bc911010d7ed3e4fa63843185fbb55d03/OptiScaler/framegen/xefg/XeFG_Dx12.cpp) | Transporte, recursos, presets/limites e políticas próprios | Referência comportamental; nenhuma implementação GPL copiada |
| [ReShade API](https://github.com/crosire/reshade/blob/main/include/reshade.hpp), instalada 6.8.0.2155 | API pública para criar/atualizar/destruir runtime em swapchain não automaticamente interceptado | Usada para o output privado do XeFG |
| [ReShade DXGI source](https://github.com/crosire/reshade/blob/main/source/dxgi/dxgi.cpp) | Sem proxy D3D12 reconhecido, o runtime automático é omitido | Corresponde ao warning do log do jogo; não tratar só como atalho configurado errado |
| [FSR3 integration](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/) | Proxy/pacing e lifecycle pertencem à integração completa | Não transplantar fila/waitable de outro fornecedor isoladamente |

As DLLs continuam XeFG 1.3.1.78 e XeLL 1.3.0.5 nos testes principais; uma rodada adicional 4x usa a alternativa admitida 1.3.2.10. Hashes acompanham os resultados. MFG NVIDIA 3x–6x permanece port experimental, além do suporte cross-vendor oficial de um frame gerado; “Intel SDK timing” nomeia a origem da aritmética, não certificação Intel dessa combinação.

## O que foi implementado

### O17 — Runtime ReShade no output final

`ReShade.log` do Cyberpunk registra `Skipping swap chain because it was created without a proxy Direct3D device`. O SDK cria uma fila privada a partir do device nativo. ReShade pode não reconhecer essa fila como sua proxy, mesmo com addon/FG ativos. A R1.6 havia validado inferência, mas não exigia runtime no output: `outputPresents=0` nos smokes não deveria ser interpretado como overlay validado.

`native_bridge.cpp::FactoryHook` agora reconhece a criação interna, com HWND coincidente. `native_overlay.cpp::WrapFinalOutput` aceita somente um objeto de sistema DXGI e fila D3D12 direct; objetos já automaticamente wrapped não recebem outro runtime. Usa `create_effect_runtime`, `update_and_present_effect_runtime` e `destroy_effect_runtime` públicos. Renderiza antes de cada Present final, após geração, sem desenhar em inputs do modelo. O device dos contextos Intel continua nativo.

O usuário confirmou que a Home deixa de abrir o ReShade inteiro. O wrapper possui refs COM, runtime e a mesma fila do swapchain; destrói o runtime antes de resize e o recria após a tentativa, inclusive falha de resize. Libera antes de destruir output. O mutex do runtime é liberado antes das chamadas DXGI nativas: Present pode esperar a thread da janela; a ordenação externa Present/Resize continua pertencendo ao SDK. Encaminha sync/flags/HDR e usa `set_color_space` público nos espaços conhecidos. Não consome waitable nem modifica hotkey/INI para simular uma correção. Diagnostics informa o vínculo; counters técnicos ficam só no relatório copiado.

Testes reais exigem criação, updates, resize/recriação e número de runtimes criados igual ao destruído; passaram em 2x/3x/4x/6x. A API ReShade pode adicionar trabalho de efeitos/overlay que antes estava faltando; isso precisa entrar no benchmark, não ser descrito como otimização grátis. Entrada Home/foco/alt-tab real e HDR visual ainda precisam teste manual. Trava do wrapper não substitui contrato de lifecycle externo; criação/resize concorrentes arbitrários continuam fora da cobertura.

### P17 — Comparação opcional com os deadlines originais XeFG

O núcleo atual já usa o port de pacing estudado. Portanto a nova opção não tenta repetir a mesma correção ou empilhar outro pacer. `General > Advanced pacing comparison > Use Intel SDK timing (test)` vem **off**. Em on, o hook conserva a aritmética original de deadline do provider e desativa metering NVIDIA naquele grupo. Preserva o hook necessário aos outputs extras, fences, wait helper, quantidade de frames e recursos.

`sdk_pacing::RequestSdkDeadlines` só enfileira preferência. O presenter a aplica no primeiro output de um batch, antes de qualquer deadline desse batch; não alterna no meio do grupo. Voltar a off restaura o caminho adaptativo/metering conforme a preferência original do usuário. A opção de metering fica indisponível enquanto essa comparação está selecionada, sem apagar sua preferência. Estado requested/active e logs de política ficam explícitos. `[XeFG] SdkDeadlines=0/1` persiste; ABI de controles versão 3 recusa clientes antigos que interpretariam padding como opção.

Isso é **experimento de seleção de caminho, não ganho demonstrado**. Nos smokes o pedido e a restauração da política foram observados; a janela ocluída pode não entrar no corpo dos deadlines. O resultado JSON distingue zero observações de deadline (`null`, não aprovação). Quando houver registro em jogo, `sdk_deadlines=true` deve manter `deadline_ns == sdk_deadline_ns` e `flip_metered=false`. Não se anunciou validação de scheduling/scanout a partir de inferência bem-sucedida.

### H17 — Guard de inputs opcionais sem forçar UI Composition

Intel documenta que fornecer HUD-less/UI compatíveis também pode ajudar **interpolação**, com composição desligada. Por isso não foram descartados todos esses recursos por “economia de cópia”. O princípio reaproveitado de `quality_guard.hpp` do DLSS-G é validar o input opcional, mantendo cor/depth/MV requeridos. Regras NVIDIA específicas não foram transportadas cegamente: R10G10B10A2 de dois bits de alpha é permitido no caminho HDR Intel.

`hud_inputs.hpp::CheckHudInput` / `native_streamline.cpp::Tags` verificam formato compatível com output, dimensão/full output extent, bounds, sample count, array e shader-read. Um opcional inválido ainda não submetido é omitido e mantém interpolação do backbuffer; não desliga FG só por esse opcional. Se um tag válido do mesmo tipo já entrou no SDK e depois é invalidado, a ponte não finge conseguir retirá-lo: bypass desse batch. Erro de API SDK continua causando bypass. Inputs requeridos mantêm suas validações/fallback anteriores.

Estado simples em Quality e contadores/motivos no relatório copiado. Logs de metadata ocorrem na primeira observação de cada tipo, incluindo `encoding_verified=false`; não chamam formato igual de prova de pixel correto. O guard desta ponte considera HUD do tamanho do output completo; integrações de HUD parcial/letterboxing precisam perfil próprio. Não altera cores, alpha, motion vectors, depth, pesos ou modelos. Não adiciona blur, histórico ou passes GPU.

Teste de inferência envia UI em formato de MV: o guard filtra e conserva FG com composição off. Outro invalida HUD-less após tag aceito: o batch é bypassado com segurança. Teste focado cobre HDR Intel aceito, formato incompatível, extents/overflow, multisample/array e DENY_SHADER_RESOURCE. A prioridade relatada pelo usuário é a mira/crosshair deformando ou duplicando. Os testes comprovam os caminhos de input, **não melhora de crosshair já válido**. Se os inputs do jogo já forem corretos, o guard não promete mudar a imagem. UIAlpha isolado não é reinterpretado como textura colorida XeFG; criar essa conversão exige dataflow/color space/lifetime demonstrados.

## O que transferir do DLSS-G

| Mecanismo do repositório | Decisão |
|---|---|
| Estimador/pacer de seis amostras, metering/lead | Já presente. Sem nova espera/cap/fila por suposição |
| Quality Guard e validação de inputs | Princípio aplicado em H17, com regras Intel e fallback por frame |
| `hdr_compat.hpp` / UI recomposition | Não forçado; usuário pediu alternativas à composição |
| Reflex/Pacing Lab, waitable | Não ligado sobre XeLL; ownership e compatibilidade não demonstrados |
| Epoch/lifetime e confirmações | Mantidos; política nova diferencia pedido de aplicação |
| Cubins/SASS/heurísticas de blend NVIDIA | Não são kernels XeFG. Sem transplante de números de registradores/thresholds |
| Pesquisa temporal TGL/LMR | Não ativada; benefício anterior não demonstrado e outro modelo/backend |

## Validação e pendências

Build MSVC Release x64 passou. CTest: **63 passed / três skipped / zero failed**, 49,65 s na rodada final. Skips opcionais históricos: provider_identity, engineering_manifest, release_1_4_5_manifest. Real SDK 2x/3x/4x/6x exercita inferência, Tensor, XeLL, tags/lifetimes, UI opt-in/off/ausente, dois casos de HUD inválido, mudança/restauração de política e lifecycle do runtime final. UI Composition continua desmarcada por padrão. Widgets reais foram renderizados com status sintético; não são captura de gameplay.

Falhas intermediárias preservadas: inserção de um novo `if` no harness mudou a associação do `else` e fez o caso de MV ausente exigir FG ativo; corrigido o fluxo do teste. Depois, o helper exigia observação de deadline mesmo na janela ocluída: agora registra ausência separadamente e exige ativação/restauração, sem declarar passthrough não observado. Não se mascarou violação de deadline: um registro observado com valor diferente continua falhando.

Não houve nova captura/instalação no jogo durante o desenvolvimento desta build. A coleta R1.6 permanece identificada. Antes de alegar melhora: mesma cena/4x/base FPS/configuração, três pares alternados de 30 s com opção off/on; overlay fechado, aquecimento, profiler separado, p95/p99/p99.9 de display e latência física/feeling comparados. Critérios e scripts da R1.6 continuam aplicáveis, agora ligando os dois estados à mesma R1.7. Não testar regressão de versão como se ela fosse a hipótese do usuário.

Pendências anteriores permanecem: idle GPU ao mudar SleepMode, lifecycle concorrente/multiadapter/device loss, dependência da qualidade dos markers do motor e cobertura de execução GPU. Novo SDK timing não as resolve. Se a comparação piorar cadence, throughput ou resposta, voltar a off. Se HUD mudar para pior, retornar à R1.6 guardada e revisar metadata/pixels; não compensar com blur. Se o overlay falhar, guardar ReShade.log e log XeMFG: runtime ligado não comprova foco de teclado correto.

## Entrega e rollback

ProductVersion `1.4.5-XeMFG-R1.7`, FileVersion `1.4.5.12`. Hash do addon e commit final no manifesto do pacote. Inclui rollback R1.6 e oficial 1.4.5; nenhum runtime Intel/OptiScaler/ASI é incluído. MIT da ponte/componentes mantida; payloads Intel conservam termos próprios. Substituir addon com jogo fechado e manter uma só cópia. Sem hot reload ou publicação automática.
