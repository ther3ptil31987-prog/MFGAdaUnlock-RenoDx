# Implementação experimental de engenharia sobre a release 1.4.4

Data da investigação: 17/10/2026. Baseline: `3e898157fd59ed9eef65c181619060ddb305c6bd`. Branch de implementação: `experimental/1.4.4-engineering-20261017`, em worktree isolado. Este documento acompanha uma DLL compilada, testes e manifesto; não é uma nova release estável.

## Resultado e limite da entrega

**Foram implementadas correções de identidade de conteúdo, rollback, duração de alocações, tratamento de falhas dos hooks, semântica de métricas e confirmação dos trials.** Também foi removida uma alocação de 64 KiB do caminho comum de notificação do loader. Os perfis visuais e os payloads gerados da 1.4.4 foram preservados.

A suíte passou em **40/40 testes**, incluindo os 32 da baseline e oito novos. Passaram 21 roundtrips de providers, dez compilações de blends completos e dois harnesses numéricos executados na GPU. Esses resultados são offline. **Não há ganho de FPS, pacing exibido, qualidade em movimento ou latência física demonstrado para esta build.** Não foi instalado nenhum addon no jogo nem alterada sua configuração.

O backlog inteiro foi classificado na seção seguinte. Os itens dependentes de execução do runtime, recursos reais e capturas não foram apresentados como concluídos. O jogo estava fechado durante esta etapa. O acesso a CUDA permitiu executar fragmentos com buffers próprios; ele não fornece os recursos, fences, fases e parâmetros privados necessários para reproduzir com segurança um dispatch completo de DLSS-G.

Os benchmarks antigos continuam sendo da **release 1.4.4**, SHA-256 `5a193d86c6f60a7f0f275799d66eef7f14bd0946cdbefd48a3237506c747d5cd`. A análise antiga de S1 e seus 18 testes pertence à outra branch; não é validação desta entrega. O [relatório original](ada-dlssg-engineering-investigation-2026-10-17.md) permanece como histórico identificado. A implementação aqui parte de S2, sem portar a árvore de pesquisa por cima da release.

## Estado de cada item

“Implementado” descreve a mudança de código e seus testes, sem prometer ocorrência anterior de crash ou benefício em jogo. Onde há trabalho residual, ele está explícito. A versão estruturada é [engineering-backlog-status-2026-10-17.json](engineering-backlog-status-2026-10-17.json).

