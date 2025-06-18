# DOCUMENTAÇÃO PARA EXECUTAR O ESTRESSADOR DE DISCO

Esse projeto foi criado para rodar em uma maquina virtual, utilizamos a Oracle VirutalBox. Na maquina virtual utilizamos o "ubuntu-24.04.2-live-server-amd64" como ISO. Abaixo vou listar os comandos necessários para rodar o estressador.

## Pré-requisitos

Ter o GCC (GNU Compiler Collection) instalado em sua maquina, para poder compilar o arquivo `main.c`

```
sudo apt update
sudo apt install build-essential -y
```

## Compilação do Programa

1. Abra um terminal e acesse o diretório aonde você salvou o arquivo `main.c`

   `cd /caminho/para/diretório`

2. Compile o programa usando o GCC com o seguinte comando:
   
   `gcc main.c -o disk_stressor`

   - `main.c`: É o nome do seu arquivo de código-fonte.
   - `-o disk_stressor`: Define o nome do arquivo executável de saída.

   Caso de erro adicione o comando `-lrt` no final do código

   `gcc main.c -o disk_stressor -lrt`

   - `-lrt`: Linka com a biblioteca de tempo real, necessária para `mmap` em algumas configurações.

   Se a compilação for bem-sucedida, nenhum erro será exibido e um arquivo executável chamado `disk_stressor` será criado no mesmo diretório.

3. Execute o estressador com o seguinte comando:

   `./disk_stressor <Tempo de duração em segundos>`