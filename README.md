# Relatório do T1

## Instruções

### Compilar e executar

- Ajustar parâmetros desejados no [cfg.h](cfg.h)

`make`

`./kernelsim`

### Pausar/continuar simulação

`pkill -SIGUSR1 kernelsim`

## Escolhas de IPC

### Pipes

Utilizamos um pipe desde o início para enviar os interrupts do intersim ao kernelsim. Já para enviar os pedidos de syscall dos apps ao kernel, inicialmente utilizamos os sinais SIGUSR, mas percebemos que isso acabou gerando muitos problemas de concorrência, e decidimos refatorar para esses pedidos serem, também, enviados por um pipe. Como muitas alterações foram necessárias, isso gerou a versão atual v2 do trabalho. A v1 pode ser encontrada [aqui](https://github.com/bathwaterpizza/t1_inf1316).

Os dados enviados através dos pipes são apenas inteiros com significados especiais, que estão definidos nas enums em [types.h](types.h). No caso do pipe de pedidos de syscall, o dado é o app_id do app que enviou o pedido, e então o pedido em si é extraído da shm entre o kernel e o app. No loop principal do kernel, utilizamos o `select()` para ler vários pipes ao mesmo tempo sem que se bloqueiem.

### Sinais

O dispatcher utiliza sinais para parar e continuar a execução dos apps conforme sugerido, mas na verdade enviamos um SIGUSR1 como stop aos apps, cujo handler em seguida salva seu contexto com o kernel e levanta um SIGSTOP para si próprio. Se utilizássemos o SIGSTOP diretamente, não haveria como cadastrar um handler para salvar contexto, pois é um dos sinais não mascaráveis do SO.

Todos os programas também possuem handlers de SIGINT ou SIGTERM, para os encerrar com um cleanup adequado. Os apps possuem um handler de SIGSEGV para debug, como segfaults de children não são anunciadas no stdout ou stderr por padrão.

Além disso, utilizamos o SIGUSR1 no kernelsim para pausar e continuar a simulação. Ao receber o sinal, o kernel pausa todos os outros processos do sistema simulado, e mostra um dump do estado de cada app. Foram necessários vários ajustes para essa funcionalidade não interferir no funcionamento do sistema, como o uso da versão thread-safe de localtime em nossa função `msg()`, e o handling do erro `EINTR` que ocorre quando uma syscall é interrompida por um interrupt de sinal.

### Memória compartilhada

todo
