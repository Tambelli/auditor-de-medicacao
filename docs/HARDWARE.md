# Hardware e enquadramento

A compra informada é o [ESP32-CAM com gravador integrado da RoboCore](https://www.robocore.net/wifi/esp32-cam-com-gravador-integrado). A página descreve ESP32 LX6, OV2640, flash de 4 MB e ausência de slot MicroSD; não fornece esquema elétrico definitivo da câmera. Não se deve transferir automaticamente as especificações das propostas com ESP32-S3 para esta placa.

## Perfis implementados

Selecione um perfil apenas depois de conferir a unidade/esquema correspondente. A identificação fornecida pelo grupo é “ESP32-CAM”, sem confirmação independente da pinagem interna. O perfil padrão é **não confirmado**. A seleção ocorre pela serial e fica em NVS; não é preciso recompilar.

| Sinal da câmera | AI Thinker, perfil 1 | WROVER KIT, perfil 2 |
|---|---:|---:|
| PWDN | 32 | não utilizado |
| RESET | não utilizado | não utilizado |
| XCLK | 0 | 21 |
| SCCB SDA / SCL | 26 / 27 | 26 / 27 |
| D0, D1, D2, D3 | 5, 18, 19, 21 | 4, 5, 18, 19 |
| D4, D5, D6, D7 | 36, 39, 34, 35 | 36, 39, 34, 35 |
| VSYNC / HREF / PCLK | 25 / 23 / 22 | 25 / 23 / 22 |

Origem dos perfis: [CameraWebServer da Espressif, versão 2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h). Esse arquivo é usado como referência de pinagem; o firmware deste projeto utiliza ESP-IDF diretamente. Um módulo escrito “WROVER” no encapsulamento não prova que a placa siga WROVER KIT.

Se nenhum perfil corresponder ao esquema da unidade, ajuste o mapeamento em `main/camera.cpp` e documente a revisão. Erro de inicialização não determina por si só qual perfil é correto: pode resultar de cabo, alimentação ou sensor. Não há descoberta automática confiável de pinagem.

## Montagem sem eletrônica adicional

1. Encaixe o flat da OV2640 com a placa desligada, respeitando o lado dos contatos do conector. A câmera acompanha o módulo, mas pode vir desconectada.
2. Use a USB para alimentação e gravação; mantenha o cabo de dados disponível para configuração e som no computador.
3. Fixe a câmera e a caixa usando um suporte já disponível. Não apoie objetos sobre contatos energizados.
4. Enquadre um compartimento e parte do seu entorno. Use iluminação ambiente constante. A câmera não ilumina a cena nesta implementação.
5. Não ligue buzzer, LED, RTC, OLED nem cartão SD. O firmware não aciona saídas auxiliares. Botões BOOT e RESET conservam as funções de gravação/reinício.

O driver captura JPEG 160 × 120, converte em cinza e reduz somente o recorte a 32 × 32. Para a caixa ainda a comprar, prefira geometria que permita observar diretamente o conteúdo de um compartimento; reflexos, tampas opacas, divisórias e comprimidos da mesma cor do fundo reduzem a separação. A adequação precisa ser demonstrada pela calibração e pelos ensaios, sem atribuir precisão antecipadamente.

## Limites da configuração

Não é possível produzir som no ESP32 usando apenas software se a placa não possuir transdutor. O som desta entrega é do computador via painel. Sem ele, o estado lógico continua no ESP32 e pode ser consultado na serial, mas não existe alerta audível autônomo, remoto ou armazenado com histórico persistente. O histórico completo é coletado no computador; somente configuração e referências ficam na flash.

A hora não sobrevive como configuração válida a um reinício: é obrigatório ajustá-la novamente. Não há compromisso de autonomia por bateria nem medição de consumo. O chip pode ter recursos de baixo consumo, mas eles não comprovam baixo consumo do conjunto com gravador USB e regulador.
