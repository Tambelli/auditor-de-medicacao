# Verificação da entrega

Verificação executada em **25/09/2026**, no Windows, para o código entregue neste repositório.

| Verificação | Resultado observado |
|---|---|
| Núcleo C++ compilado e executado nativamente | **273 verificações aprovadas** |
| Testes Python do protocolo e avaliador | **5 testes aprovados** |
| Sintaxe dos scripts (`compileall`) | Aprovada |
| Importação do painel e console | Aprovada; Tk 8.6 disponível no ambiente |
| Compilação ESP32 / ESP-IDF 5.4.1 | **SUCCESS**, incluindo bootloader, partições e firmware |
| Build incremental após ajuste final da câmera | **SUCCESS** |
| RAM estática informada pelo linker/PlatformIO | 40.424 bytes de 327.680 (12,3%) |
| Aplicação em flash informada pelo build | 372.447 bytes de 4.063.232 (9,2%) |
| Tamanho de `firmware.bin` | 372.848 bytes |
| Limpeza de alias temporário de unidade | Confirmada após build |

RAM estática não representa o pico total de execução: framebuffer JPEG, RGB (57.600 bytes), decodificador, pilhas de tarefas e mensagens usam memória adicional. O campo `heap` do status permite acompanhar a memória disponível na bancada. A saída do compilador não comprova que a câmera iniciou nem que o limite temporal de dois segundos foi atingido.

Comandos usados:

```powershell
.\.venv\Scripts\python.exe tools/run_tests.py
.\.venv\Scripts\python.exe -m compileall -q tools tests
powershell -ExecutionPolicy Bypass -File tools/build.ps1
```

Os testes nativos utilizaram Zig 0.13.0 como compilador C++17, com `-Wall -Wextra -Werror`. Os arquivos testados são o mesmo `components/auditor/core.cpp` vinculado ao firmware e as funções reais das ferramentas Python. Os dados de visão desses testes são sintéticos.

Artefatos locais em `.pio/build/esp32cam/`:

- `bootloader.bin`: 26.752 bytes.
- `partitions.bin`: 3.072 bytes.
- `firmware.bin`: aplicação; gravar usando `tools/build.ps1 -Action upload -Port COMx`, que aplica os offsets corretos junto com bootloader/partições.
- `firmware.elf`: símbolos para depuração.

SHA-256 da aplicação verificada:

```text
013f7d1ee6a205417189daf557bd47df2422fcaeaa511ba926938c7eb84f2f84
```

A compilação inicial exigiu abreviar os caminhos do Windows e impedir que o SDK herdasse a identificação Git do repositório ainda sem commits. Essas correções foram incorporadas ao script de build e ao CMake. As dependências ficaram em `.pio-core`; não houve alteração nos materiais originais de `Documentacao`. O ESP-IDF emitiu um aviso de Kconfig de Bluetooth (`visible if`), sem impedir o build; Bluetooth não é inicializado pela aplicação.

## Pendências de bancada

Não foram executados gravação em placa, confirmação da pinagem da unidade RoboCore, captura física, calibração da caixa ainda a comprar, medição de acurácia, tempo real de resposta, som no computador do usuário, consumo elétrico nem 20 ciclos contínuos em hardware. O painel foi importado e seu protocolo testado, mas a interação visual com uma placa conectada não foi exercitada.

O requisito de som autônomo do protótipo original não é atendido com a lista atual de componentes: o alerta sonoro desta implementação depende do painel USB aberto. O roteiro para validar os demais pontos está em `VALIDACAO.md`; nenhum resultado óptico foi inferido dos testes sintéticos.
