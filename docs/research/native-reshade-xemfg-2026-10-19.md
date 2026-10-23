# XeMFG nativo no ReShade — R1

## Correção R1.1 — ordem dos marcadores

A R1 (`a5badabeb73a11abb34f540b1ad63076e9df3f69`, addon SHA-256 `36942e1d5d66a4747df86e8a3a0fbb4f632e1932ae1152927b7fc301a27e9adc`) abriu o Cyberpunk sem ativar FG. O log mostrou modelo/contexto prontos, pedido habilitado, recursos `7` (depth/MV/HUDless), constantes válidas, sleep XeLL bem-sucedido, mas máscara de marcadores `23` no instante de Present: simulação start/end, submissão start e apresentação start. O gate exigia também submissão end (`31`) e bloqueou a geração. Os lançamentos Tensor permaneceram em zero nessa sessão.

O [contrato XeLL](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xell_developer_guide_english.md#markers) permite sobreposição das fases; cada end deve suceder o seu start, sem exigir que RenderSubmitEnd anteceda Present. A R1.1 remove essa exigência prematura do gate, mantendo a transmissão do end quando realmente recebido. Não fabrica timestamps, não remove waits e não desativa validações de inputs, sleep ou exclusividade do gerador. Contabiliza e registra os primeiros ends recebidos depois de Present para verificar a integração real.

Os quatro testes nativos foram executados novamente em 2x/3x/4x/6x e passaram. Agora enviam RenderSubmitEnd após Present nos últimos 32 frames e exigem admissão da geração e sucesso do encaminhamento tardio ao XeLL, preservando também o teste negativo de MV ausente. A suíte completa 50/3 pertence ao baseline R1; a revalidação desta mudança foi dirigida aos quatro testes afetados. Esses testes demonstraram o cenário sintético permitido; a verificação real posterior está abaixo. A R1.1 usa FileVersion `1.4.5.6`.

Os logs originais foram preservados localmente em `out/native-reshade-r1-game-check/` na raiz da workspace, separados das evidências sintéticas do pacote.

### R1.1 — ativação confirmada no Cyberpunk

O usuário informou que a cena carregou em 4x. A sessão seguinte da R1.1, com fonte binária em `b8bf0031daf12fbf9d13bc1e8012f82934fc2654`, addon SHA-256 `66384ac1f948964fc5fd1488267d021a22697f7c63b4e2b5abc8fd07222d99ac`, foi reexaminada pelos logs salvos, sem nova captura. ReShade 6.8.0.2155 foi carregado diretamente como `dxgi.dll` e registrou o addon 1.4.5.6. A instalação não contém o companion ASI ativo; os checks de startup do backend recusam um host OptiScaler 10 concorrente.

No intervalo de ticks `66197250–66343109` (145,859 s de sessão, incluindo transições), o SDK apresentou **31 amostras bem-sucedidas de 4 outputs**, além de uma amostra de cada 2, 3 e 6 outputs após mudanças de multiplicador. São amostras esparsas de status, não contagem de todos os frames nem FPS exibido. A rota CUDA foi registrada ativa em compute 8.9 com 16 kernels/17 layers; o contador de lançamentos Tensor avançou de zero a **279.764**. Há registros de deadlines em 2x/3x/4x/6x com `flip_metered=true` e configurações aceitas pelo driver (`status=0`, `feedback=0`).

Os primeiros quatro `native_late_submit_end` reais foram registrados para os IDs 450–453, todos com máscara final `63`: os seis marcadores obrigatórios foram encaminhados para esses frames. Isso confirma no jogo o cenário que a R1 bloqueava. As entradas admitidas também confirmam inputs/constantes válidos, sleep XeLL executado e desligamento confirmado do FG NVIDIA e do Reflex nativo.

Foram amostrados três resultados `-14` (inputs insuficientes) nas reentradas da geração; cada um foi seguido por sucesso na próxima amostra, cerca de um segundo depois. Eles não foram omitidos da evidência. Há hitches registrados pelo pacer; este teste não é um benchmark e não permite atribuir esses eventos à ponte ou comparar sua frequência com a X1.3.

**Conclusão sustentada:** a integração nativa executa MFG e pacing no Cyberpunk, e a correção de ativação R1.1 foi validada funcionalmente. Em seguida, o usuário confirmou que a imagem e a resposta continuam boas comparadas à X1.3 com OptiScaler. Isso fornece aprovação subjetiva inicial; equivalência visual cega, FPS comparativo, cadência efetivamente exibida e latência física não foram medidos. Não houve nova build nem mudança de configuração para produzir essa conclusão.

Evidências locais preservadas em `out/native-reshade-r1.1-validation-2026-10-19/`: sessão JSONL, ReShade.log, INI e `validation.json` (SHA-256 `1a06879a855ff03655cd8fb52c283c5491c52d97a7a56d1221477d1376a10cee`). O ZIP/manifesto selado antes da validação continua representando seu estado pré-validação; este registro posterior não reescreve os artefatos publicados localmente.

Data: 19/10/2026. Branch: `feature/1.4.5-xemfg-reshade`, derivada da X1.3 (`0d8dc0b6d9aff9aad9afaf57f6939e94fada93c5`), descendente da 1.4.5 oficial (`0fe65579d287581107babe267ee3e48c9135bf8a`). A árvore antiga modificada, a X1.3 e a release publicada foram preservadas.

## Entrega

Um `.addon64` contém os dois backends e seu painel ReShade. A escolha é exclusiva e fixada ao iniciar o processo. No modo DLSS-G, continuam as políticas da 1.4.5. No modo XeMFG, a ponte própria recebe os inputs Streamline e usa os componentes MIT já validados: modelo Intel MFG, programas adaptados, desbloqueio de multiplicador, CUDA/Tensor e pacing/metering. Não há dependência de OptiScaler ou ASI no caminho nativo. As DLLs Intel continuam necessárias.

A ponte nova foi escrita a partir dos contratos públicos Streamline, Intel e DXGI; não se importou implementação GPL do OptiScaler. Houve leitura de referência de configuração/ordem de chamadas, sem transplante de suas classes, shaders, conversores, hooks, renderer ou gerenciamento de recursos. Os componentes de dashdogy permanecem em `vendor/`, com proveniência e avisos originais. Dados Intel incorporados conservam seus termos próprios.

## Fluxo e limites de controle

```mermaid
flowchart TD
    A[ReShade carrega addon] --> B{Backend no INI}
    B -->|DLSSG| C[Integração oficial 1.4.5]
    B -->|XeMFG| D[Modelo e perfis Intel da X1.3]
    D --> E[Interceptar criação DXGI antes do swapchain]
    E --> F[XeFG proxy e wrapper COM próprio]
    G[Tags, constantes, tokens e marcadores Streamline] --> H[Ponte própria D3D12]
    H --> I[Mesma identidade para recursos, XeLL e Present]
    I --> F
    F --> J[Inferência Intel e rota Tensor NVIDIA]
    J --> K[Pacer Intel e metering existente]
    K --> L[DXGI, driver e monitor]
```

O evento ReShade `create_swapchain` não permite trocar o COM retornado; `init_swapchain` é tarde para a API Intel que recria um swapchain existente. A ponte intercepta `IDXGIFactory2::CreateSwapChainForHwnd` da fábrica observada antes de seu primeiro uso e chama `xefgSwapChainD3D12InitFromSwapChainDesc`. Uma guarda de reentrada permite a criação interna pelo SDK. Um wrapper `IDXGISwapChain4` próprio possui a identidade COM e faz a preparação antes de `Present/Present1`, sem inferir destruição pelo contador de referências de outro proxy.

O dispositivo ReShade é preservado na criação do contexto Intel para que as filas e o runtime ReShade continuem disponíveis. A identidade nativa é resolvida separadamente para validar recursos e para a rota CUDA. Um teste inicial usando somente o dispositivo desembrulhado fez o ReShade recusar o swapchain por falta de proxy D3D; isso foi corrigido antes da candidata.

O número de callbacks ReShade `present` não mede o total de frames gerados/apresentados pelo SDK. O teste observou um callback por Present da aplicação e múltiplas submissões reportadas pelo Intel. O painel e os resultados não transformam esse contador em FPS exibido.

## Contratos implementados

- Hooks de `slGetFeatureFunction` e funções de feature já resolvidas, cobrindo ponteiros cacheados; tags legadas e `slSetTagForFrame`; constantes e marcadores públicos via PCL/Reflex. Os módulos interceptados e callbacks publicados têm lifetime de processo.
- `DLSSGSetOptions` registra o pedido e encaminha uma estrutura V1 conhecida com NVIDIA FG desligado. A geração XeFG só é habilitada com confirmação desse desligamento e do Reflex nativo desligado. Não são copiados membros de uma ABI mais nova a partir de buffers antigos.
- `slReflexSleep` é substituído pelo sleep XeLL antes da simulação. O limiter solicitado é transferido; marcadores de simulação, submissão e apresentação mantêm o mesmo ID das constantes e inputs. Não se inventam marcadores de simulação dentro de Present. Sentinelas privadas de versões históricas não são adivinhadas.
- Tokens explícitos têm identidade preservada. Tags legadas são associadas ao token observado em `RenderSubmitStart`; ausência dessa associação desabilita a geração daquele frame. A primeira implementação admite um viewport/output, com cache limitado a 16 frames.
- Inputs `OnlyValidNow`/`ValidUntilEvaluate` usam o contrato Intel `ONLY_NOW` no command list produtor, que registra a cópia naquele ponto e restaura o estado informado. `ValidUntilPresent` mantém sua semântica original. Um simples AddRef não é tratado como preservação de conteúdo.
- Recursos são conferidos por identidade COM do dispositivo, tipo, amostragem, sub-região e estado. Cor HUDless/UI é encaminhada quando fornecida pelo jogo; não há novo detector de HUD ou alteração dos vetores. Recursos e constantes inválidos desabilitam FG, sem blur, frames repetidos ou alteração silenciosa dos inputs.
- A escala de movimento normalizada do Streamline é convertida para o modo de pixels Intel usando a extensão válida dos MVs. A matriz de visão é construída com posição e base de câmera; a projeção sem jitter vem das constantes. Flags de depth/jitter devem coincidir com a inicialização.
- Transições de enable/multiplicador usam o pacing já validado. Resize limpa o cache de inputs e a continuidade; o próximo frame reinicia o histórico do SDK. Formatos novos não suportados impedem geração. Destruição libera o proxy público antes do contexto Intel e o XeLL por último. Se a retirada do SDK falhar, suas dependências são retidas até o fim do processo.

## Verificação executada

Build MSVC 19.44 / VS 2022, SDK Windows 10.0.26100.0, driver NVIDIA 617.42, RTX 4070 SUPER. ReShade real 6.8.0.2155; XeFG 1.3.1.78; XeLL 1.3.0.5. Os hashes exatos, fontes e inputs efetivos de compilador/linker estão no manifesto do pacote.

`cmake --build build/tests --config Release -j 2` e `ctest --test-dir build/tests -C Release --output-on-failure`: **50 passaram, 3 opcionais ignorados, nenhum falhou** (53 cadastrados). Os ignorados são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`, dependentes de inputs/artefatos históricos adicionais; não são o teste do addon nativo.

Os quatro novos testes usam um chamador Streamline sintético, com DLL de fixture própria, mas **ReShade, D3D12, XeFG, XeLL, modelo e execução Tensor reais**. Em 2x/3x/4x/6x, verificam inferência, os setters e status do SDK, ambas APIs de tags, cópia de inputs temporários, bypass por MV ausente, recuperação, desativação, resize e contexto retirado sem leak. Também continuam passando os testes do companion anterior, incluindo publicação antes do primeiro consumidor, redirects do host e patch/restore dos providers.

As janelas de teste retornaram `DXGI_STATUS_OCCLUDED`. Em 4x, por exemplo, o SDK reportou três submissões geradas e resultado de interpolação zero; esse resultado **não comprova quatro frames exibidos**. A inferência e os lançamentos Tensor são observáveis; cadência, ausência de flicker, HDR, FPS e latência física no jogo continuam pendentes. O relato de qualidade perfeita pertence à X1.3 com outro transporte.

## Escopo inicial e riscos pendentes

O alvo inicial é NVIDIA/D3D12/Streamline, começando pelo Cyberpunk. Outputs SDR RGBA8/BGRA8 e HDR10 RGB10A2 são admitidos. Não há conversão genérica de FP16/scRGB, Vulkan, DX11, inputs NGX diretos, swapchains CoreWindow/composição, nem controle geral de múltiplos viewports/adapters. A API não permite FG em fullscreen exclusivo. Backend requer reinício; hot unload não é anunciado como seguro.

Precisam de validação real: seleção efetiva do swapchain pelo jogo, ordem/identidade dos marcadores, flags e escala dos inputs, domínio da cor HUDless em HDR/RenoDX, UI composition nativa, overlays, cadeias de proxies NVIDIA/ReShade e status/cadência após oclusão/alt-tab. Formato de storage não é prova de encoding; não foi implementada uma transformação arbitrária de cor. O novo transporte ainda não recebeu aprovação visual/perceptual do usuário nem benchmark comparativo com X1.3.

O instalador faz backup e rollback da configuração inteira afetada antes de substituir o carregador OptiScaler pelo ReShade. Preserva o RenoDX do jogo, shaders/preset e os runtimes. O pacote inclui o rollback oficial 1.4.5 e restauração exata da instalação anterior. Não houve publicação de release.

## Referências primárias

- [Guia Intel XeSS-FG, revisão fixada](https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md): criação, XeLL, inputs, estados, validade e composição.
- [Streamline 2.8](https://github.com/NVIDIA-RTX/Streamline/blob/v2.8.0/docs/ProgrammingGuide.md) e [DLSS-G](https://github.com/NVIDIA-RTX/Streamline/blob/v2.8.0/docs/ProgrammingGuideDLSS_G.md): lifecycle, tags, tokens e constantes.
- [ReShade](https://github.com/crosire/reshade): headers de eventos e implementação de fábrica/dispositivo consultados também no checkout local usado no build.
- [MFG-Unlocker MIT, revisão fixada](https://github.com/dashdogy/MFG-Unlocker/tree/a4cf2c4ab632e84c72254932793bb9e64b33d76e): componentes preservados em `vendor/` e seus avisos.
