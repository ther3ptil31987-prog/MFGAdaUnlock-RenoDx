# XeMFG R1.11 — depth, viewport e continuidade da apresentação

20 de outubro de 2026. Base: R1.10 (`95c94a8`), branch isolada `feature/1.4.5-xemfg-compat-r1.11`. A árvore antiga modificada, a release 1.4.5 e os pacotes selados anteriores são preservados. Esta candidata trata falhas de integração; não anuncia os jogos como corrigidos sem nova evidência de gameplay.

## Evidência que orientou a mudança

| Caso | Primeira falha observável | Intervenção |
|---|---|---|
| TLOU2 | Constantes reais nos PIDs 31668/8752/11768/33288/8612/2228 têm `depthInverted=0`, mas a inicialização padrão era invertida. `issues=8`. Há recursos aceitos, porém nenhum `native_final_overlay` nesses trechos. | Default de depth normal, com validação mantida, e adaptador de factory que liga explicitamente o runtime final. Marcadores/identidade de frames ainda devem ser conferidos na cena. |
| Alan Wake 2, PID 28732 | `depthInverted=0`, `issues=8`; também sem runtime final. O log selecionou viewport 1 num pedido Off; pedidos de outra viewport podiam ser recusados antes de serem registrados. O usuário confirma FG ligado no menu. | Default normal; Off não seleciona viewport; pedidos de viewport são registrados de forma limitada. A causa completa da ausência de interpolação ainda depende do novo log. |
| Steam / Requiem, PID 19340 | Primeira saída inicializa; ao retirá-la restam duas referências nativas. Seguem 164 recusas Intel `-17`, com erro DXGI `0x80070005`, antes do gameplay. | Preservar uma referência própria e reconfigurar os buffers da mesma saída para a nova fila Intel, quando ownership/layout/dispositivo permitem. Não liberar referências de Steam nem mudar sua configuração. |
| Requiem, PID 16080 e ring fornecido | Inferência ativa. No ring de 327 amostras: intervalo de source Present máximo 192,934 ms; chamada SDK máxima 11,9232 ms; espera de latência máxima 18,678 ms; nenhuma recusa/bypass. O usuário informa que houve uma trava de segundos na rodada. | Instrumentar também a fila final de apresentação Intel e calibrar clocks GPU/CPU. O ring não localiza os segundos percebidos nem permite atribuí-los a XeLL, modelo ou metering. |

Os logs foram preservados em `out/xemfg-r1.11-evidence/baseline/`, com hashes de addon, runtimes, interposer, ReShade e configurações. A pasta é local/ignorada. Logs anexados contêm launchers, processos de crash/video e jogos: a seleção de PID e emissão é obrigatória. O último PID do arquivo nem sempre executa o jogo.

## Correções implementadas

### Depth e seleção de viewport

`native_game_defaults.hpp` usa os nomes observados `tlou-ii.exe`, `tlou-ii-l.exe` e `AlanWake2.exe` para inicializar XeFG sem a flag de depth invertido. Uma opção explícita `XeFG/InvertedDepth` mantém prioridade. A validação das constantes continua recusando convenções diferentes: o perfil não autoriza aceitar dados incompatíveis. Não há reescrita de depth, MV ou pixels.

Um pedido Off inicial não pode escolher o dono da feature. A viewport é selecionada pelo primeiro pedido habilitado. Off de outra viewport é encaminhado para manter NVIDIA desligado, mas não muda o estado lógico da viewport escolhida. Tags/constantes só são consumidas para o dono selecionado. Outra viewport que tenta ligar simultaneamente continua recusada; esta ponte ainda possui uma única feature de saída.

O teste de viewport secundária envia Off na viewport 1 antes de ligar a viewport 0 e repete Off da secundária durante inferência. Isso reproduz uma lacuna real da R1.10; ainda não prova que todos os pedidos do Alan Wake 2 sigam essa mesma ordem.

### Runtime final do ReShade independente da factory externa

