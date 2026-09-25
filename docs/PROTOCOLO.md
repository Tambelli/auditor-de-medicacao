# Protocolo USB

UART0, **115200, 8N1**, comandos JSON terminados por LF. Máximo de **511 bytes por comando**, sem contar LF. Respostas e eventos em JSON Lines; o bootloader/ESP-IDF também podem imprimir linhas de diagnóstico não JSON. O painel preserva essas linhas no log e ignora-as no parser.

Cada comando pode incluir `id` numérico; a resposta `event=reply` repete esse identificador e contém `ok` e `message`. Eventos assíncronos incluem `ms` (milissegundos monotônicos desde boot) e `epoch` UTC (zero se a hora não foi configurada). Timestamps `ms` só são únicos dentro de um boot. A fila de comandos comporta oito itens; aguarde `reply` antes de enviar o próximo comando demorado.

## Comandos

| Comando | Exemplo | Resultado |
|---|---|---|
| Consultar | `{"cmd":"status","id":1}` | Estado, configuração, referências, disponibilidade e heap |
| Hora | `{"cmd":"time","epoch":1790334000}` | Unix UTC inteiro, entre 2024 e 2100; valida hora neste boot |
| Perfil | `{"cmd":"set","profile":1}` | 0 desativado; 1 AI Thinker; 2 WROVER KIT; trocar apaga referências |
| ROI | `{"cmd":"set","roi":[40,24,80,72]}` | x,y,largura,altura; trocar apaga referências |
| Agenda | `{"cmd":"set","hour":8,"minute":0,"window_s":60,"interval_s":5,"confirmations":2}` | Salva agenda; não arma automaticamente |
| Limiares | `{"cmd":"set","max_distance":18,"margin":5,"context_distance":20,"stability":8}` | Ajuste experimental, com ciclo inativo |
| Vazio | `{"cmd":"calibrate","label":"empty"}` | Coleta cinco amostras, valida estabilidade e salva referência |
| Presente | `{"cmd":"calibrate","label":"present"}` | Idem; valida separação entre classes quando ambas existem |
| Limpar referências | `{"cmd":"clear_calibration"}` | Remove referências e desarma |
| Imagem | `{"cmd":"snapshot"}` | Imagem cinza 160 × 120 em base64, apenas USB |
| Leitura de teste | `{"cmd":"sample"}` | Classifica e registra, sem mudar o ciclo |
| Armar agenda | `{"cmd":"arm"}` | Verifica presença e arma a próxima ocorrência do horário |
| Armar demonstração | `{"cmd":"arm","delay_s":10}` | Início relativo de 1 a 86400 segundos; zero equivale à agenda |
| Reconhecer | `{"cmd":"ack"}` | Marca alerta/falha como reconhecido; resultado é preservado |
| Cancelar | `{"cmd":"cancel"}` | Desarma, cancela o resultado ativo; não declara retirada |

Durante `armed` ou `monitoring`, somente `status`, `ack` e `cancel` são aceitos. Alterações de hora/ROI/perfil/referências e transmissão de imagens não podem concorrer com o ciclo. Se um comando chega durante uma captura, sua execução aguarda o retorno do driver. A serial é recebida por tarefa separada, porém isso não elimina o tempo limite interno de aquisição.

## Validação e persistência

- ROI de no mínimo 32 × 32, dentro de 160 × 120 e ocupando no máximo 75% da cena.
- Horas 0–23, minutos 0–59; intervalo de 1–3600 s, janela até 86400 s e suficiente para o número de confirmações (2–10).
- `max_distance`: 1–80; `margin`: 1–40; `context_distance`: 1–80; `stability`: 1–40. Valores não finitos são rejeitados.
- Alterações são persistidas antes de serem aplicadas. Erro NVS preserva a configuração anterior. Nunca se apaga NVS automaticamente por falta de espaço ou mudança de versão.
- A versão de esquema e um checksum detectam alterações acidentais. NVS também aplica suas verificações de integridade. O checksum da aplicação não é autenticação criptográfica.
- A calibração nova com ambas as referências deve ter distância entre classes maior que `2 * margin` e variação do entorno menor ou igual a `stability`.
- Hora válida, resultado, reconhecimento e ciclo ativo não são restaurados após reset. Não se deduz retirada no período desligado.

`set` responde sucesso quando os parâmetros foram persistidos; a mensagem e `camera_ready` informam separadamente se a câmera iniciou. Uma configuração persistida não comprova que a pinagem selecionada corresponda à placa.

## Eventos

- `boot`: versão de firmware e resultado da leitura de NVS.
- `status`: estado atual; emitido também a cada 10 segundos.
- `observation`: classe, estabilidade, sucesso de captura, distâncias e duração da leitura completa.
- `transition`: mudança de estado da agenda para monitoramento/resultado.
- `reply`: confirmação/erro do comando.
- `image`: largura, altura e `gray_base64`, uma imagem de desenvolvimento solicitada explicitamente.

`removed` significa retirada aparente confirmada em duas ou mais observações vazias após presença inicial. `alert` significa presença observada recentemente ao final da tolerância. `fault` significa evidência insuficiente, leitura inconclusiva ou captura atrasada/ausente. Um único vazio sem a persistência exigida termina em `fault`, não em `removed`.

O relógio exibido no agendamento é **UTC−3 fixo**, sem política de horário de verão. Os prazos ativos usam relógio monotônico de 64 bits, evitando wrap de 32 bits e ajustes do relógio civil. A tolerância começa no horário programado, não no instante em que uma captura atrasada conseguir retornar.
