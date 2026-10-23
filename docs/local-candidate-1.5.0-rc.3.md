# MFG Unlock 1.5.0-rc.3 — candidata local

Esta build parte da RC2. Não é uma release pública e não foi instalada automaticamente em jogos.

- Isola opções/estado do jogo das consultas externas da Steam no resolver Streamline.
- Descarta inputs que retornam depois de resize ou troca de sessão.
- Mostra o requisito que impede XeMFG de gerar, mantendo o backend selecionado em passthrough.
- Respeita o modo de exibição escolhido pelo jogo; Force Borderless antigo é ignorado em XeMFG.
- Acrescenta relatório de compatibilidade com hashes, validação de inputs, primeira recusa de latência e erro específico do dispositivo.
- Protege a inspeção de DLLs transitórias durante a inicialização, sem manter módulos não relacionados presos ao processo.

O caminho DLSS-G, o modelo XeMFG, CUDA, flip metering e pacing existentes foram preservados. UI Composition continua opcional e desmarcado por padrão. Não há novos controles públicos de tentativa de qualidade.

Plague Tale ainda precisa ter sua recusa de latência identificada na sessão real. Black Flag ainda requer localizar o acesso que causa perda do dispositivo. O isolamento de observador não certifica todos os caminhos de Steam Overlay/REFramework. A RC3 não acrescenta suporte completo a entradas FSR-FG ou NVAPI direto.

Com o jogo fechado, use `renodx-mfgunlock.addon64` da raiz deste pacote para o teste. Mantenha somente um addon MFG e conserve as DLLs Intel admitidas da instalação. Não inclui OptiScaler, ASI companion ou substitutos de driver. Reinicie o processo; não faça hot reload.

Para investigar uma falha: Diagnostics → **Copy compatibility report**. Esse JSON contém metadados e hashes de módulos, sem pixels, caminhos pessoais ou endereços GPU. O pacote de engenharia também guarda logs locais de teste; esses logs são separados do relatório público sanitizado.

Para voltar à RC2, feche o jogo, restaure `rollback/renodx-mfgunlock-RC2.addon64` com o nome `renodx-mfgunlock.addon64` e reinicie. A RC2 conserva sua política antiga de janela; as preferências atuais permanecem no INI. Release é o binário principal; `debug/` é destinado a diagnóstico.

Consulte `engineering-report.md` e `manifest.json` para escopo, resultados efetivos, proveniência e limitações. Testes sintéticos não equivalem a validação em todos os jogos ou medição de fluidez no monitor.