| ID | Estado | Trabalho e evidência | Limite ou dependência restante |
|---|---|---|---|
| B01 | Implementado | Manifesto experimental distingue baseline, commit, arquivos, três headers gerados, payloads, perfis, flags, headers/bibliotecas externos efetivamente lidos pelo MSVC e DLL. Captura pode receber manifesto e exige hash correspondente a addon mapeado. Teste com artefato real recusa rótulo estável e flags ausentes | Módulo mapeado não prova provider/feature ativo. `RuntimeIdentity` permanece desconhecido até confirmação independente |
| M01 | Implementado | Schema 2 registra coluna, unidade, definição, população, classificador, coletor e domínio. Comparator bloqueia aliases diferentes, latência/GPU entre caminhos e mudança de PresentMode | Dados antigos precisam de nova análise; não ganharam identidade retroativamente |
| R01 | Implementado | Identidade do cubin original completo, tamanho, ELF/arquitetura e footprint antes do replacement. Sete hashes originais conhecidos; 18 instâncias de kernels examinadas nos inputs locais | FNV é identificação de compatibilidade, não autenticação. Provider desconhecido recebe fallback conservador |
| R02 | Implementado parcialmente | Dez perfis binários exatos, com fronteiras verificadas por disassembly; descoberta separada de escrita; preflight e rollback por conjunto. Elimina scanners genéricos dos writers em runtime | Transação de memória não prova quiescência de execução. Patches de 7 bytes e vínculo ao adapter/feature ainda precisam da fronteira do host/runtime; não se anuncia hot patch arbitrário seguro |
| R03 | Implementado | Contagem 1..32 antes da escrita; rollback comum em geometria/midpoint/redirect Blackwell; alocação retida enquanto houver referência pendente. Endereço inválido não é lido antes da validação de acesso | Falha/conflicto pode reter memória até o processo terminar. Isso é preferível a liberar memória ainda referenciada |
| R04 | Implementado parcialmente | Sessões de módulo com geração, PE e código normalizado para ASLR; ownership dos próprios patches; referências retidas e limitadas a 64 módulos | Não há identidade universal de feature/dispositivo/recurso, nem aposentadoria quente provada. A lease impede unload/reuso; não equivale a demonstrar reload real no mesmo endereço |
| R05 | Implementado parcialmente | Enumeração/abertura/registro/attach/commit/detach falhos não são sucesso. Trampolines e estado sobrevivem ao detach fracassado. Fault injection automatizada | Snapshot não impede toda criação posterior de thread. Quiescência global e unload antes do primeiro Present continuam dependentes do host. Após primeiro Present observado, o addon experimental fica residente até sair do processo |
| L01 | Implementado | QueueTrial espera setter confirmado e janela nova; desconhecido difere de cap zero. Cap/epoch/função são lidos sob try-lock consistente. Trials de multiplicador descartam janelas anteriores e sobrepostas | Confirmação da API não é medição física do limiter. Owner ausente/erro fica pendente; não se chama setter em thread arbitrária |
| L02 | Bloqueado por dependência externa | Preservadas as esperas NVIDIA; reusada a conclusão limitada das capturas antigas: caminhos distintos, timeline GPU do jogo ausente | Precisa capturar recurso/slot/fence, produção, execução FG e exibição na mesma sessão. Sem isso não se remove espera nem aumenta ring |
| L03 | Bloqueado por dependência externa | Mantido loop do plugin. Rastros antigos mostram CPU adicional, sem erro por deadline correlacionado | QPC/deadline/fence e oversleep precisam de observação no hash exato em execução. Um benchmark genérico de Sleep não mede esse loop |
| P01 | Bloqueado para otimização de desempenho | Dez blends completos da 1.4.4/ablações compilados para Ada, atributos e hashes registrados; nenhuma fórmula alterada | Duração por dispatch, shape real, stalls e caminho crítico ausentes. Reduzir PTX/registradores sem isso não seria otimização demonstrada |
| P02 | Implementado | Buffer local de 512 wchar no Notify comum; fallback longo preservado. 10.000 chamadas aquecidas sem alocação C++ de heap; nomes/OTA/reentrada cobertos | Não mede todas as alocações internas do Windows nem ganho de FPS. Telemetria NVAPI/VRAM não foi movida sem medir custo |
| P03 | Bloqueado por dependência externa | Política de ownership/cópias/retention preservada; testes de memória existentes passaram | Precisa de alloc/free/cópias/residência e consumidor real em ciclos de FG/resize. Não há diagnóstico de leak ou cópia dispensável nesta entrega |
| Q01 | Bloqueado para conclusão visual | Harness permite V3, sem fotometria, sem border direcional, V2 e base; roundtrips verificam seleção/fallback offline | Ablação estrutural/JIT não substitui vídeo cego em movimento, identificação de frames gerados e custo em jogo |
| Q02 | Validado sem mudança algorítmica | Fragmento V3 exato contra oracle FP32: 16.384 casos, 32.768 saídas GPU e zero erros acima da tolerância; anchors, simetria e casos não finitos no teste CPU | Não valida domínio de cor/encoding do jogo, HDR perceptual, ghosting nem sombras. Não há justificativa para outro threshold ou normalização |
| Q03 | Bloqueado por dependência externa | Diagnóstico e testes existentes de constantes/tags preservados | Precisam-se pixels de MV/depth, jitter e frame/fase correlacionados. Tags finitas não provam conteúdo correto; não houve reescrita de inputs |
| X01 | Bloqueado para ativação | Backend temporal já existente continua excluído pelas flags de release; testes de sua política e unidade passaram | Recurso/geração, contexto CUDA, ordenação e resets em execução precisam ser demonstrados antes de habilitar histórico. Não foi criado outro backend |
| X02 | Bloqueado para pacer alternativo | Pacer/Reflex nativos e laboratório existente preservados; nenhum segundo consumer de waitable | Falta interface identificada e ownership exclusivo. Port direto Intel→DLSS-G e polling GPU como display engine foram rejeitados como implementação sem suporte demonstrado |

## Mudanças e fallback

### Conteúdo e perfis de instrução

[kernel_identities.hpp](../../src/addons/mfgunlock/kernel_identities.hpp) autoriza somente os cubins originais inspecionados. O hash cobre também nomes, parâmetros e metadados; não apenas `.text`. As variantes existentes continuam com seus próprios gates. Os headers gerados foram copiados da release local conferida, sem reconstrução dos payloads NVIDIA.

