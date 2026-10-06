# Banana Pi M2 Zero com gráficos mínimos

O objetivo deste repositório é servir como uma segunda etapa do projeto [Banana Pi M2 Zero do Kernel](https://github.com/MatheusAlvesA/banana-pi-m2-zero-kernel). Conclua aquele projeto antes de começar este para entender como a base do sistema foi construída.

Neste projeto, vamos substituir o programa `start.c` da etapa anterior por uma nova versão, capaz de mostrar gráficos simples na tela e vinculada dinamicamente às bibliotecas necessárias.

O repositório traz três versões desse programa:

- `bin/start.c`: desenha diretamente com DRM/KMS, usando apenas `libc` e `libdrm`.
- `bin/start_gl.c`: desenha com a GPU usando OpenGL ES 2.0 por meio da [Mesa3D](https://mesa3d.org/). Veja a seção [Versão com OpenGL ES (Mesa3D)](#versão-com-opengl-es-mesa3d).
- `bin/start_sdl.c`: desenha com a GPU por meio da [SDL2](https://www.libsdl.org/), que cuida do DRM/KMS, da GBM e do EGL. Veja a seção [Versão com SDL2](#versão-com-sdl2).

Apenas uma delas é usada por vez, sempre instalada no cartão SD como `/bin/start`.

Sobre a mesma base da versão com SDL2, também é possível rodar jogos feitos com a [Godot Engine](https://godotengine.org/) na Banana Pi M2 Zero. O guia [Godot na Banana Pi M2 Zero](godot.md) mostra como compilar o modelo de exportação para a placa, exportar o jogo e obter 60 FPS, inclusive em 1080p.

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

## Device tree

O U-Boot carrega o device tree da placa, `boot/dtb/sun8i-h2-plus-bananapi-m2-zero.dtb`, e aplica sobre ele o overlay `boot/dtb/overlay/bananapi-m2-zero-graphics.dtbo` antes de iniciar o kernel. O overlay reúne todos os ajustes que este projeto faz no device tree:

- `capacity-dmips-mhz` igual nos quatro núcleos Cortex-A7.
- Alimentação da GPU (`mali-supply`) e tensão de 1,2 V em todos os pontos de operação da GPU, para que o `lima` controle a frequência.
- I2S2 habilitada e placa de som `simple-audio-card` para o áudio pelo HDMI (veja [Áudio pelo HDMI](#áudio-pelo-hdmi)).

Copie o `.dtbo` para `dtb/overlay/` na partição de boot do cartão SD, ao lado da pasta `dtb/` que já contém o `.dtb`.

O overlay referencia os nós do device tree pelos labels do código-fonte do kernel (`&mali`, `&hdmi`, `&i2s2` etc.). Para isso, o `.dtb` precisa ter sido compilado com símbolos, ou seja, conter o nó `__symbols__`. O `.dtb` deste repositório já foi compilado assim. Se gerar o seu próprio no projeto anterior, passe `DTC_FLAGS=-@` ao `make`, na pasta do código-fonte do kernel:

```sh
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- DTC_FLAGS=-@ \
    allwinner/sun8i-h2-plus-bananapi-m2-zero.dtb
```

O arquivo gerado fica em `arch/arm/boot/dts/allwinner/`. Sem os símbolos, o U-Boot não consegue aplicar o overlay, mostra `ERRO: falha ao aplicar o overlay` e interrompe o boot.

Depois de alterar o overlay, compile-o novamente com o `dtc` (pacote `device-tree-compiler`):

```sh
dtc -@ -I dts -O dtb \
    -o boot/dtb/overlay/bananapi-m2-zero-graphics.dtbo \
    boot/dtb/overlay/bananapi-m2-zero-graphics.dts
```

Para conferir o resultado sem a placa, aplique o overlay no computador com o `fdtoverlay`, do mesmo pacote. Se o comando terminar sem erro, o U-Boot também conseguirá aplicá-lo:

```sh
fdtoverlay -i boot/dtb/sun8i-h2-plus-bananapi-m2-zero.dtb \
    -o /tmp/resultado.dtb boot/dtb/overlay/bananapi-m2-zero-graphics.dtbo
```

Se alterar o `boot/boot.cmd`, gere novamente o `boot/boot.scr`, que é a versão lida pelo U-Boot, e copie-o para o cartão SD:

```sh
mkimage -C none -A arm -T script -d boot/boot.cmd boot/boot.scr
```

O `mkimage` faz parte do pacote `u-boot-tools`.

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

Os buffers exibidos na tela precisam de memória fisicamente contígua, reservada pelo parâmetro `cma=128M` que o `boot/boot.cmd` passa ao kernel. A correção de `mali-supply` no overlay do device tree (veja [Device tree](#device-tree)) permite que o `lima` controle a frequência da GPU.

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
- A Mesa e a `libdrm` não dependem do udev: elas encontram os dispositivos por `/dev` e `/sys`, que o `init` já monta.

## Versão com SDL2

O arquivo `bin/start_sdl.c` faz o mesmo que o `bin/start_gl.c`: pinta a tela de vermelho com a GPU por 10 segundos. Além disso, se a barra de espaço for pressionada num teclado conectado, a tela fica verde, o que serve para testar a entrada pelo udev. Se houver uma placa de som, o programa também toca um tom de 440 Hz (a nota lá) durante os 10 segundos, o que serve para testar o [áudio pelo HDMI](#áudio-pelo-hdmi). A diferença é que a [SDL2](https://www.libsdl.org/) assume as etapas que o `start_gl.c` faz manualmente: procurar o dispositivo DRM, assumir o controle como DRM master, criar a GBM e o EGL, e exibir cada quadro na tela.

A SDL2 é a camada usada por muitos jogos e engines para acessar vídeo, áudio e entrada. Fazê-la funcionar sobre a Mesa é o primeiro passo para rodar programas maiores neste sistema.

### Configuração do Buildroot

Parta da configuração da seção [Configuração do Buildroot](#configuração-do-buildroot) da Mesa, pois a SDL2 usa a GBM, o EGL e o OpenGL ES gerados por ela. Execute `make menuconfig` na pasta do Buildroot e habilite:

**System configuration**

- Em *`/dev` management*, escolha `Dynamic using devtmpfs + eudev` (`BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV`). Essa opção seleciona o pacote `eudev`, que fornece o daemon `udevd`, a ferramenta `udevadm` e a biblioteca `libudev`. Mantenha os padrões do pacote.

**Target packages → Libraries → Audio/Sound**

- Habilite `alsa-lib` (`BR2_PACKAGE_ALSA_LIB`), a biblioteca do ALSA, a interface de áudio do kernel Linux.

**Target packages → Graphic libraries and applications (graphic/text)**

- Em *Graphic libraries*, habilite `sdl2` (`BR2_PACKAGE_SDL2`) e, dentro dele:
  - `OpenGL ES` (`BR2_PACKAGE_SDL2_OPENGLES`)
  - `KMS/DRM video driver` (`BR2_PACKAGE_SDL2_KMSDRM`)

A opção `KMS/DRM video driver` só aparece depois de habilitar `OpenGL ES`. Não habilite os drivers X11 ou Wayland: sem eles, o KMS/DRM é o único driver de vídeo da SDL2.

Com a `libudev`, a SDL2 encontra teclados, mouses e controles em `/dev/input`. Com a `alsa-lib`, ela ganha o driver de áudio `alsa`. A SDL2 só usa essas bibliotecas se elas existirem quando ela for compilada, por isso habilite as três opções antes de executar o `make`.

Salve a configuração e compile:

```sh
make
```

Como a toolchain e a Mesa já estão prontas, apenas a SDL2, o udev e o ALSA são compilados. A biblioteca gerada é `output/target/usr/lib/libSDL2-2.0.so.0`, e os cabeçalhos ficam em `output/staging/usr/include/SDL2/`.

### Cópia para o cartão SD

A `libSDL2` depende diretamente apenas da `libc` e da `libm`. A `libdrm`, a `libgbm`, a `libEGL`, a `libGLESv2`, a `libudev` e a `libasound` são carregadas em tempo de execução por `dlopen`. Por isso, copie novamente as pastas de bibliotecas inteiras, como na seção da Mesa. A pasta `lib/` inclui a `libudev`, a `libblkid` e, em `lib/udev/`, as regras e o banco de dados de hardware (`hwdb.bin`) do udev:

```sh
sudo cp -a output/target/lib/.     /media/usuario/main/lib/
sudo cp -a output/target/usr/lib/. /media/usuario/main/usr/lib/
```

Copie também os programas do udev, sua configuração e os arquivos de configuração do ALSA, que a `alsa-lib` lê de `/usr/share/alsa`:

```sh
sudo mkdir -p /media/usuario/main/sbin /media/usuario/main/usr/bin \
    /media/usuario/main/etc/udev /media/usuario/main/usr/share
sudo cp -a output/target/sbin/udevd        /media/usuario/main/sbin/
sudo cp -a output/target/usr/bin/udevadm   /media/usuario/main/usr/bin/
sudo cp -a output/target/etc/udev/udev.conf /media/usuario/main/etc/udev/
sudo cp -a output/target/usr/share/alsa    /media/usuario/main/usr/share/
```

Não copie a pasta `output/target/sbin/` inteira: o `sbin/init` do Buildroot é um link para o BusyBox e substituiria o `init` deste projeto. O `/sbin/udevadm` do Buildroot também é dispensável, pois é só um link para `/usr/bin/udevadm`.

### Inicialização do udev

O `udevd` precisa estar rodando antes de o programa abrir a SDL2. O `sbin/init.c` deste projeto faz isso na função `start_udev`, antes de executar `/bin/start`, nas mesmas etapas do script `S10udevd` que o Buildroot instalaria para o init do BusyBox:

1. Monta um `tmpfs` em `/run`, onde o udev guarda seu banco de dados (`/run/udev`). Assim, o banco é recriado a cada boot.
2. Executa `/sbin/udevd --daemon`, que só retorna depois de começar a escutar os eventos do kernel.
3. Executa `udevadm trigger` para subsistemas e para dispositivos. Os dispositivos detectados durante o boot geraram eventos antes de o `udevd` existir, e o `trigger` pede ao kernel para reenviá-los.
4. Executa `udevadm settle`, que espera o `udevd` processar todos os eventos, com limite de 30 segundos.

Se `/sbin/udevd` não existir no cartão, o `init` segue sem udev.

O `init` é estático e não depende das bibliotecas do cartão. Para compilá-lo, use o compilador cruzado do projeto anterior, na raiz deste repositório:

```sh
arm-linux-gnueabihf-gcc \
    -Wall -Wextra -O2 -mcpu=cortex-a7 -static -s \
    sbin/init.c \
    -o sbin/init
```

Copie `sbin/init` para `/sbin/init` no cartão SD:

```sh
sudo cp sbin/init /media/usuario/main/sbin/init
```

### Compilação de `start_sdl.c`

Assim como o `start_gl.c`, o `start_sdl.c` precisa da toolchain do Buildroot. Execute na raiz deste repositório:

```sh
BUILDROOT_DIR="$PWD/buildroot"
BUILDROOT_CC="$BUILDROOT_DIR/output/host/bin/arm-buildroot-linux-gnueabihf-gcc"
BUILDROOT_SYSROOT="$BUILDROOT_DIR/output/staging"

"$BUILDROOT_CC" \
    --sysroot="$BUILDROOT_SYSROOT" \
    -Wall -Wextra -O2 -mcpu=cortex-a7 \
    bin/start_sdl.c \
    -lSDL2 \
    -o bin/start_sdl
```

Copie `bin/start_sdl` para `/bin/start` no cartão SD:

```sh
sudo cp bin/start_sdl /media/usuario/main/bin/start
```

O programa imprime o driver de vídeo, que deve ser `KMSDRM`, a resolução da tela e o renderer, que deve ser `opengles2`. O renderer `software` indica que a SDL2 não conseguiu usar a GPU.

Em seguida, o programa imprime o driver de áudio, que deve ser `alsa`, e o formato aberto. O áudio é opcional: se a SDL2 não encontrar uma placa de som, o programa imprime o erro e continua só com o vídeo.

### Observações de execução

- A SDL2 procura em `/dev/dri` o primeiro `card*` com um conector conectado e ignora o node do `lima`, que não tem conectores. Para forçar um node, defina `SDL_KMSDRM_DEVICE_INDEX` com o número do `card`.
- O programa usa um laço de quadros com vsync, como um jogo: trata os eventos, desenha e apresenta o quadro. A SDL2 converte `SIGINT` e `SIGTERM` no evento `SDL_QUIT`, que encerra o laço.
- Ao encerrar, `SDL_Quit` libera o DRM master e restaura o modo anterior da tela, que volta a mostrar o console do kernel.
- Sem a `libudev`, a SDL2 não procura teclados e mouses em `/dev/input`. Se a entrada não funcionar, confirme que o `init` imprimiu `udev iniciado` no boot. O kernel em `boot/zImage` já inclui o `evdev` e os drivers `usbhid` e `hid-generic`, usados por teclados e mouses USB.
- A `alsa-lib` dá à SDL2 o driver de áudio, mas a placa só tem som depois das alterações da seção [Áudio pelo HDMI](#áudio-pelo-hdmi). Sem uma placa de som em `/dev/snd`, a SDL2 não consegue abrir nenhum dispositivo de áudio.

## Áudio pelo HDMI

O H3 envia o áudio ao HDMI por uma interface I2S interna, a I2S2. O controlador HDMI da Synopsys (`dw-hdmi`) recebe essas amostras e as transmite junto com o vídeo. No Linux, três drivers participam:

| Driver | Função |
|---|---|
| `sun4i-i2s` | Interface I2S2, que lê as amostras da memória por DMA |
| `dw-hdmi-i2s-audio` e `hdmi-audio-codec` | Lado do HDMI, que recebe o I2S e configura o envio do áudio ao monitor |
| `simple-audio-card` | Placa de som que liga as duas pontas e aparece em `/dev/snd` |

### Device tree

No device tree original, a I2S2 está desabilitada e não existe nenhuma placa de som. O overlay `boot/dtb/overlay/bananapi-m2-zero-graphics.dts` (veja [Device tree](#device-tree)) faz os ajustes necessários:

1. Habilita a I2S2 (`&i2s2`).
2. Garante `#sound-dai-cells` no nó do HDMI (`&hdmi`), para que ele possa ser referenciado como interface de áudio.
3. Cria o nó `/hdmi-sound`, uma `simple-audio-card` que liga a I2S2 ao HDMI. O codec do HDMI exige o sinal de quadro (LRCK) invertido e slots fixos de 32 bits, configurados por `simple-audio-card,frame-inversion` e pelas propriedades de TDM. A propriedade `playback-only` limita a placa à reprodução: a I2S2 só tem DMA de envio, e sem essa propriedade a placa também tentaria criar o fluxo de captura, falhando com `Missing dma channel for stream: 1`.

Os nós do HDMI e da I2S2 são referenciados pelos labels `&hdmi` e `&i2s2`, e o U-Boot cria os `phandle` necessários ao aplicar o overlay. A configuração segue a [proposta de áudio HDMI para H3/H5](https://www.mail-archive.com/linux-kernel@vger.kernel.org/msg2322809.html) enviada ao kernel, que não chegou a ser incorporada.

### Kernel

O kernel em `boot/zImage` já inclui todos os drivers do áudio HDMI. Se usar esse kernel, recompilar é opcional: copie-o para o cartão SD e siga para [Conferindo o resultado](#conferindo-o-resultado-1).

Se preferir compilar o seu próprio kernel no projeto anterior, saiba que o `sunxi_defconfig` já inclui o `sun4i-i2s`, o `dw-hdmi-i2s-audio` e o `hdmi-audio-codec`, mas não inclui a `simple-audio-card`. Sem ela, a I2S2 e o HDMI são detectados, mas nenhuma placa de som é criada.

Na pasta do código-fonte do kernel, depois de gerar a configuração com o `sunxi_defconfig` e antes de compilar, execute:

```sh
./scripts/config --enable DRM_FBDEV_EMULATION \
                 --enable FB_DEVICE \
                 --enable FRAMEBUFFER_CONSOLE \
                 --enable DRM_CLIENT_DEFAULT_FBDEV \
                 --enable SND_SIMPLE_CARD \
                 --enable SND_SUN4I_I2S \
                 --enable DRM_DW_HDMI_I2S_AUDIO
```

As quatro primeiras opções são as do console do projeto anterior. As três últimas são as do áudio HDMI. O `--enable` grava cada opção como `=y`, ou seja, compilada dentro do kernel, e não como módulo, pois o sistema não carrega módulos. Diferente do `make menuconfig`, o comando produz sempre a mesma configuração e pode ser repetido sem navegar pelos menus.

O `scripts/config` não confere dependências. Se faltar alguma, o `make` remove a opção do `.config` sem avisar. Depois de iniciar a compilação, confirme que as linhas abaixo terminam em `=y`:

```sh
grep -E 'CONFIG_(SND_SIMPLE_CARD|SND_SIMPLE_CARD_UTILS|SND_SUN4I_I2S|DRM_DW_HDMI_I2S_AUDIO|SND_SOC_HDMI_CODEC|DRM_DW_HDMI)=' .config
```

A `SND_SIMPLE_CARD_UTILS` e a `SND_SOC_HDMI_CODEC` não aparecem no comando, pois são habilitadas automaticamente pela `simple-audio-card` e pelo `dw-hdmi-i2s-audio`. Ao final, copie o novo `zImage` para `boot/zImage` e para o cartão SD.

### Conferindo o resultado

No boot, a placa de som deve aparecer no log do kernel e em `/proc/asound/cards`, com o nome `sun8i-h3-hdmi`, e os dispositivos devem existir em `/dev/snd` (`controlC0` e `pcmC0D0p`). Ao executar o `start_sdl`, o programa deve imprimir `Driver de audio: alsa` e tocar um tom de 440 Hz no monitor ou na TV enquanto a tela estiver vermelha ou verde.

O áudio só é enviado enquanto o HDMI está ativo, ou seja, enquanto um programa mantém um modo de vídeo na tela, e o monitor ou TV precisa aceitar áudio pelo HDMI. Monitores sem alto-falantes aceitam o sinal, mas ficam em silêncio.
