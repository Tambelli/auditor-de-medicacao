# Documentação completa da implementação

Este documento explica o código entregue para o Auditor Visual de Medicação e serve como guia de revisão antes da montagem com o ESP32-CAM e a caixa de medicamentos.

## Objetivo e escopo

O equipamento observa um compartimento, captura imagens no horário configurado e compara a cena com referências calibradas de compartimento vazio e com dose. O processamento e a decisão ocorrem no ESP32. A retirada visual não comprova a ingestão, não identifica medicamentos e não reconhece pessoas.

O ciclo começa somente quando a hora, a câmera, a calibração e a presença inicial estão válidas. A retirada só é confirmada depois de duas observações consecutivas de vazio. Uma cena ambígua, instável, escura, saturada ou encoberta gera resultado inconclusivo/falha, nunca uma retirada automática.

## Base do projeto

`Documentacao/Tarefa 02 - Turma 3.docx` é a especificação consolidada usada. Ela define comparação de referências, configuração serial, agenda, confirmação em capturas consecutivas, rearmamento e ensaios. Os demais DOCX/PPTX da pasta descrevem o conceito inicial.

O repositório ESP-VISION foi consultado, mas sua matriz de suporte não lista o ESP32 clássico. O projeto usa o driver oficial [esp32-camera](https://github.com/espressif/esp32-camera), que suporta ESP32 e OV2640. Não há CNN ou pesos artificiais: o classificador é uma comparação explicável e embarcada.

## Adaptações ao hardware escolhido

A placa RoboCore tem ESP32 clássico, câmera OV2640 e gravador USB. Como não há buzzer confirmado, o firmware emite `alert` pela USB e o painel `tools/panel.py` toca o som no computador. Sem o painel aberto não existe som autônomo. O reconhecimento usa o painel ou `ack`; BOOT e RESET continuam reservados à gravação.

A página comercial não fornece a pinagem definitiva da câmera. O perfil `0` deixa a câmera desativada. O perfil `1` usa a pinagem AI Thinker e o perfil `2` usa a pinagem WROVER KIT. Esses mapas estão em `main/camera.cpp` e precisam ser conferidos na unidade recebida; uma falha também pode ser cabo, alimentação ou sensor.

Não são usados Wi-Fi, Bluetooth, RTC, bateria, deep sleep, cartão MicroSD, buzzer, OLED ou iluminação externa. A hora é ajustada pela USB após cada reinício e a agenda usa UTC-3 fixo.

## Organização do código

- `components/auditor/core.cpp`: validação, visão e máquina de estados sem dependência de hardware.
- `components/auditor/include/auditor.hpp`: tipos, parâmetros e API do núcleo.
- `main/camera.cpp`: perfis, captura JPEG, decodificação RGB888 e luminância.
- `main/storage.cpp`: persistência NVS com versão e checksum.
- `main/app_main.cpp`: inicialização, comandos JSON, calibração, agenda e eventos.
- `tools/panel.py`: painel Tkinter, snapshots, ROI, calibração, agenda, som e logs.
- `tools/console.py`: console serial sem interface gráfica.
- `tools/evaluate.py`: rótulos humanos, matriz de confusão e metas.
- `tools/build.ps1`: compilação e upload no Windows.
- `tests/core_tests.cpp` e `tests/test_tools.py`: testes automatizados.
- `docs/`: arquitetura, hardware, protocolo, validação e verificação.

## Inicialização e persistência

O firmware inicializa NVS, lê a configuração, prepara UART0 em 115200 8N1, cria uma fila FreeRTOS e inicia a câmera somente para o perfil `1` ou `2`. Emite os eventos `boot` e `status`. Configuração e calibração sobrevivem ao reset; hora, reconhecimento e ciclo ativo não sobrevivem. Isso impede inferir uma retirada durante o período desligado.

O registro NVS possui versão e checksum FNV-1a. Erro de leitura, versão ou checksum não apaga a memória automaticamente: o firmware volta aos padrões e informa o erro. Alterar perfil ou ROI apaga as referências porque elas não servem para outra geometria.

## Pipeline de visão

A câmera captura JPEG QQVGA, 160 × 120, com um framebuffer. O primeiro buffer de cada leitura é descartado. O JPEG é decodificado para RGB888 em 57.600 bytes e convertido para cinza com `gray = (77*R + 150*G + 29*B) >> 8`.

A ROI deve estar dentro da imagem, ter no mínimo 32 × 32 pixels e ocupar no máximo 75% da cena. Ela é reduzida a 32 × 32 por médias de blocos. Até 192 pontos fora da ROI acompanham o entorno e ajudam a rejeitar deslocamento e mudança global de iluminação.

A distância entre imagens é a média da diferença absoluta dos pixels. A classificação só é aceita quando há duas referências válidas, a separação é maior que `2 * margin`, o entorno está estável, a diferença entre as duas distâncias supera `margin` e a distância escolhida fica abaixo do limite configurado e de 45% da separação.

## Calibração

Cada comando de calibração coleta cinco quadros. O primeiro define a cena; os seguintes precisam permanecer próximos dele. A referência é salva como `present` ou `empty`. Ao concluir as duas referências, o firmware rejeita cenas pouco separadas ou com entorno instável.

Padrões: ROI `[40,24,80,72]`, perfil `0`, horário `08:00`, janela de 60 s, intervalo de 5 s, duas confirmações, distância máxima 18, margem 5, distância de contexto 20 e estabilidade 8.

## Máquina de estados

```text
IDLE -> ARMED -> MONITORING -> REMOVED
                         \-> ALERT
                         \-> FAULT
```

`arm` verifica presença e agenda um único ciclo. O horário civil calcula o início; os prazos ativos usam milissegundos monotônicos de 64 bits. Uma leitura concluída depois do fim da janela não confirma retirada. Uma lacuna maior que `interval_s + 2 s` quebra a sequência de vazios. Depois de qualquer resultado é necessário repor a dose e armar novamente.

## Protocolo USB

Comandos são JSON Lines terminados por LF, com no máximo 511 bytes. Podem conter `id`, repetido no `reply`. Os comandos são `status`, `time`, `set`, `calibrate`, `clear_calibration`, `snapshot`, `sample`, `arm`, `ack` e `cancel`.

```json
{"cmd":"status","id":1}
{"cmd":"time","epoch":1790334000,"id":2}
{"cmd":"set","profile":1,"roi":[40,24,80,72],"id":3}
{"cmd":"calibrate","label":"empty","id":4}
{"cmd":"calibrate","label":"present","id":5}
{"cmd":"arm","delay_s":10,"id":6}
```

Durante ciclo ativo somente `status`, `ack` e `cancel` são aceitos. Os eventos são `boot`, `status`, `observation`, `transition`, `reply` e `image`. O painel salva também linhas de diagnóstico não JSON do bootloader.

## Painel e console

O painel conecta à porta serial, evita reset intencional por DTR/RTS, mostra snapshots, permite arrastar a ROI, aplica perfil, ajusta hora, calibra, agenda, arma, cancela, reconhece alertas, toca som e grava `logs/*.jsonl`.

```powershell
.\.venv\Scripts\python.exe tools/panel.py
.\.venv\Scripts\python.exe tools/console.py COM5
```

Snapshots são solicitados explicitamente e apenas com ciclo inativo. Não há transmissão para a internet. `tools/evaluate.py` recebe o log e um CSV `ms,truth` para gerar matriz de confusão.

## Compilação e gravação

Versões fixadas: PlatformIO 6.1.18, espressif32 6.11.0, ESP-IDF 5.4.1 e esp32-camera 2.0.16. Flash de 4 MB, modo DIO e sem exigência de PSRAM.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install platformio==6.1.18 -r tools/requirements.txt
powershell -ExecutionPolicy Bypass -File tools/build.ps1
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Action upload -Port COM5
```

O script de build usa uma unidade `subst` temporária para contornar o limite de caminhos do Windows ao extrair o ESP-IDF e a remove ao terminar. O último build validado gerou bootloader, partições e firmware com cerca de 40.424 bytes de RAM estática e 372.447 bytes de aplicação em flash.

## Testes realizados

```powershell
.\.venv\Scripts\python.exe -m pip install ziglang==0.13.0
.\.venv\Scripts\python.exe tools/run_tests.py
.\.venv\Scripts\python.exe -m compileall -q tools tests
```

Os testes C++ compilam o mesmo núcleo do firmware e cobrem ROI, imagens inválidas, cenas escuras/saturadas, referências, instabilidade, oclusão sintética, persistência temporal, atrasos, prazos, reconhecimento, cancelamento e rearmamento. Os testes Python cobrem fragmentação serial, ruído do bootloader, JSON inválido, snapshot, rótulos duplicados e boots múltiplos.

Resultado local registrado em `docs/VERIFICACAO.md`: 273 verificações C++ aprovadas, 5 testes Python aprovados e compilação ESP32 concluída. Esses números não comprovam acurácia óptica, pinagem física, consumo ou funcionamento com a caixa real.

## Primeira montagem

1. Com a placa desligada, encaixe o flat da OV2640 no sentido correto.
2. Fixe câmera e caixa, enquadrando um compartimento e parte do entorno.
3. Grave o firmware, abra o painel e confira a pinagem.
4. Ajuste a hora, capture uma imagem e selecione a ROI.
5. Calibre vazio e presente sem mãos na cena.
6. Execute `sample` e confirme `present`.
7. Arme uma demonstração de 10 segundos, retire a dose e aguarde duas leituras vazias.
8. Rearme com a dose reposta e deixe-a até o fim para testar `alert`.
9. Cubra a câmera em outro ensaio; o resultado esperado é `fault`, nunca `removed`.

Não faça pontes em GPIOs desconhecidos nem conecte/desconecte o flat energizado.

## Pendências e limitações

Ainda precisam ser feitos na placa real: confirmação da pinagem, captura física, calibração da caixa a comprar, 30 observações presentes, 30 vazias, 10 oclusões, 20 ciclos, medição de duração, atraso, heap, som e consumo. A meta documental é 90% em cada classe cheia/vazia e zero confirmação indevida nas oclusões.

Limitações conhecidas: o som depende do painel; a hora precisa ser ajustada após reinício; não há repetição diária automática; o algoritmo não identifica medicamentos; reflexos, sombras, tampas, baixo contraste e comprimidos pequenos podem causar `unknown`; a classificação não comprova ingestão.

## Documentos complementares

- [README.md](README.md): instalação e demonstração resumidas.
- [docs/ARQUITETURA.md](docs/ARQUITETURA.md): rastreabilidade dos requisitos.
- [docs/HARDWARE.md](docs/HARDWARE.md): montagem e pinagem.
- [docs/PROTOCOLO.md](docs/PROTOCOLO.md): protocolo serial completo.
- [docs/VALIDACAO.md](docs/VALIDACAO.md): ensaios de bancada.
- [docs/VERIFICACAO.md](docs/VERIFICACAO.md): resultados de software.
- [main/app_main.cpp](main/app_main.cpp): aplicação.
- [components/auditor/core.cpp](components/auditor/core.cpp): núcleo.
- [main/camera.cpp](main/camera.cpp): câmera.
- [tools/panel.py](tools/panel.py): painel.