[binary_profiles.json](../../src/addons/mfgunlock/binary_profiles.json) e o header correspondente contêm identidades de código executável e os sites exatos dos gates, Legacy e ceiling. A ferramenta [build_compatibility_profiles.py](../../tools/build_compatibility_profiles.py) usa inputs explícitos e `dumpbin /DISASM:NOBYTES`. Não exporta modelos nem cubins originais.

Inputs conferidos: NGX vendor 310.9.1 e 310.9.16; quatro NGX OTA locais (incluindo 310.9.0 e outra emissão de 310.9.16); plugins Streamline 2.8 e 2.14.16. São oito imagens PE e dez perfis de patch por finalidade. Variantes com a mesma versão de arquivo podem ter código diferente e permanecem separadas.

O Legacy possui perfis para **os plugins 2.8 e 2.14.16 examinados**. Não foi encontrado input local 2.14.1 para gerar um perfil exato. Uma versão antes alcançada apenas pela busca ampla, mas sem identidade validada, agora é recusada por esse writer; o caminho nativo permanece. Esta restrição de suporte é intencional e precisa aparecer no teste de compatibilidade, sem ser descrita como equivalência a todo comportamento antigo.

Os patches de arquitetura e Legacy com immediate de um byte continuam escrevendo somente esse byte após conferir a instrução completa. Os outros sites preservam a transformação já existente na baseline. A transação assegura preflight, ownership e recuperação; **não torna uma sequência multibyte atômica nem resolve loader lock ou execução concorrente**. Essa parte de R02 continua aberta. Não se adicionou um patch de sincronização nem nova escrita periódica em Present.

### Alocações, sessão e teardown

[patch_transaction.hpp](../../src/addons/mfgunlock/patch_transaction.hpp) e [pointer_redirect.hpp](../../src/addons/mfgunlock/pointer_redirect.hpp) compartilham o contrato de aplicação/restauração. Falha em restaurar ou bytes pertencentes a outro writer mantêm o registro pendente e a alocação. Só se libera quando todas as referências foram retiradas. `VirtualQuery` recusa páginas não legíveis; a lease do módulo continua necessária para evitar corrida com unload.

O caminho normal de restauração e os ramos excepcionais usam a mesma regra. Foram testados 0/1/32/33 descritores, falhas por site, cópia fracassada, rollback fracassado, ownership externo e ponteiro inválido. Não há prova de crash anterior em jogo nem alegação de redução de VRAM.

[module_session.hpp](../../src/addons/mfgunlock/module_session.hpp) mantém uma referência por módulo e geração. O hash de código desfaz relocations DIR64 e reconhece apenas mudanças próprias registradas. Uma mudança externa não é normalizada como se fosse nossa. Isso impede restaurar um endereço reutilizado porque o módulo não é liberado enquanto pode ser referenciado; não implementa hot reload seguro.

**A build experimental exige encerrar completamente o jogo para trocar DLL, perfil binário ou provider.** As leases não são liberadas no meio da sessão. Após o primeiro Present observado, o addon também é fixado no processo para manter destinos de hooks válidos. Esse compromisso pode impedir unload/OTA em vida e não substitui um protocolo completo de drenagem. A build não deve ser promovida como estável com suporte a hot unload.

### Trials, métricas e custo de CPU

O setter Reflex só publica uma confirmação depois de sucesso; retry nativo confirma cap privado zero, falha total deixa estado desconhecido. O leitor usa try-lock, sem bloquear Present. Epoch ou função diferente invalida a confirmação. As janelas de candidato começam depois da confirmação e não reaproveitam frames de uma janela anterior. Timeout solicita restauração e aguarda sua confirmação.

O analyzer conserva os nomes históricos para leitura, mas acompanha sua definição no schema 2. [Compare-PresentMon.ps1](../../src/addons/mfgdiagnostics/Compare-PresentMon.ps1) evita produzir deltas entre latências ou tempos de GPU com endpoints distintos. Mudança de PresentMode interrompe a comparação automática. Isso não resolve causalidade: pares repetidos e domínio de exibição continuam necessários.

O único custo removido com comprovação direta nesta etapa é a alocação do buffer amplo em notificações comuns do loader. O buffer curto é local à chamada, inclusive na reentrada; nomes longos continuam usando fallback. Não houve cache permanente de paths ou alteração de frequência dos observers.

## Validação executada

Ambiente: Windows x64, VS 2022/MSVC 19.44, SDK 10.0.26100, Python 3.13.15, RTX 4070 SUPER, driver 617.42. O pacote inclui logs/JSONs e SHA-256 de seus arquivos; paths completos e versões dos inputs constam da evidência local. O manifesto liga a DLL ao commit, flags e hashes reais.

