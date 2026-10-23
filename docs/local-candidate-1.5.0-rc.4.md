# MFG Unlock 1.5.0-rc.4 — candidata local

Esta candidata parte da RC3 mais recente. Não foi publicada nem instalada automaticamente.

- **Mafia:** corrige a recusa de cabeçalhos de recurso zerados, mantendo as validações D3D12, de dispositivo, extent e duração.
- **Plague Tale:** acrescenta espera XeLL no início real da simulação quando a integração não chama sleep explicitamente. Não inventa markers nem adiciona espera em Present.
- **Recriação de saída:** aceita a equivalência opaca entre alpha não especificado e ignorado. Os testes mantêm referências externas durante apresentação, inferência e resize.
- **Death Stranding 2:** identifica o shader jittered ausente e mantém apresentação original com mensagem explícita. **Ainda não interpola nesta build.**
- **Black Flag:** o erro DX12 específico **ainda está sem solução confirmada**. O teste de recriação corrigido não reproduziu a perda do dispositivo do jogo.

Mafia e Plague Tale passaram pelas reproduções isoladas dos bloqueios observados. A confirmação em gameplay com esta candidata continua pendente. Não há alegação de correção universal ou melhoria de desempenho.

Validação final: **136 testes aprovados e 3 opcionais ignorados em Release; os mesmos resultados em Debug**, sem falhas. Os três controles negativos contra a RC3 reproduziram as rejeições; a candidata passou pelos cenários correspondentes. O teste com o addon RenoDX do Black Flag também passou em processo isolado, sem certificar o jogo.

Com o jogo fechado, o addon de teste é `renodx-mfgunlock.addon64` na raiz do pacote. Mantenha somente um addon MFG e as DLLs Intel admitidas já utilizadas; reinicie o processo. UI Composition permanece opcional e desmarcado por padrão. O modo de janela do jogo é respeitado.

Para rollback, feche o jogo e restaure `rollback/renodx-mfgunlock-RC3.addon64` com o nome `renodx-mfgunlock.addon64`. O pacote não redefine seu INI nem inclui DLLs Intel, OptiScaler ou um ASI companion. `debug/` e símbolos são destinados a diagnóstico.

Consulte `engineering-report.md` para causas, experimentos que falharam e limites, e `manifest.json` para hashes e resultados finais. Nenhuma nova rodada manual de gravações é necessária para verificar os bloqueios já reproduzidos pelos testes.
