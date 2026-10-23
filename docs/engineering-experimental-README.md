# MFGAdaUnlock 1.4.4 — Engineering Experimental

Build experimental derivada da release 1.4.4 (`3e898157`). Contém correções de compatibilidade, rollback, duração de alocações, hooks, medição e trials. Preserva Local Stable, V2 Compatibility e o tratamento existente de HUD/UI.

**Validação desta entrega:** suíte 40/40, 21 roundtrips de providers, dez blends completos compilados e harnesses numéricos na GPU. Não foi feita validação em jogo desta DLL. Não há ganho comprovado de FPS, fluidez, imagem ou latência física. O relatório detalha os 18 itens, inclusive os parcialmente implementados e os dependentes de captura real.

## Conteúdo

- `experimental/`: DLL experimental e seu manifesto de fonte/build/payloads/dependências.
- `rollback-1.4.4/`: controle 1.4.4 preservado e manifesto original. SHA-256 da DLL: `5a193d86c6f60a7f0f275799d66eef7f14bd0946cdbefd48a3237506c747d5cd`.
- `docs/`: relatório de implementação, investigação histórica e status por item.
- `validation/`: CTest, resumos de JIT/GPU/roundtrip e verificações de preservação.
- `analysis/`: scripts de captura/análise com identificação das métricas.
- `source-changes.patch`: alterações relativas à baseline 1.4.4, incluindo testes e ferramentas. Não inclui os providers nem os headers gerados proprietários.
- `BUILD_INFO.json` e `SHA256SUMS.txt`: identidade e checksums do pacote.

## Teste manual

1. Encerre completamente o jogo. Guarde a DLL e a configuração atuais em backup fora da pasta carregada pelo ReShade.
2. Coloque **somente a DLL de `experimental/`** no diretório onde o addon atual é carregado. Não carregue duas versões de MFGAdaUnlock simultaneamente. Não substitua NGX, Streamline ou driver por causa deste pacote.
3. Inicie o jogo e confirme `Experimental 1.4.4 Engineering` no log/painel existente. Preserve as configurações de qualidade/HUD/UI. Comece com **Legacy desmarcado**, mantendo a escolha do runtime.
4. Confirme provider/perfis efetivos e multiplicador antes de comparar. Reinicie completamente entre mudanças que exigem restart. Use a mesma cena aquecida, configurações e pelo menos três pares alternados de 30 s; profiling recebe rodada separada.

Esta build mantém referências de módulos até o fim do processo e fica residente após o primeiro Present observado. **Hot unload/reload não é suportado.** Isso pode limitar atualização OTA durante a sessão. Perfis binários desconhecidos são recusados; Legacy foi conferido nos plugins 2.8 e 2.14.16 disponíveis localmente. O protocolo de execução concorrente dos sites multibyte e a identidade completa de feature/dispositivo continuam abertos; esta DLL não deve ser tratada como release estável.

## Rollback

Encerre o processo, retire a DLL experimental e recoloque sua DLL anterior ou o controle de `rollback-1.4.4/`. Reinicie. Não dependa de unload em vida para trocar o binário. As configurações do jogo, DLLs NVIDIA e driver não foram alterados na preparação deste pacote.

## Reprodução

O manifesto contém commit, arquivos, flags, hashes dos três headers gerados e o conjunto de headers/bibliotecas efetivamente lido pelo MSVC. O patch deve ser aplicado sobre a baseline 1.4.4. Execute com `powershell -NoProfile -ExecutionPolicy Bypass -File tools/Build-EngineeringExperimental.ps1 -RenoDxSource <checkout-RenoDX> -OfflineProviders <paths-separados-por-ponto-e-virgula>`. A ferramenta verifica os inputs locais e compila/testa sem instalar no jogo. Os providers e payloads proprietários continuam sendo dependências locais; um clone limpo sem esses inputs não reproduz todos os testes offline.

Para benchmarks, `Capture-Performance.ps1` aceita `-BuildManifest`, `-TimingPath` e `-RuntimeIdentity`. O manifesto é aceito apenas se seu hash de addon corresponder a um módulo mapeado. Essa correspondência não prova provider ativo; deixe a identidade de runtime desconhecida quando não houver evidência de seleção. `Compare-PresentMon.ps1` recusa comparações com definições ou domínios incompatíveis.