`native_factory` é um adaptador COM próprio de `IDXGIFactory2`, baseado na API pública DXGI. Ele envolve a factory nativa obtida pelos contratos públicos de unwrapping. Cada criação HWND feita pelo SDK passa explicitamente pela ligação do renderer final, inclusive numa thread diferente ou com um entrypoint diferente do detour inicial.

O requisito antigo de que a factory nativa tivesse a mesma entrada de vtable do hook de bootstrap foi removido: essa restrição podia conservar uma factory Streamline externa, receber outra proxy e deixar o renderer final sem uma swapchain nativa admitida. A factory adaptada vive enquanto o contexto Intel vive; uma falha de destruição conserva também essa dependência.

O escopo de criação restaura o estado anterior de reentrada, em vez de zerá-lo sempre. O ReShade continua dono de seus atalhos e captura de input. Não foram adicionados hooks de Home nem renderer de GUI externo.

### Continuidade quando Steam retém a saída nativa

Com `gameoverlayrenderer64.dll` presente, a retirada de um output pode manter **uma referência própria** à swapchain nativa, somente se ainda houver outros donos. Não há `Release` de referência desconhecida. O cache é limitado a duas janelas e usa uma propriedade por HWND para recusar reutilização após destruição/reuso do handle.

A saída só fica elegível depois que `xefgSwapChainDestroy` retorna sucesso. Uma recusa conserva/quarentena dependências e bloqueia novas criações Intel até reiniciar o processo. Isso evita reutilizar uma saída de um contexto cuja retirada não foi confirmada.

Na próxima criação, se ainda houver donos externos, é exigido o mesmo dispositivo, layout compatível e fila direta. `ResizeBuffers1` associa os novos buffers à **nova fila de apresentação do SDK**, usando masks e queues por buffer. Não se continua apresentando numa fila privada já aposentada. Mudanças incompatíveis ou buffers ainda referenciados retornam erro; não há loop para forçar liberação. Se os donos externos já saíram, a referência do cache é liberada e uma swapchain nova é criada normalmente.

O SDK continua criando/gerenciando seu proxy, seus buffers, fences e metering. Esta mudança adapta a saída nativa dentro da chamada de factory do SDK. Não empilha dois geradores de frames. Releases COM do cache são feitos fora de sua trava, para permitir callbacks de overlays sem inversão de lock.

O teste opt-in `Compatibility/RetainNativeOutput=1` simula o dono externo; essa chave não é uma opção pública de qualidade. A política automática usa apenas presença do módulo Steam, sem alterar configurações do usuário. UI Composition permanece opcional.

## Diagnóstico das travadas

Com `StallTrace=1` e `GpuTiming=1`, há observação separada da fila de origem e da fila de apresentação Intel. Os eventos incluem label da fila, fence própria, timestamps e calibração QPC/GPU. Timestamps da fila final não são scanout nem duração isolada do modelo; servem para localizar progresso/atraso da fila que realmente recebe a saída.

Arenas diagnósticas ainda em voo são conservadas e coletadas quando uma nova saída constata sua conclusão. Quatro arenas não resolvidas desabilitam novas medições GPU. Nenhum wait diagnóstico foi adicionado. Instrumentação permanece desligada por padrão e não deve acompanhar benchmarks finais.

Também são amostradas chamadas Streamline, com ID, viewport, thread e tipo de marker. Isso permite investigar os registros em que constantes/recursos e markers aparecem em IDs diferentes. Não foi criado um join temporal por proximidade nem foram fabricados markers para aprovar a feature.

O ring fornecido cobre cerca de nove segundos de intervalos acumulados. Apesar do relato de travada na mesma rodada, ele não contém um intervalo CPU de segundos. Pode haver diferença entre trecho retido, trabalho assíncrono e o que foi exibido. Não é evidência suficiente para remover waits, alterar deadlines ou atribuir culpa ao XeLL. O novo rastro deve atravessar a próxima ocorrência real.

## Validação e limites

