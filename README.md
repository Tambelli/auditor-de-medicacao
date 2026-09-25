# Auditor visual de medicação

Firmware para o **ESP32 clássico com câmera OV2640**, painel USB e ferramentas de ensaio. Implementa o escopo de **um compartimento** da `Documentacao/Tarefa 02 - Turma 3.docx`, adaptado à placa [ESP32-CAM com gravador integrado da RoboCore](https://www.robocore.net/wifi/esp32-cam-com-gravador-integrado) e a uma caixa de medicamentos. A aquisição, a classificação e a decisão executam na placa. A observação de retirada **não comprova ingestão**.

O projeto usa ESP-IDF e o driver oficial [espressif/esp32-camera](https://github.com/espressif/esp32-camera). O ESP-VISION sugerido não lista o ESP32 clássico entre seus alvos suportados. A comparação de referências segue o método inicial especificado na Tarefa 02; não há rede neural nem pesos fictícios de treinamento.

## O que está implementado

- Captura JPEG 160 × 120, conversão local para cinza e ROI configurável de um compartimento.
- Calibração cheio/vazio com cinco amostras estáveis por referência, persistidas em NVS.
- Classificação `present`, `empty` ou `unknown`, rejeitando distância excessiva, ambiguidade, iluminação extrema e mudanças no entorno.
- Verificação de estabilidade entre dois quadros em cada leitura.
- Confirmação de retirada somente após presença no armamento e duas ou mais observações consecutivas de vazio.
- Agenda de execução única por rearmamento, horário configurável e janela de tolerância.
- Estados de dose pendente e falha de leitura distintos; reconhecimento nunca equivale a retirada.
- Interface serial JSON Lines, painel local com imagem/seleção de ROI, som e registros exportáveis.
- Testes do mesmo núcleo C++ usado no firmware, testes das ferramentas e CI.

## Adaptações ao material da pasta Documentacao

A Tarefa 02 consolida as propostas anteriores e especifica comparação simples de imagens, ajuste de hora pela serial e rearmamento manual. Ela orienta esta implementação. A restrição atual de usar somente a placa e a caixa prevalece sobre as listas antigas de ESP32-S3, RTC, buzzer, OLED, bateria e iluminação externa.

**Alerta sonoro:** a placa informada não inclui buzzer. O firmware registra o alerta pela USB; o painel toca o som no computador conectado, com a saída de áudio habilitada. Sem computador/painel, a classificação continua, mas **não existe alarme sonoro autônomo**. O requisito original RF06 precisa desta alteração de arquitetura ou de um buzzer adicional. Não se utiliza BOOT/RESET como reconhecimento; o botão correspondente fica no painel/serial. Nenhum GPIO de LED não confirmado é acionado.

**Pinagem:** o nome comercial ESP32-CAM não basta para garantir a ligação interna. A página da RoboCore não publica esquema definitivo e essa versão não possui MicroSD. O firmware inicia com `profile=0`, sem acionar a câmera, até selecionar uma pinagem conferida. Há perfis AI Thinker e WROVER KIT. Isso evita assumir que a versão com USB seja eletricamente idêntica à AI Thinker tradicional. Veja [hardware](docs/HARDWARE.md).

**Alimentação e iluminação:** usa alimentação USB e iluminação ambiente estável de bancada. Cabo, computador/fonte e fixação mecânica precisam estar disponíveis. A montagem não tem RTC externo, controle de iluminação, bateria nem deep sleep. Ao reiniciar, a hora fica inválida e o ciclo desarmado, mesmo com referências salvas.

## Compilar e gravar no Windows

Requer Python 3.10+ com pip. Para o painel, a instalação de Python precisa incluir **Tcl/Tk**. Não abra simultaneamente dois programas na mesma porta serial.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install platformio==6.1.18 -r tools/requirements.txt
powershell -ExecutionPolicy Bypass -File tools/build.ps1
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Action upload -Port COM5
.\.venv\Scripts\python.exe tools/panel.py
```

Troque `COM5` pela porta da placa. O gravador USB já está integrado. Se não entrar automaticamente no bootloader, siga a identificação dos botões da sua unidade: normalmente manter BOOT, pressionar/soltar RESET, iniciar gravação e soltar BOOT ao conectar. Reinicie depois da gravação, se necessário. Não faça pontes em pinos cuja função não foi conferida.

No Windows, `tools/build.ps1` cria e remove um alias de unidade com `subst` para evitar o limite de caminhos durante a extração do ESP-IDF. Os arquivos continuam nesta pasta, sem serem movidos. Use o mesmo script nos builds seguintes. Em Linux ou em diretório curto, o comando direto é `python -m platformio run -e esp32cam`.

Versões: PlatformIO Core **6.1.18**, plataforma `espressif32` **6.11.0**, framework ESP-IDF **5.4.1** e `esp32-camera` **2.0.16**. Flash de 4 MB, DIO, uma aplicação sem OTA, NVS de 64 KiB. A configuração não exige PSRAM; usa QQVGA e um buffer JPEG em DRAM.

Alternativa no terminal de um ESP-IDF 5.4.1 instalado:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

`main/idf_component.yml` instala os componentes oficiais. O lock gerado pelo primeiro build registra as dependências transitivas. A pasta original `Documentacao` não é modificada.

## Primeira demonstração

1. Posicione a câmera imóvel sobre **um** compartimento. Use objetos de teste contrastantes e luz constante. Comprimidos muito pequenos numa imagem da caixa inteira podem não ser detectáveis; aproxime/enquadre o compartimento.
2. Abra o painel, selecione a porta e conecte. Aplique o perfil cuja pinagem foi conferida. A câmera deve aparecer como disponível. Se falhar, confira o cabo flat e a pinagem antes de continuar.
3. Clique **Ajustar hora**. O painel envia Unix UTC; a agenda utiliza UTC−3 fixo.
4. Clique **Capturar imagem USB**. Arraste para selecionar o compartimento e **Salvar ROI**. O recorte deve ter ao menos 32 × 32 pixels e ocupar no máximo 75% da imagem, preservando entorno para controle da cena.
5. Deixe o compartimento vazio, afaste as mãos e clique **Calibrar VAZIO**. Reponha os objetos, afaste as mãos e clique **Calibrar PRESENTE**. Cada comando coleta cinco quadros estáveis. As duas referências devem produzir `calibrated: true`.
6. Use **Testar leitura** para confirmar `present`. Configure janela de 60 s e intervalo de 5 s e salve a agenda. Clique **Demonstração: iniciar em 10 s**.
7. Retire o objeto dentro da janela, afaste a mão e aguarde duas leituras vazias: deve aparecer **RETIRADA APARENTE CONFIRMADA**. Em um segundo ciclo, reponha, rearme e deixe o objeto: deve aparecer **ALERTA** no fim da janela. Cubra a cena em outro ensaio: o resultado deve ser **FALHA**, não retirada.
8. **Reconhecer / silenciar** apenas reconhece o resultado. Um novo ciclo exige reposição e **novo armamento**, que captura e verifica a presença novamente.

No uso da agenda, **Armar para o horário** seleciona a próxima ocorrência do horário salvo. Se ele já passou, agenda o dia seguinte. Não há repetição automática diária: cada dose exige reposição e rearmamento. Para a tolerância de 45 minutos da proposta antiga, use `window_s=2700`.

O som depende do painel aberto e das configurações de áudio do sistema. Desligar a USB reinicia o requisito de hora/rearmamento. Conectar a serial pode reiniciar algumas placas por particularidades do gravador; o painel não envia intencionalmente o pulso DTR/RTS de reset.

## Console e registros

```powershell
.\.venv\Scripts\python.exe tools/console.py COM5
```

Digite `time`, `status`, `snapshot`, `sample`, `ack`, `cancel` ou comandos JSON. A lista completa está em [protocolo USB](docs/PROTOCOLO.md). Console e painel salvam logs em `logs/*.jsonl`; imagens só saem pela USB mediante `snapshot`, com o ciclo inativo. Não há conexão Wi-Fi, envio de imagens para servidores nem serviço de rede nesta versão.

## Testes

```powershell
.\.venv\Scripts\python.exe -m pip install ziglang==0.13.0
.\.venv\Scripts\python.exe tools/run_tests.py
```

Em Linux, com g++ disponível: `python tools/run_tests.py --cxx g++`. Os testes compilam o núcleo C++ efetivo, sem simular sua implementação em outra linguagem. Casos incluem limites da ROI, rejeição de referências, oclusão simulada, persistência temporal, prazo vencido, captura atrasada, reconhecimento e rearmamento. Testes sintéticos não medem acurácia com medicamentos reais.

Leia o [protocolo de bancada](docs/VALIDACAO.md) para executar os ensaios de 30 cheio + 30 vazio + 10 oclusões e 20 ciclos. [Arquitetura e rastreabilidade](docs/ARQUITETURA.md) mapeiam as funções aos requisitos. [Verificação de software](docs/VERIFICACAO.md) registra o que foi efetivamente executado nesta entrega.

## Estrutura

Para a documentação consolidada da implementação, consulte [DOCUMENTACAO_IMPLEMENTACAO.md](DOCUMENTACAO_IMPLEMENTACAO.md).

```text
components/auditor/   visão e máquina de estados C++ independentes do hardware
main/                câmera, NVS, serial e integração ESP-IDF
tools/               painel, console, avaliação e executor de testes
tests/               testes do núcleo e do protocolo
docs/                montagem, arquitetura, protocolo e validação
Documentacao/        materiais originais do grupo
```
