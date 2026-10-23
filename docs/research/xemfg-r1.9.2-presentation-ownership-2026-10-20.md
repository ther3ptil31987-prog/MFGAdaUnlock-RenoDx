# XeMFG R1.9.2 — propriedade da apresentação

20 de outubro de 2026. Continuação isolada da R1.9.1, na base oficial DLSS-G 1.4.5. Esta candidata trata a integração de apresentação e inicialização; não altera os kernels de qualidade da release DLSS-G.

## Falha e reprodução

O usuário informou crash no boot do Pragmata com a R1.9.1. O registro do PID 20244 mostra a saída inicial de 1920×1080, seguida da tentativa de criar outra saída de 2560×1440 para a mesma janela. O Intel SDK retornou -17 e registrou `0x80070005`. Não houve inferência. O estado NVIDIA observado permaneceu zero; portanto os dois avisos filtrados na R1.9.1 não explicam esse boot. A instalação foi restaurada para o addon oficial 1.4.5 e o par Intel original antes de iniciar a nova reprodução.

O teste anterior usava ReShade/Intel reais, mas uma fixture para as funções Streamline. Isso deixou sem cobertura os callbacks DXGI do plugin NVIDIA. O novo teste carrega os DLLs reais do Pragmata (Streamline 2.8, plugin DLSS-G SHA-256 `d7fa7559194b7b31487ed31697282f27f1c25adc6ac3332ad1c1ee5f42cf09da`), o provider NGX, ReShade e o par Intel admitido. Usa uma janela e identidade de projeto próprias, sem abrir ou modificar um jogo.

Com a R1.9.1, a reprodução falhou durante as recriações. Um dump isolado identificou leitura de ponteiro nulo em `sl.dlss_g.dll+0x22987`; outra execução com permissões normais terminou com `E_ABORT` no Present. A reprodução sem nosso addon também atingiu o mesmo RVA na sequência NVIDIA com FG desligado. Isso identifica uma suposição inválida no caminho de callbacks que continuava ativo; não prova que a exceção isolada e o erro de criação no jogo sejam exatamente a mesma falha.

Na ponte anterior, desligar `DLSSGOptions.mode` não removia os callbacks de apresentação do plugin NVIDIA. Assim, o Intel possuía a saída, mas o Streamline ainda chamava o código de apresentação NVIDIA.

## Correção

- Captura os exports de inicialização do interposer já mapeado, sem chamar a SDK dentro do loader. A desativação efetiva ocorre na chamada de inicialização do host (`slInit`, ou `slSetD3DDevice` antes da primeira saída).
- Usa a API pública `slSetFeatureLoaded(kFeatureDLSS_G, false)` e verifica o estado físico retornado. Isso retira os callbacks NVIDIA da lista do Streamline, preservando as APIs de opções/estado usadas pela ponte. As demais features permanecem encaminhadas.
- O estado lógico de `slIsFeatureLoaded`/`slSetFeatureLoaded` para DLSS-G passa a representar a feature substituída pela ponte Intel. Um pedido de desligamento do jogo permanece desligamento; habilitar a feature não reinstala os callbacks conflitantes. A geração também continua dependendo das opções do jogo e dos inputs completos.
- Recusa criar a saída Intel se a inicialização segura não foi observada, em vez de alterar a lista de callbacks durante Present ou no worker. O painel explica esse caso.
- Antes de `slShutdown`, impede novas consultas do worker e drena as consultas anteriores. Libera a trava antes da desmontagem dos plugins e mantém mapeado o código que recebeu detours. Não suprime a chamada original de shutdown.

`slSetFeatureLoaded` não é thread-safe. Por isso, nenhum worker ou Present tenta executá-lo. A implementação segue as fronteiras de inicialização/controle que já exigem quiescência do host. Referências primárias: [gerenciador de plugins do Streamline 2.8](https://github.com/NVIDIA-RTX/Streamline/blob/v2.8.0/source/core/sl.plugin-manager/pluginManager.cpp), [API pública](https://github.com/NVIDIA-RTX/Streamline/blob/v2.8.0/source/core/sl.api/sl.cpp) e [proxy de swapchain](https://github.com/NVIDIA-RTX/Streamline/blob/v2.8.0/source/core/sl.interposer/dxgi/dxgiSwapchain.cpp). A API desativa os hooks; não transforma o algoritmo Intel em NVIDIA.

## Verificação e limites

O teste real cobre addon antes de `slInit` e addon depois de `slInit`, mas antes de `slSetD3DDevice`. Cada ordem é exercitada com XeLL e Reflex. Há quatro recriações na mesma janela, resolução 1920×1080→2560×1440, buffers 3/4, R10G10B10A2 e flags `0x842`. Inputs sintéticos válidos atravessam `slSetConstants`/`slSetTagForFrame`, geram inferência Tensor em 4x e são retirados antes do encerramento. O teste exige contextos Intel e runtime manual ReShade aposentados.

A fixture também testa desligar/religar a feature, conservar os controles de Reflex e impedir consultas após shutdown. O teste com os flags do RE Engine conserva a cobertura anterior de inputs voláteis, bypass por input incompleto, composição UI opcional, resize e restauração. Resultados finais, hashes e recibos estão no manifesto e em `evidence/validation-summary.json` do pacote.

Uma execução restrita chegou às quatro recriações, mas ficou esperando no encerramento NGX. O dump mostrava a espera dentro da telemetria NVIDIA. Reexecutar com as permissões normais, permitindo cache NGX, completou o encerramento; não foi adicionado um bypass de shutdown para esconder essa limitação do ambiente de teste.

Emissão final: SHA-256 do addon `e7e3a6c437d2dc19a3eabdb1dc03bf298539c2a129275acd3c8ca3b38fdad15c`, versão de arquivo 1.4.5.21. A suíte concluiu **91 testes aprovados e três opcionais ignorados**, sem falhas, em 290,74 s. As quatro combinações de Streamline real completaram inferência, recriação e shutdown. Os opcionais ignorados são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`; isso não equivale a validar novamente aqueles artefatos históricos.

**Ainda não validado em gameplay:** boot e interpolação do Pragmata, motivo da recusa de depth na R1.9, piscadas, Control Resonant e demais jogos Capcom. O novo teste remove uma lacuna concreta, mas não fornece pixels, cadência exibida ou latência física desses jogos. A primeira verificação no Pragmata deve confirmar boot estável antes de examinar os inputs da cena. CUDA conserva a cobertura numérica anterior de nove de 17 programas.

Rollback recomendado no Pragmata: fechar o jogo e restaurar o snapshot do instalador, contendo addon oficial 1.4.5 e o par Intel original. A R1.9.1 fica arquivada para proveniência, não como fallback recomendado. Sem nova release automática.