| Verificação | Resultado | Interpretação |
|---|---|---|
| Build Release da suíte e da DLL experimental | Sucesso | DLL x64 compilada com política Local Stable/V2 Compatibility da 1.4.4 |
| CTest | 40/40, nenhum skip na execução local | Inclui mutações, fault injection, regressões de release, acknowledgment e semântica |
| Identidade e perfis | 18 instâncias de cubins originais mutadas; dez perfis apply/restore | Cobertura dos oito inputs explicitamente fornecidos; não é teste de jogo |
| Provider roundtrip | 21/21; 310.9.0/.1/.16 × sete modos | Imagem PE inteira idêntica após restore; nenhuma feature NGX criada |
| JIT de blends completos | 10/10 | Dois providers × V3, duas ablações, V2 e base; sem dispatch completo |
| Fotometria GPU | 16.384 casos; 32.768 saídas; erro máximo absoluto `4,82e-7`, tolerância `1e-4` | Fragmento exato contra oracle FP32; não preferência visual |
| Border GPU | 25.796 casos × duas direções; erro máximo `3,62e-7` px, tolerância `0,003` px | Fragmento exato; continuidade/cantos/simetria/anchor cobertos |
| Loader | 10.000 Notify comuns aquecidos, zero alocações C++ de heap | Contagem determinística do processo de teste; não benchmark de gameplay |
| Preservação | Hashes conferidos no encerramento e registrados no pacote | Working tree original e seus arquivos não são substituídos pela implementação |

O teste antigo de runtime hooks expôs uma regressão durante desenvolvimento: construção do restore lia um endereço inválido antes da proteção. Foi corrigida e coberta por teste de endereço inválido e pela suite completa. A primeira execução do harness de shader também recusou âncoras devido a CRLF introduzido pelo emissor auxiliar; normalização LF foi corrigida no harness. As falhas não foram ignoradas nem convertidas em aprovação.

### Recursos dos blends completos

| Modo | Text bytes 310.9.1 / 310.9.16 | Registros .1 / .16 | Shared / local bytes |
|---|---:|---:|---:|
| V3 | 17.152 / 17.152 | 48 / 48 | 288 / 0 |
| V3 sem fotometria | 16.384 / 16.384 | 48 / 48 | 288 / 0 |
| V3 sem border direcional | 15.232 / 15.232 | 48 / 50 | 288 / 0 |
| V2 | 14.464 / 14.464 | 48 / 48 | 288 / 0 |
| Base | 12.928 / 12.928 | 48 / 46 | 288 / 0 |

São emissões C++ completas desta base, driver JIT para `sm_89`. O máximo de threads reportado é 256, sem revelar launch shape real. Zero memória local e 48 registros não são prova de ocupação alcançada, ausência de stalls ou ganho de FPS. A ablação que remove um componente chegou a usar mais registros em um provider: tamanho de código sozinho não decide a promoção de uma variante.

## O que validar no jogo e como promover

O ponto de partida recomendado continua sendo **Legacy desmarcado**, preservando a escolha do runtime. O par antigo da 1.4.4 favorece esse caminho em FPS exibido, mas seus modos de apresentação diferem. Não foi demonstrada melhoria de imagem causada por Legacy, nem uma forma segura de recuperar sua diferença de throughput.

1. **Compatibilidade primeiro:** mesma instalação/save/cena, controle 1.4.4 preservado, DLL experimental por vez. Confirmar no log perfil selecionado, provider ativo, multiplicador pedido/aceito/observado e ausência de fallback inesperado. Reiniciar entre trocas. Testar FG off/on, resize, alt-tab e encerramento; manter HUD/UI e Automatic Guard existentes.
2. **Pacing e throughput:** aquecimento completo, versões/caps/VRR/VSync/refresh/modo fixados; ao menos três pares alternados de 30 s em 2x/3x/4x, e superiores somente quando aceitos. Usar `Capture-Performance.ps1 -BuildManifest ... -TimingPath runtime|legacy`; só preencher `RuntimeIdentity` depois de comprovar seleção. Benchmark sem profiler e rodada de GPU Trace separada.
3. **L02/P01/P03:** primeiro um smoke de Nsight Graphics que mostre a execução do PID, filas e FG. Juntar token/fase, recurso/slot, fence, início/fim de render/FG e exibição. Medir cópias, alloc/free e residência em pelo menos 30 ciclos. Sem cobertura, registrar a lacuna; não deduzir GPU idle a partir de wait de CPU.
4. **L03:** no hash exato do plugin, medir deadline pretendido, wakeup e Present real, preservando fence/cancelamento. Só estudar espera híbrida quando erro/custo forem materiais. Não aplicar timer resolution ou prioridade global como substituto da medição.
5. **Q01–Q03:** vídeos cegos em movimento, frames gerados e fase identificados; ablação por componente, SDR/HDR separados, fios/vegetação/silhuetas/disocclusion/iluminação/bordas/camera cut. Mapear domínio de cor e conteúdo dos inputs antes de alterar confiança. Não reabrir HUD/UI.
6. **L01/X02:** testar owner atrasado/ausente, setter recusado e restauração; comparar fonte real e input-to-photon físico. Waitable, Present e markers precisam de um proprietário demonstrado antes de novo pacer. Histórico X01 exige resets e identidade por geração antes de ativação.

