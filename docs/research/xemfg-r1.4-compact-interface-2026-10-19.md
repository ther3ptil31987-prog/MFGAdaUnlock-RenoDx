# XeMFG R1.4 — painel compacto e ajuda contextual

Base: R1.3, commit `8fe4cc97033e334fd60bdb130d317439e284469f`, addon SHA-256 `f20a92b2ced415fbe8462b4cfb82f1f80b526e4a9b839bfbf6417e704bd7dcff`. O usuário relatou resposta do mouse mais próxima do transporte com OptiScaler. Esse retorno subjetivo pertence à R1.3; não é uma medição física nem validação antecipada desta build.

## Mudanças

- Menos padding e espaçamento no estilo compartilhado DLSS-G/XeMFG. Resumo de três cards com altura de duas linhas, sem o subtítulo redundante. A fonte escolhida no ReShade é preservada; todas as alterações de estilo são locais e restauradas ao sair do painel.
- Controles XeMFG usam a mesma proporção de colunas. Em largura estreita passam a linhas empilhadas. Rótulos e estados longos quebram linha dentro da coluna.
- Descrições longas dos ajustes foram para tooltips no controle, rótulo e marcador `(?)`. Informações essenciais sobre reinício, erros e aplicação pendente continuam visíveis.
- Os seletores avançados de latência agora têm rótulos na coluna própria e IDs ocultos nos combos. Antes, a largura integral do combo empurrava seu rótulo visível para fora do card. Os valores e a rota suportada permanecem Auto/Reflex Streamline → Auto/XeLL.
- Diagnostics XeMFG mostra conexão, modelo, aceleração, metering, latência e janela. Contadores acumulados e códigos brutos ficam somente no relatório copiado, incluindo os dados da ponte antes mostrados num segundo card.
- Os rótulos de DLSS-G passam a alinhar a baseline com os controles e a quebrar suas descrições dentro da coluna. Seus controles e bindings existentes permanecem.

Não há alteração no modelo, nos shaders, na composição do HUD, na sincronização, no processamento XeLL, no código CUDA ou no pacer desta revisão.

## Qual flip metering é usado

Em placas NVIDIA, dashdogy descreve explicitamente **o flip metering do driver NVIDIA**, também usado por DLSS-G. O nosso backend já importa esse mecanismo: `backend.cpp` resolve a função NVAPI `0xf3148c42`; `vendor/sdk_pacing.cpp::ConfigureFlip` fornece `NvFlipConfigV2` à fila Direct do swapchain XeFG. O resultado e o feedback de independent flip determinam o estado mostrado. O caminho de apresentação do SDK permanece disponível quando o driver não aceita metering.

Portanto, o texto “Driver flip metering” não indica falta de um caminho Intel disponível no mod de referência. Intel XeFG é o gerador; o metering solicitado neste hardware é NVIDIA. O painel agora explicita “NVIDIA driver - active”. Aceitação do driver não comprova cadência no monitor nem hardware de apresentação Blackwell.

Conferidos o código MIT fixado em `a4cf2c4ab632e84c72254932793bb9e64b33d76e`, o código importado local e o [README público do autor](https://github.com/dashdogy/MFG-Unlocker#v160), consultado em 19/10/2026. A versão pública corrente não é tratada como identidade do ASI fornecido nem como prova de paridade integral de funcionalidades. [Referência fixada de pacing](https://github.com/dashdogy/MFG-Unlocker/blob/a4cf2c4ab632e84c72254932793bb9e64b33d76e/source/native/xess/sdk_pacing.cpp).

## Verificação

O preview usa os widgets reais e ImGui, com estados sintéticos, sem injetar no jogo. Foram renderizados e inspecionados cenários das quatro abas, fonte grande/janela estreita, roteamento avançado aberto, tooltip e seletor de multiplicador aberto. O harness confirma abertura do tooltip e do popup e ausência de overflow horizontal no card de roteamento expandido. As imagens verificam layout; não medem o driver, a imagem gerada ou latência física. A compilação DLSS-G verifica os bindings; o preview visual usa o painel XeMFG.

Compilação concluída com `cmake --build build/tests --config Release -j 2`. A verificação final `ctest --test-dir build/tests -C Release --output-on-failure --output-log build/ctest-native-r1.4.log` terminou com **57 testes aprovados, três opcionais ignorados e nenhuma falha**, em 55,02 s. Os ignorados são `provider_identity`, `engineering_manifest` e `release_1_4_5_manifest`, dependentes de inputs históricos opcionais. Os quatro testes nativos de inferência real 2x/3x/4x/6x usaram o addon final. Os quatro cenários extras do preview também passaram após a compilação final.

Addon final: versão de arquivo `1.4.5.9`, produto `1.4.5-XeMFG-R1.4`, SHA-256 `d6204480a38682c04a7e4c5fcafec6c0e087517dffc30c39d0a5738efba2e324`. O manifesto do pacote liga esse binário ao commit e aos hashes de todos os inputs.

## Entrega e rollback

Build local R1.4, sem publicação automática. O pacote inclui manifesto de fontes/inputs, evidências e rollback para a R1.3 e para a 1.4.5 oficial. Instalação requer jogo fechado. O addon e as configurações da R1.3 devem ser salvos antes da substituição.
