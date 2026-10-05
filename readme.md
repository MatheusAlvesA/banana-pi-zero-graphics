# Banana Pi M2 Zero com gráficos mínimos

O objetivo deste repositório é servir como uma segunda etapa do projeto [Banana Pi M2 Zero do Kernel](https://github.com/MatheusAlvesA/banana-pi-m2-zero-kernel). Conclua aquele projeto antes de começar este para entender como a base do sistema foi construída.

Neste projeto, vamos substituir o programa `start.c` da etapa anterior por uma nova versão, capaz de mostrar gráficos simples na tela e vinculada dinamicamente às bibliotecas necessárias.

O repositório traz duas versões desse programa:

- `bin/start.c`: desenha diretamente com DRM/KMS, usando apenas `libc` e `libdrm`.
- `bin/start_gl.c`: desenha com a GPU usando OpenGL ES 2.0 por meio da [Mesa3D](https://mesa3d.org/). Veja a seção [Versão com OpenGL ES (Mesa3D)](#versão-com-opengl-es-mesa3d).

Apenas uma delas é usada por vez, sempre instalada no cartão SD como `/bin/start`.

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

## Versão com OpenGL ES (Mesa3D)

O arquivo `bin/start_gl.c` é uma alternativa ao `bin/start.c`. Em vez de preencher um buffer de pixels pela CPU, ele usa a GPU da placa para desenhar com OpenGL ES 2.0, por meio da Mesa3D.

### Como a pilha gráfica funciona

O Allwinner H2+ da Banana Pi M2 Zero tem uma GPU Mali-400 MP2. No Linux, ela é atendida pelo driver `lima` do kernel e pelo driver `lima` da Mesa. A saída de vídeo e a GPU são dispositivos DRM separados:

| Dispositivo | Driver | Função |
|---|---|---|
| `/dev/dri/card0` | `sun4i-drm` | Saída de vídeo (modeset, HDMI) |
| `/dev/dri/card1` | `lima` | GPU, sem conectores |
| `/dev/dri/renderD128` | `lima` | Renderização |

A numeração dos nodes `card*` pode variar entre inicializações. Por isso os programas procuram o dispositivo que tem um conector conectado, em vez de usar um número fixo.

A Mesa liga os dois dispositivos com o driver `kmsro` ("render-only"): o programa abre o node do `sun4i-drm`, e a Mesa usa a GPU do `lima` para renderizar em buffers que podem ser exibidos na tela. Como o sistema não tem X11 nem Wayland, a janela é criada com GBM e EGL diretamente sobre o DRM/KMS.

A Mali-400 suporta OpenGL ES 2.0. Não há suporte a OpenGL ES 3.x, e o OpenGL desktop exposto pela Mesa nessa GPU é incompleto. Por isso o programa usa OpenGL ES 2.0.

### Kernel

O kernel em `boot/zImage` já inclui os drivers necessários: `sun4i-drm`, `sun8i-mixer`, `sun8i-dw-hdmi` e `lima`. Se compilar o seu próprio kernel no projeto anterior, confirme que `CONFIG_DRM_SUN4I`, `CONFIG_DRM_SUN8I_MIXER`, `CONFIG_DRM_SUN8I_DW_HDMI` e `CONFIG_DRM_LIMA` estão habilitados. O `sunxi_defconfig` já inclui esses drivers.

Os buffers exibidos na tela precisam de memória fisicamente contígua, reservada pelo parâmetro `cma=128M` que o `boot/boot.cmd` passa ao kernel. A correção de `mali-supply` no mesmo script permite que o `lima` controle a frequência da GPU.

### Configuração do Buildroot

A Mesa e suas dependências são compiladas com o Buildroot 2026.08, que inclui a Mesa 26.1.8. Baixe o Buildroot, descompacte-o na pasta `buildroot/` dentro deste projeto, que é ignorada pelo git, e execute `make menuconfig` dentro dela.

Partindo da configuração padrão, ajuste as opções abaixo. Os nomes entre parênteses são os símbolos gravados no arquivo `.config`.

**Target options**

- Target Architecture: `ARM (little endian)` (`BR2_arm`)
- Target Architecture Variant: `cortex-A7` (`BR2_cortex_a7`)
- Target ABI: `EABIhf` (`BR2_ARM_EABIHF`)
- Floating point strategy: `VFPv4-D16` (`BR2_ARM_FPU_VFPV4D16`)

**Toolchain**

- C library: `glibc` (`BR2_TOOLCHAIN_BUILDROOT_GLIBC`)
- Kernel Headers: `Linux 7.1.x kernel headers` (`BR2_KERNEL_HEADERS_7_1`)
- GCC compiler Version: `gcc 15.x` (`BR2_GCC_VERSION_15_X`)
- `Enable C++ support` (`BR2_TOOLCHAIN_BUILDROOT_CXX`). A Mesa exige C++. Habilite antes da primeira compilação, pois mudar essa opção depois obriga a recompilar toda a toolchain.

**Target packages → Graphic libraries and applications (graphic/text)**

- Em *Graphic libraries*, habilite `mesa3d` (`BR2_PACKAGE_MESA3D`) e, dentro dele:
  - `Gallium lima driver` (`BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_LIMA`)
  - `gbm` (`BR2_PACKAGE_MESA3D_GBM`)
  - `OpenGL EGL` (`BR2_PACKAGE_MESA3D_OPENGL_EGL`)
  - `OpenGL ES` (`BR2_PACKAGE_MESA3D_OPENGL_ES`)
- Em *Graphic applications*, habilite `kmscube` (`BR2_PACKAGE_KMSCUBE`), usado para testar a pilha gráfica.

Não habilite nenhum outro driver da Mesa, nem LLVM, X.org ou `libglvnd`. Sem `libglvnd`, as bibliotecas `libEGL` e `libGLESv2` são as da própria Mesa e não dependem de arquivos de configuração em `/usr/share`. O pacote `libdrm` é selecionado automaticamente pela Mesa.

As demais opções podem ficar no padrão do Buildroot, incluindo as de proteção (`-fstack-protector-strong`, PIE, RELRO e `_FORTIFY_SOURCE`).

Salve a configuração e compile:

```sh
make
```

A primeira compilação leva bastante tempo, pois inclui a toolchain com C++ e a Mesa.

A glibc é configurada para a versão dos cabeçalhos do kernel escolhida, e os programas gerados exigem um kernel 7.1 ou mais recente. O kernel em `boot/zImage` é o 7.2.

### Conferindo o resultado

Ao final, confira se as bibliotecas da Mesa foram geradas:

```sh
ls -l output/target/usr/lib/ output/target/usr/lib/gbm/
```

As bibliotecas principais ficam em `output/target/usr/lib/`: `libEGL`, `libGLESv2`, `libgbm`, `libgallium-26.1.8.so` e `libdrm`, além de dependências como `libstdc++`, `libexpat` e `libz`. A pasta `output/target/usr/lib/gbm/` deve conter `dri_gbm.so`, o backend que a `libgbm` carrega em tempo de execução.

Na Mesa 26 não existe mais a pasta `usr/lib/dri/` com um arquivo por driver. Os drivers `lima` e `kmsro` ficam dentro da `libgallium-26.1.8.so`, usada diretamente pela `libEGL`, pela `libGLESv2` e pelo `dri_gbm.so`. O `kmsro` não tem uma opção própria no Buildroot: ele é compilado junto com o driver `lima` e usado automaticamente para dispositivos de exibição sem driver de GPU próprio, como o `sun4i-drm`.

### Cópia para o cartão SD

Com a Mesa, a lista de bibliotecas é longa, e parte delas, como o backend `/usr/lib/gbm/dri_gbm.so`, é carregada em tempo de execução por `dlopen`, sem aparecer nas dependências dos executáveis. Por isso, copie as pastas de bibliotecas inteiras, em vez de arquivos individuais. Na pasta do Buildroot, com a partição raiz do cartão montada em `/media/usuario/main`:

```sh
sudo mkdir -p /media/usuario/main/usr/lib /media/usuario/main/usr/bin
sudo cp -a output/target/lib/.     /media/usuario/main/lib/
sudo cp -a output/target/usr/lib/. /media/usuario/main/usr/lib/
sudo cp -a output/target/usr/bin/kmscube /media/usuario/main/usr/bin/
```

Ajuste o ponto de montagem para o da sua máquina. O `cp -a` preserva os links simbólicos. Essas bibliotecas substituem as da pasta `lib/` deste repositório: a `libc`, o carregador dinâmico e a `libdrm` precisam vir da mesma compilação que a Mesa.

Não extraia no cartão a imagem `output/images/rootfs.tar`. A configuração padrão do Buildroot instala o init do BusyBox em `/sbin/init`, e ele substituiria o `sbin/init` deste projeto.

O carregador dinâmico procura bibliotecas em `/lib` e `/usr/lib` por padrão, então não é necessário criar `/etc/ld.so.cache`. A `libgbm` procura seu backend em `/usr/lib/gbm`.

### Testando com o kmscube

Antes de usar o `start_gl`, valide a pilha gráfica com o `kmscube`, que desenha um cubo girando usando GBM, EGL e OpenGL ES 2.0. Como o `init` executa `/bin/start`, crie temporariamente um link para ele no cartão:

```sh
sudo ln -sf /usr/bin/kmscube /media/usuario/main/bin/start
```

Se o cubo aparecer no HDMI, o kernel, a Mesa e o driver `lima` estão funcionando. Para voltar a usar o seu programa, remova o link e copie novamente o executável para `/bin/start`.

### Compilação de `start_gl.c`

O `start_gl.c` precisa dos cabeçalhos e das bibliotecas da Mesa gerados pelo Buildroot, então use a toolchain do Buildroot, como na Opção 2 acima. O compilador cruzado do Ubuntu não serve aqui, pois os cabeçalhos da Mesa do sistema não correspondem às bibliotecas do cartão.

Execute na raiz deste repositório:

```sh
BUILDROOT_DIR="$PWD/buildroot"
BUILDROOT_CC="$BUILDROOT_DIR/output/host/bin/arm-buildroot-linux-gnueabihf-gcc"
BUILDROOT_SYSROOT="$BUILDROOT_DIR/output/staging"

"$BUILDROOT_CC" \
    --sysroot="$BUILDROOT_SYSROOT" \
    -Wall -Wextra -O2 -mcpu=cortex-a7 \
    -I"$BUILDROOT_SYSROOT/usr/include/libdrm" \
    bin/start_gl.c \
    -lEGL -lGLESv2 -lgbm -ldrm \
    -o bin/start_gl
```

Copie `bin/start_gl` para `/bin/start` no cartão SD, pois é esse o caminho executado pelo `init`:

```sh
sudo cp bin/start_gl /media/usuario/main/bin/start
```

### Observações de execução

- O kernel inicia o `init` com `HOME=/`, e a Mesa grava o cache de shaders em `/.cache`, no cartão SD. Para desativar o cache, defina `MESA_SHADER_CACHE_DISABLE=true` no ambiente do programa.
- Para diagnosticar falhas na inicialização do EGL ou no carregamento dos drivers, defina `EGL_LOG_LEVEL=debug`.
- O sistema não tem udev. A Mesa e a `libdrm` encontram os dispositivos por `/dev` e `/sys`, que o `init` já monta.
