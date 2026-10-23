# XeMFG R1.9.4 — comando do produtor e isolamento do congelamento

20 de outubro de 2026. Base `60df2d970670cf850a7b661f04f8a69bd5902265` (R1.9.3), na worktree isolada da branch `feature/1.4.5-xemfg-compat-ui-r1.9`. A release pública 1.4.5 e a árvore antiga modificada não foram substituídas. Esta entrega é uma candidata local.

## Resultado do Pragmata

A R1.9.3 continuou congelando no boot. O PID 24940 destruiu o primeiro contexto Intel com retorno zero, mas o último Release observado da swapchain nativa retornou duas referências. Depois houve centenas de tentativas de criação, com resultado Intel -17 e DXGI `0x80070005`. A instrumentação de referências observou o overlay da Steam nessa cadeia de chamadas; isso sozinho não atribui a ele todas as referências.

O usuário desativou o overlay da Steam nas propriedades do jogo. Com a **R1.9.3 original**, o PID 440 completou a troca da saída inicial, registrou `remaining_refs=0` e não apresentou falhas de inicialização. Reativar o overlay restabeleceu o bloqueio no teste seguinte. O DLL do overlay pode continuar mapeado mesmo com sua opção desativada: presença do módulo não equivale a overlay ativo.

Uma tentativa local de guardar uma referência própria até depois da destruição completa do SDK não resolveu o caso com o overlay ativo (PID 13764, ainda duas referências e 40 falhas de inicialização). **Essa alteração foi descartada e não integra a R1.9.4.** Não foram liberadas referências desconhecidas nem modificados os hooks da Steam. A compatibilidade com o overlay ativo permanece aberta; desativá-lo é um contorno demonstrado neste boot, não uma correção geral.

Também foram feitas comparações temporárias com arquivos da raiz retirados e restaurados com backup. O jogo possui cópias em `_storage_`; a retirada de um arquivo da raiz não garante isolamento do componente. Esses testes não são usados para excluir RenoDX ou REFramework como participantes de todas as interações. A comparação Steam manteve os componentes instalados. Todos os arquivos temporariamente substituídos foram restaurados.

## Correção de input implementada

No boot que passou com a R1.9.3, a profundidade foi recusada com `volatile_command`: formato 19, 1708×960, estado 224, lifecycle `eOnlyValidNow`, com comando presente. O estado de admissão tinha `resources=5` e `failed=1`: movimento e cor aceitos, profundidade ausente. Não era uma justificativa para estender artificialmente a validade do recurso.

O unwrapping exigia que a tabela de métodos COM estivesse dentro de uma imagem DLL. Uma lista válida pode ter uma tabela própria no heap, com métodos implementados em módulos carregados. A R1.9.4 adiciona uma política restrita aos comandos emprestados pelo produtor durante a chamada de tagging:

- valida legibilidade, limites e alinhamento da tabela;
- exige implementações COM executáveis em imagens carregadas, mantendo os módulos fixados;
- segue os contratos públicos de unwrapping, com detecção de ciclos e profundidade limitada;
- confere que o comando e o recurso pertencem ao dispositivo da saída;
- mantém a recusa de listas de cópia, layouts incompatíveis, implementações desconhecidas e recursos inválidos.

A admissão de factories e hooks persistentes mantém a política anterior. Não há patch de vtable de outro componente nessa correção. O Intel SDK continua recebendo `ONLY_NOW`, registrando a cópia no comando original do produtor. Um AddRef não é usado como substituto da conservação do conteúdo.

O novo teste fornece um proxy COM válido com tabela própria no heap, usa cópias reais pelo Intel SDK e cobre os dois entrypoints de tags. A **R1.9.3 selada falha** com `heap producer command was not accepted` (`native-20261020-140025-558169`). A candidata aceita o comando válido, recusa uma entrada apontando para dados sem código e retoma a geração no frame seguinte.

## Verificação no boot da candidata

Com o overlay Steam novamente desativado, o PID 18968 da R1.9.4 completou as recriações observadas sem falha de inicialização; os dois outputs aposentados registraram zero referências. A profundidade deixou de aparecer entre as tags recusadas e a admissão passou a:

```text
resources=7 markers=31 constants=1 valid=1 failed=0 sleep=1
```

Essa é evidência no jogo de que o SDK aceitou profundidade, movimento e cor. O processo permaneceu no menu, com pedido de FG desligado, `enabled=0` e zero lançamentos Tensor. **Não é validação de interpolação na cena.** O addon não transforma o FG desligado do jogo em geração forçada. Tags opcionais de UI com formato incompatível continuam omitidas; UI Composition permanece opcional e desligado por padrão.

## Testes e limites

A suíte incorpora a regressão do comando e um teste de recriação com janela visível: quatro ciclos, 90 chamadas Present por ciclo, inferência 4x e shutdown com Streamline/ReShade/Intel reais. Os testes anteriores usavam janelas ocultas. O teste novo confirma submissão pela API com janela visível; não mede scanout, cadência exibida, qualidade ou latência física.

O pacote exige a suíte completa, as rotas Reflex/XeLL, os runtimes admitidos e os recibos dos testes novos. Resultados finais e hash do addon ficam em `evidence/validation-summary.json` e no manifesto do pacote. Não há novo benchmark de FPS nem alegação de ganho de frame pacing.

