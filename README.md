# Relatório do T1

Neste relatório, procuramos explicar as principais decisões de projeto tomadas para implementar com sucesso o simulador pedido no enunciado do trabalho. O resultado foi uma aplicação robusta, que atendeu aos requisitos e satisfez todos os nossos testes.

## Instruções

### Compilar e executar

- Ajustar parâmetros desejados no [cfg.h](cfg.h)

- `make`

- `./kernelsim`

### Pausar/continuar simulação

- `pkill -SIGUSR1 kernelsim`

## Escolhas de IPC

### Pipes

Utilizamos um pipe desde o início para enviar os interrupts do intersim ao kernelsim. Já para enviar os pedidos de syscall dos apps ao kernel, inicialmente utilizamos os sinais SIGUSR, mas percebemos que isso acabou gerando muitos problemas de concorrência, e decidimos refatorar para esses pedidos serem, também, enviados por um pipe. Como muitas alterações foram necessárias, isso gerou a versão atual v2 do trabalho. A v1 pode ser encontrada [aqui](https://github.com/bathwaterpizza/t1_inf1316).

Os dados enviados através dos pipes são apenas inteiros com significados especiais, que estão definidos nas enums em [types.h](types.h). No caso do pipe de pedidos de syscall, o dado é o app_id do app que enviou o pedido, e então o pedido em si é extraído da shm entre o kernel e o app. No loop principal do kernel, utilizamos o `select()` para ler vários pipes ao mesmo tempo, sem que se bloqueiem.

### Sinais

O dispatcher utiliza sinais para parar e continuar a execução dos apps conforme sugerido, mas, na verdade enviamos um SIGUSR1 como stop aos apps, cujo handler em seguida salva seu contexto com o kernel e envia um SIGSTOP para si próprio. Se utilizássemos o SIGSTOP diretamente, não haveria como cadastrar um handler para salvar contexto, pois é um dos sinais não-mascaráveis do SO.

Todos os programas também possuem handlers de SIGINT ou SIGTERM, para os encerrar com um cleanup adequado. Os apps possuem um handler de SIGSEGV para debug, como segfaults de children não são anunciadas no stdout ou stderr por padrão.

Além disso, utilizamos o SIGUSR1 no kernelsim para pausar e continuar a simulação. Ao receber o sinal, o kernel pausa todos os outros processos do sistema simulado, e mostra um dump do estado de cada app. Foram necessários vários ajustes para essa funcionalidade não interferir no funcionamento do sistema, como o uso da versão thread-safe de localtime em nossa função `msg()`, e o handling do erro `EINTR` que ocorre quando uma syscall é interrompida por um sinal.

### Memória compartilhada

Para cada app, o kernel aloca dois inteiros em shm. O primeiro armazena o estado de seu Program Counter, e o segundo armazena uma eventual syscall pendente, mas que também utilizamos para informar ao kernel que o app terminou sua execução. Essa shm é efetivamente nossa interpretação do kernel salvando o contexto do app, tanto que forçamos a perda dos dados imediatamente após salvar o contexto, no momento em que um app é interrompido pelo scheduler:

```C
static void handle_kernel_stop(int signum) {
  (...)
  // Save program counter state to shm
  set_app_counter(shm, app_id, counter);

  // Simulate data loss
  counter = 0;
  (...)
}
```

Em alguns casos, o acesso a shm estava gerando segfaults, por exemplo, em uma situação na qual o kernel tenta verificar a syscall pendente de um app para tomar uma decisão de dispatching, no momento em que o mesmo a modifica. Resolvemos isso encapsulando a função de dispatch e as escritas na parte de syscall da shm com um semáforo.

## Time-sharing

Ao receber o `IRQ_TIME` do intersim, o kernel executa nosso dispatcher, que funciona conforme um round-robin: o app em execução é pausado e inserido na fila de espera, e o próximo da fila é continuado. Apps que pedirem syscalls são bloqueados e entram na fila de um dispositivo, até a chegada de um `IRQ_D1` ou `IRQ_D2` correspondente os liberar, e então são inseridos na fila de espera.

Como mencionado anteriormente, foi importante encapsular o dispatcher em um semáforo que garante sua execução não concorrente com a decisão de pedido de syscall do app em execução, para evitar condições de corrida. Outro detalhe é que o dispatcher precisa checar uma série de edge cases, por exemplo, quando não há um app a ser continuado (todos bloqueados por syscalls), ou quando o chaveamento não é necessário (apenas um app está disponível para execução).

## Módulo util

São algumas funções compartilhadas entre os programas do simulador.

- Criamos o `msg` e o `dmsg` para adicionar um prefixo de timestamp em todas as mensagens, e utilizamos o `fflush()` ao fim de cada uma para garantir que não há delays por bufferização.
- As funções de get e set são apenas uma forma conveniente de acessar a shm entre os apps e o kernel, sem precisar reescrever o cálculo de offset.
- As funções do TAD de queue são uma implementação simples que aproveitamos da disciplina de EDA, para as filas de espera do sistema.

## Testes

Como a grande maioria dos erros provém de problemas de concorrência aleatórios, e não erros determinísticos a partir de parâmetros, decidimos testar a aplicação manualmente durante todo o desenvolvimento, a cada nova funcionalidade implementada no simulador.

Uma parte importante foi o esforço para entender o que está acontecendo durante a execução, o que alcançamos através do uso de asserts e prints não bufferizados de debug, com timestamps de precisão em milissegundos, em pontos-chave de todo o programa. Os binários também foram compilados com a opção `-g`, de maneira que podemos dar attach com o gdb a qualquer um dos processos em execução, para investigar travamentos e estados inesperados. Essa análise com o gdb foi crucial para, por exemplo, entender que a função `pause()` estava bloqueando nosso pause handler de receber outros sinais, o que nos levou à troca pela função `sigsuspend()`, que não altera a máscara de sinais do processo.

Sobre os testes em si, a cada alteração no programa executamos dois tipos de cfg: um lento (1000ms para o app sleep e 500ms para o intersim) com 3 apps e 10 para o max PC, o que nos possibilita visualizar os passos individuais do simulador com calma, e outro rápido (100ms para o app sleep e 50ms para o intersim), com 10 apps e 100 para o PC, como uma espécie de teste de estresse. Isso nos permitiu identificar erros que ocorrem raramente em apenas uma das situações acima.

Por fim, realizamos alguns testes de valor limite com as probabilidades, para verificar que a simulação estava reagindo corretamente às mudanças.
