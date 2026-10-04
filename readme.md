# Banana Pi M2 Zero com gráficos mínimos

O objetivo deste repositório é servir como uma segunda etapa do projeto [Banana Pi M2 Zero do Zero](https://github.com/MatheusAlvesA/banana-pi-m2-zero-kernel). Conclua aquele projeto antes de começar este para entender como a base do sistema foi construída.

Neste projeto, vamos substituir o programa `start.c` da etapa anterior por uma nova versão, capaz de mostrar gráficos simples na tela e vinculada dinamicamente às bibliotecas necessárias.

## Dependências

O programa `start` depende das bibliotecas compartilhadas `libc` e `libdrm`, que precisam estar disponíveis no cartão SD do Banana Pi. Como o sistema do projeto anterior foi construído do zero, também é necessário incluir o carregador dinâmico `ld-linux-armhf.so.3`.

Os arquivos necessários já estão disponíveis na pasta `lib/` deste repositório. Copie seu conteúdo para `/lib` na partição raiz do cartão SD, preservando o link simbólico `libdrm.so.2`, que aponta para `libdrm.so.2.134.0`. Copie também o conteúdo de `bin/`, `sbin/` e `boot/` para os diretórios correspondentes no cartão, seguindo a estrutura do projeto anterior.

### Buildroot

Embora as bibliotecas estejam disponíveis neste repositório, você pode compilá-las por conta própria. Elas foram geradas com o [Buildroot](https://buildroot.org/).

Baixe o Buildroot, descompacte-o, entre na pasta pelo terminal e execute `make menuconfig`, conforme o [manual do Buildroot](https://buildroot.org/downloads/manual/manual.html). Configure a arquitetura ARM de 32 bits, o processador Cortex-A7, a ABI de ponto flutuante hard-float, a biblioteca C glibc e o pacote `libdrm`, com suporte a bibliotecas compartilhadas. Depois, execute `make`. A compilação pode levar bastante tempo.

Os arquivos necessários estarão nos seguintes caminhos, relativos à pasta do Buildroot:

- `output/target/lib/ld-linux-armhf.so.3`
- `output/target/lib/libc.so.6`
- `output/target/usr/lib/libdrm.so.2`
- `output/target/usr/lib/libdrm.so.2.134.0`

O número da versão de `libdrm` pode variar conforme a versão do Buildroot e a configuração utilizada. Copie as bibliotecas e o carregador para `/lib` no cartão SD, incluindo os arquivos apontados por eventuais links simbólicos e preservando esses links. Assim, o sistema terá a biblioteca C (`libc`) e a biblioteca de acesso à interface gráfica DRM/KMS do kernel (`libdrm`).

## Compilação de `start.c`

O arquivo `bin/start.c` implementa um programa simples que executa as seguintes etapas:

1. Procura um dispositivo DRM em `/dev/dri/card*` com uma saída de vídeo utilizável.
2. Seleciona um conector conectado, um controlador de exibição (CRTC) compatível e o modo de vídeo preferido, ou o primeiro modo disponível.
3. Assume o controle do dispositivo como DRM master e prepara um buffer de pixels.
4. Preenche o buffer de vermelho e exibe a imagem em toda a tela.
5. Aguarda 10 segundos, libera os recursos e encerra. Se estiver executando como PID 1, permanece ativo após liberar os recursos.

O kernel precisa oferecer suporte a DRM/KMS e disponibilizar um dispositivo compatível em `/dev/dri`. O programa também precisa de permissão para acessar esse dispositivo e assumir o controle como DRM master. O `sbin/init.c` deste projeto monta `/dev` e inicia `/bin/start` como processo filho durante a inicialização.

### Opção 1: compilador cruzado do projeto anterior

Para compilar, execute o comando abaixo na raiz deste repositório, usando o mesmo compilador cruzado do projeto anterior. Também são necessários os cabeçalhos de desenvolvimento da `libdrm`, incluindo as funções de dumb buffer usadas no código. O exemplo considera que esses cabeçalhos estão em `/usr/include/libdrm`; ajuste o caminho se estiverem em outro diretório e use versões compatíveis com as bibliotecas ARM do cartão SD.

```sh
arm-linux-gnueabihf-gcc \
    -Wall -Wextra -O2 -mcpu=cortex-a7 \
    -I/usr/include/libdrm \
    bin/start.c \
    -L"$PWD/lib" \
    -Wl,-rpath-link,"$PWD/lib" \
    -l:libdrm.so.2 \
    -o bin/start
```

### Opção 2: toolchain do Buildroot

Se você já baixou e compilou o Buildroot conforme as instruções acima, pode usar a própria toolchain gerada por ele. Conforme o [manual do Buildroot](https://buildroot.org/downloads/manual/manual.html), as ferramentas ficam em `output/host/bin/`, e `output/staging/` aponta para o sysroot, que contém os cabeçalhos e as bibliotecas do sistema de destino.

Execute o comando abaixo na raiz deste repositório. O exemplo considera que o Buildroot está na pasta `buildroot/` dentro do projeto e usa a toolchain interna para ARM com glibc e hard-float:

```sh
BUILDROOT_DIR="$PWD/buildroot"
BUILDROOT_CC="$BUILDROOT_DIR/output/host/bin/arm-buildroot-linux-gnueabihf-gcc"
BUILDROOT_SYSROOT="$BUILDROOT_DIR/output/staging"

"$BUILDROOT_CC" \
    --sysroot="$BUILDROOT_SYSROOT" \
    -Wall -Wextra -O2 -mcpu=cortex-a7 \
    -I"$BUILDROOT_SYSROOT/usr/include/libdrm" \
    bin/start.c \
    -ldrm \
    -o bin/start
```

Ajuste `BUILDROOT_DIR` se a pasta tiver outro nome, como `buildroot-2026.02`, ou estiver em outro local. Se o compilador tiver outro prefixo, ajuste também `BUILDROOT_CC` para o executável correspondente em `output/host/bin/`. Caso tenha usado um diretório de saída personalizado com `O=`, substitua os caminhos de `output/` pelo diretório escolhido.

Essa opção usa os cabeçalhos e as bibliotecas do Buildroot durante a compilação. Copie o novo `bin/start` para `/bin/start` no cartão SD e use em `/lib` as bibliotecas e o carregador dinâmico dessa mesma compilação do Buildroot, preservando os links simbólicos conforme explicado acima.

#### Buildroot copiado de outro diretório

Ao copiar um Buildroot já compilado, o link simbólico `output/staging` pode continuar apontando para o diretório original. Se esse caminho não existir, a compilação poderá falhar com `fatal error: dirent.h: No such file or directory`, mesmo que os cabeçalhos estejam presentes na cópia.

Para a toolchain usada no exemplo, recrie o link com um caminho relativo e execute novamente o comando de compilação:

```sh
ln -sfn host/arm-buildroot-linux-gnueabihf/sysroot "$BUILDROOT_DIR/output/staging"
```

Se a toolchain tiver outro prefixo, ajuste também o nome do diretório dentro de `host/`.