Aceitar melhoria somente quando superar a variação do controle, com direção consistente nos pares; intervalos de incerteza que incluam ganho zero são inconclusivos. Fixar tolerâncias de regressão após medir o ruído do controle e antes de avaliar o candidato. Rejeitar piora sistemática de p95/p99 exibidos, latência física, taxa real de fonte, detalhes finos, estabilidade ou memória. **Rollback é encerrar o processo e restaurar a DLL 1.4.4 preservada**; não depender de hot unload para desfazer uma experiência.

As quatro ondas do relatório original permanecem: confiança/robustez implementadas nesta entrega; pipeline condicionado à timeline real; qualidade condicionada à ablação visual; temporal/pacer alternativo em pesquisa separada. Não há experimento privado novo habilitado por default.

## Reprodução e entrega

Usar os três headers gerados locais da release com hashes do manifesto e o checkout RenoDX/dependências registrado. A ferramenta abaixo verifica esses inputs sem sobrescrevê-los:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-EngineeringExperimental.ps1 -RenoDxSource C:/Users/ayase/Documents/renodx-build -OfflineProviders '<paths-exatos-dos-oito-inputs-separados-por-ponto-e-virgula>'
```

Ela configura VS 2022 x64, compila suíte e DLL, roda CTest e grava o manifesto. Sem inputs de fornecedor, o teste dependente deles reporta skip; isso não equivale à validação local 40/40 descrita acima. Um clone limpo não contém os headers proprietários nem os providers.

Comandos adicionais usados nesta etapa, a partir do worktree (substituir os placeholders):

```powershell
python tools/validate_engineering_providers.py --probe build/tests/Release/engineering_provider_probe.exe --provider <310.9.0> --provider <310.9.1> --provider <310.9.16> --output <out>/providers
python tools/validate_engineering_kernels.py --emitter build/tests/Release/engineering_kernel_emitter.exe --provider <310.9.1> --provider <310.9.16> --output <out>/kernels
python tools/validate_photometry.py --emitter build/tests/Release/quality_numerical_tests.exe --output <out>/photometry
python tools/validate_border_axis.py --emitter build/tests/Release/border_axis_tests.exe --output <out>/border
```

O pacote contém DLL experimental, manifesto, relatório/status, logs e resumos de validação, ferramentas de análise e patch de fonte relativo à 1.4.4. Não contém providers do jogo nem a instalação das ferramentas NVIDIA. O arquivo de checksums liga os artefatos locais. A DLL permanece separada da instalação do jogo e não foi publicada.

## Revisão crítica da promoção

A maior confiança é nos contratos exercitados offline: identidade, ausência de escrita em perfil mutado, retenção de alocação, erros de hook, janelas de trial e definições de métricas. Não há a mesma confiança num benefício perceptual. Retenção de módulos pode aumentar memória/residência e limitar OTA; confirmar esse tradeoff em sessão longa. Mais conservadorismo nos perfis pode recusar uma versão que a busca ampla alcançava; conferir fallback e registrar o motivo. As transações não fecham sozinhas as lacunas de concorrência/loader lock.

Não se recomenda emular o display engine com compute, inserir outro pacer ou reduzir uma espera sem mapear consumidor. Um custo menor de CPU pode ser irrelevante para a GPU; um shader menor pode usar mais registros; um cap pode melhorar um proxy e piorar input age. A alternativa mais simples continua sendo usar o runtime nativo, preservar a qualidade já existente e medir o componente que realmente limita a sessão antes de outra alteração.