Os casos novos usam ReShade, driver e SDK Intel reais em processos isolados: depth normal, viewport secundária e recriação com duas referências nativas de um observador. O último exercita inferência, resize e teardown após rebind, preservando as referências do observador. Testes anteriores de proteção de referências externas, markers, Reflex/XeLL, shutdown, render final, CUDA e políticas continuam na suíte.

CTest final: **115 casos, 112 aprovados, três opcionais ignorados, zero falhas; 276,69 s**. Os opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`. O addon testado tem SHA-256 `1db596cf140ede231b35026be4341832eead893cb2d1935108d7e5c533c13374`. O relatório CTest e os recibos ficam no pacote. Controles negativos executam o mesmo emissor com a R1.10 selada; falhas esperadas não são contadas como testes positivos da candidata. DLLs derivadas de jogos num emissor sintético não substituem a execução do jogo.

No boot real do Requiem, PID **21708**, o módulo `gameoverlayrenderer64.dll` estava carregado. A primeira saída foi retirada com duas referências nativas restantes e `xefgSwapChainDestroy=0`; a segunda criação reutilizou a saída com `native_output_reused.result=0`. Houve duas inicializações Intel e dois runtimes finais ReShade, sem a sequência anterior de erros `-17`/`0x80070005`. Isso confirma a recuperação dessa transição de boot com Steam presente; não valida abrir o overlay, gameplay prolongado ou pacing exibido. O processo foi encerrado automaticamente pelo teste. O addon foi carregado de `_storage_`; seu hash foi conferido após o teste e a cópia sincronizada com o backup restaurado.

No TLOU2, PID **19980**, o módulo carregado tinha o hash da candidata, com ReShade 6.8 e hooks de markers instalados. O teste curto não observou criação da swapchain; portanto não confirma renderer final nem interpolação. Não houve controle da UI do jogo nesta etapa.

No Alan Wake 2, o novo trecho do PID **23220** registra `native_final_overlay`, inicialização Intel e Presents em bypass enquanto a viewport 1 solicita Off. Isso demonstra que a criação alcançou o renderer final nesse trecho, mas não valida um pedido On nem interpolação. O processo já não estava presente na amostragem final do helper; não há snapshot de módulos desse PID nem evidência suficiente para atribuir a saída a crash, launcher ou intervenção externa. O teste de boot é **inconclusivo**, e o lançamento normal pelo Epic e uma cena jogável continuam necessários.

Ainda precisam de confirmação de runtime: overlay final/ativação no TLOU2, pedido habilitado e inputs correspondentes no Alan Wake 2, interação do overlay e gameplay Capcom com Steam ativo, e correlação de uma trava de segundos no Requiem. Nenhuma melhora de FPS, latência física ou pacing exibido foi medida nesta etapa. Sem evidência dessa rodada, esses itens permanecem não verificados.

## Entrega, captura e rollback

Addon R1.11, símbolos, manifesto, recibos e rollback R1.10/1.4.5. Sem publicação automática. Instalar só com o jogo fechado, em instalação nativa do ReShade; preservar RenoDX, configurações e o par Intel admitido. O caminho DLSS-G da 1.4.5 permanece na base. Onimusha com loader OptiScaler requer uma rodada separada antes de atribuir suas pausas à ponte nativa.

Para uma captura curta de Requiem/TLOU2/Alan Wake 2, editar a seção existente:

```ini
[Diagnostics]
StallTrace=1
GpuTiming=1
```

Desligar ambos após preservar a ocorrência. Analisar o PID do processo do jogo:

```powershell
python tools/analyze_xemfg_runtime.py "caminho/MFGAdaUnlock-XeMFG.log" --pid 1234 --output runtime-summary.json
```

Rollback: fechar o processo e restaurar o snapshot da instalação ou um único addon da pasta `rollback`. A substituição do addon não muda silenciosamente configurações do Steam nem troca o loader de outra camada.

Referências de contrato: [DXGI ResizeBuffers1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiswapchain3-resizebuffers1), [Intel XeFG](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md), [Streamline DLSS-G](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md). Implementação MIT própria; nenhuma implementação GPL do OptiScaler foi incorporada.