Emissão final 1.4.5.23, SHA-256 `66eb4e89bb6b695d476a42bc5431a33801fd627d68cee7834e0eca188e02af81`. A suíte final concluiu **94 testes aprovados, três opcionais ignorados e nenhuma falha**, em 199,08 s. Os opcionais são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`. Na primeira passagem, a regressão nova exigiu ajustar sua contagem esperada de UI Composition: o comando inválido introduz um desligamento e uma retomada adicionais. Nenhuma alteração de produção foi necessária para esse ajuste; a suíte completa foi repetida e passou.

Evidência local adicional, relativa à raiz desta worktree, em `out/xemfg-r1.9.3-evidence/`:

| Diretório / PID | Finalidade e resultado |
|---|---|
| `pragmata-freeze/`, 24940 | R1.9.3 com congelamento e loop de criação |
| `boot-None-20261020-133942/`, 19236 | Rastreio local limitado de AddRef/Release, não distribuído |
| `boot-None-20261020-134118/`, 26304 | Rastreio restrito à aposentadoria, não distribuído |
| `boot-None-20261020-134351/`, 23848 | Retirada local da camada final não resolveu; alteração descartada |
| `boot-None-20261020-134701/`, 440 | R1.9.3 original, Steam overlay desativado, recriação passou |
| `boot-None-20261020-135526/`, 13764 | Referência adicional até fim do SDK não resolveu; descartada |
| `boot-None-20261020-140032/`, 18968 | R1.9.4, Steam overlay desativado, inputs completos sem falha |

Os minidumps e logs completos são locais e não integram o download. Há um dump inicial vazio de coleta sem sucesso, não usado como evidência. O Steam relança o executável; os scripts passaram a identificar o PID real por caminho e horário antes da coleta/encerramento. O primeiro teste automático registrou inicialmente o PID do launcher, portanto esse campo não deve ser usado como identidade do processo que renderizou.

Referências de contrato: [Intel, ciclo de vida dos inputs](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md), [Microsoft, swapchain flip por HWND](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd) e [ReShade 6.8, API de runtime manual](https://github.com/crosire/reshade/blob/v6.8.0/source/addon.cpp). Nenhum código GPL foi incorporado.

## Validação em cena — PID 9028

O usuário relatou que a R1.9.4 carregou e parecia gerar quadros, mantendo o overlay da Steam desativado. O log dessa sessão cobre 183,359 s e confirma interpolação efetiva pelo SDK, além da seleção no painel:

| Observação | Resultado |
|---|---|
| Saída 4x | `frames_presented=4`, `enabled=1`, `interpolation_result=0` |
| Troca para 6x e retorno a 4x | Ambas observadas com resultado zero; não apenas solicitação de multiplicador |
| Execução CUDA/Tensor | Contador chegou a 297.760 lançamentos nesta sessão |
| XeLL | Três vínculos de contexto aceitos, todos com resultado zero |
| Flip metering | Driver aceitou a configuração 4x, `status=0`, `feedback=0`, `metered=true`; os registros de deadline também observaram saída metered em 4x/6x |
| Encerramento | Streamline concluiu com resultado zero; os três outputs foram destruídos com resultado zero e zero referências nativas restantes |

A primeira amostra após habilitar FG retornou `-14` (`XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_INPUT_RESOURCES`: input insuficiente para avaliar a interpolação). Na amostra seguinte, 1.000 ms depois, já havia saída 4x com resultado zero e 2.028 lançamentos Tensor. Não há nova ocorrência desse erro nas amostras seguintes. O log amostrado não permite estabelecer a duração exata da recusa nem classificá-la automaticamente como aquecimento normal.

A última amostra explícita de admissão completa (`resources=7`, `valid=1`, `failed=0`) antecede a ativação de FG. O limite de registros de admissão foi atingido antes da cena; portanto, não se apresenta essa amostra como auditoria de cada frame interpolado. A confirmação de execução vem dos resultados de interpolação e lançamentos efetivos. As recusas de tags opcionais de HUD observadas no início permanecem identificadas como `optional_hud`.

O usuário ativou UI Composition durante a sessão (`requested=1`, `hudless=1`, `enabled=1`, resultado zero). Essa preferência foi preservada; a opção continua desligada por padrão. Como houve troca de multiplicador e interação com o overlay, a sessão não é um benchmark controlado. Há registros de `pacing_hitch` e não houve captura de intervalos exibidos pelo PresentMon ou medição física de latência. Aceitação pelo driver e inferência funcional não demonstram cadência perfeita.

Os logs, configurações, resumo por PID e hashes estão em `out/xemfg-r1.9.4-evidence/pragmata-gameplay-9028/`, **relativo à árvore principal**, não a esta worktree. O addon instalado nas cópias da raiz e de `_storage_` tem o SHA-256 indicado acima. O pacote selado e seu manifesto não foram alterados para incorporar esta validação posterior.

## Validação restante e rollback

Manter o overlay Steam desativado para contornar o bloqueio conhecido no Pragmata. A interpolação em cena foi confirmada na sessão acima; a compatibilidade com o overlay Steam ativo continua aberta. Ainda são necessários testes controlados de imagem e desempenho antes de promover essa candidata ou generalizar o resultado a outros jogos.

Rollback: fechar o processo e usar `Restore-MFGBackup.ps1` com o snapshot do instalador. O pacote conserva a 1.4.5 oficial em `rollback/` e a R1.9.3 em `archive/` para proveniência; a R1.9.3 não é indicada como solução do congelamento. A opção de overlay da Steam foi alterada pelo usuário e não é modificada pelo instalador.

No Pragmata desta sessão, o snapshot estável é `out/local-install-backups/PRAGMATA-20261020-132725-1860/`, relativo à árvore principal. Ele contém o addon oficial de hash `9715987f…e2972e2` e os runtimes Intel originais, nas cópias da raiz e de `_storage_`. O backup criado ao instalar a R1.9.4 conserva a R1.9.3; para voltar à configuração estável, usar o snapshot estável, não apenas o backup mais recente.
