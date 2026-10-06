# Godot na Banana Pi M2 Zero

Este guia mostra como rodar na Banana Pi M2 Zero um jogo feito com a [Godot Engine](https://godotengine.org/), sobre o mesmo sistema mínimo do [readme principal](readme.md). Ele parte do ponto em que a [versão com SDL2](readme.md#versão-com-sdl2) já funciona na placa, com vídeo pelo KMS/DRM, teclado pelo udev e som pelo [áudio HDMI](readme.md#áudio-pelo-hdmi).

Ao final, o jogo é executado pelo `init` como `/bin/start`, ocupa a tela inteira e recebe o teclado e o som pela SDL2.

## Por que Godot 3 e FRT

A GPU Mali-400 da placa, com o driver `lima` da Mesa, só oferece OpenGL ES 2.0. O Godot 4 exige no mínimo OpenGL ES 3.0, mesmo no renderizador Compatibility, e não roda nessa GPU. O Godot 3 ainda tem o renderizador GLES2, por isso este guia usa o **Godot 3.6**.

Os modelos de exportação oficiais do Godot 3 para Linux criam a janela pelo X11, que não existe neste sistema. O [FRT](https://github.com/efornara/frt) é uma plataforma alternativa do Godot 3 que cria a janela, lê a entrada e toca o áudio pela SDL2. Como a SDL2 já funciona sobre o KMS/DRM, o udev e o ALSA, o FRT aproveita toda essa base.

Os binários prontos do FRT para `arm32` não servem para esta placa. Eles são compilados para ARMv8 (`Tag_CPU_arch: v8`, com instruções como `ldaex` e `stlex`), e o Cortex-A7 do H2+ é ARMv7. Ao executar um deles, o kernel encerra o programa com o sinal 4 (`SIGILL`, instrução ilegal), sem nenhuma mensagem do Godot. O `init` mostra apenas `/bin/start finalizou pelo sinal 4`. O mesmo problema é descrito na [issue 112189](https://github.com/godotengine/godot/issues/112189) do Godot, com a Orange Pi PC, que usa o H3. Por isso o FRT precisa ser compilado com a toolchain do Buildroot, como mostrado a seguir.

## Editor e modelo de exportação

O Godot usa dois programas diferentes, e só um deles precisa ser compilado:

| | Editor | Modelo de exportação (FRT) |
|---|---|---|
| Onde roda | No PC (x86_64) | Na placa (ARMv7) |
| Função | Criar o projeto e exportar o jogo | Carregar o jogo e executá-lo |
| Origem | Baixado pronto do site do Godot | Compilado com a toolchain do Buildroot |

O jogo exportado é um arquivo `.pck`, com as cenas, os scripts GDScript, as imagens, os sons e as configurações do projeto. Ele não tem código de máquina e serve para qualquer arquitetura. Na placa, o modelo de exportação do FRT é o executável, e o `.pck` é o jogo que ele carrega.

```
Jogo (start.pck)
        │
Godot 3.6.3 (motor, GLES2)    ┐
        │                     ├── /bin/start (modelo de exportação)
FRT (platform/frt)            ┘
        │
SDL2  →  Mesa (EGL/GLES2, lima)  +  udev  +  ALSA
        │
Kernel (DRM/KMS, evdev, som)
```

## Atalho: modelo já compilado

O arquivo `bin/start_gd` deste repositório é o modelo de exportação do FRT já compilado conforme a seção [Compilar o FRT](#2-compilar-o-frt), com a toolchain do Buildroot (ARMv7) e com a [janela sem buffer de profundidade](#janela-sem-buffer-de-profundidade). Com ele, não é preciso compilar o FRT: basta baixar o editor, configurar o projeto e exportar só o `.pck`.

Para usá-lo, copie-o para o cartão SD como `/bin/start` e coloque o jogo ao lado, como `/bin/start.pck`:

```sh
sudo cp bin/start_gd /media/usuario/main/bin/start
sudo cp start.pck    /media/usuario/main/bin/start.pck
```

O `.pck` é gerado no editor por **Project → Export… → Export PCK/Zip**, a partir de um preset **Linux/X11**, como explicado em [Exportar o jogo](#4-exportar-o-jogo). Se o preset reclamar da falta de modelos de exportação, preencha os campos **Custom Template** com o caminho de `bin/start_gd`.

O atalho vale nestas condições:

- **As bibliotecas do cartão SD são as do Buildroot da [versão com SDL2](readme.md#versão-com-sdl2).** O binário é ligado dinamicamente à `libSDL2`, à `libz`, à `libm` e à `libc` dessa compilação, e a SDL2 usa a Mesa, o udev e o ALSA do cartão.
- **O editor é o Godot 3.6.x, até o 3.6.3**, sem a versão Mono, e os scripts são em GDScript. O binário não tem suporte a C# nem a GDNative.
- **É o modelo de depuração (`release_debug`).** Ele mostra os erros e os `print()` no console e tem desempenho muito próximo do modelo de `release`. Para a versão final do jogo, compile o FRT com `target=release`.
- **O jogo não usa 3D com `render_direct_to_screen`**, pois a janela não tem buffer de profundidade. Jogos 2D e jogos 3D no modo padrão não são afetados.

Se alguma dessas condições não for atendida, compile o FRT conforme a seção 2.

## 1. Baixar o editor

Baixe o **Godot 3.6.3**, na versão *Standard* para Linux x86_64, no [arquivo de versões do Godot](https://godotengine.org/download/archive/). Não use a versão *Mono* (C#): o FRT é compilado sem suporte a C#, então os scripts precisam ser em GDScript.

A versão do editor não pode ser mais nova que a do modelo de exportação. O FRT usado aqui é a versão `3.6.3-1`, baseada no Godot 3.6.3, então qualquer editor 3.6.x até o 3.6.3 serve.

## 2. Compilar o FRT

### Dependências

Além do Buildroot da [versão com SDL2](readme.md#versão-com-sdl2), compilado com suporte a C++ (`BR2_TOOLCHAIN_BUILDROOT_CXX`), a compilação usa o `git`, o `python3` e o `scons`:

```sh
sudo apt install git python3 scons
```

O FRT usa estes arquivos do Buildroot, que já existem se a SDL2 foi compilada:

- `output/host/bin/arm-buildroot-linux-gnueabihf-g++`, o compilador C++
- `output/host/bin/arm-buildroot-linux-gnueabihf-gcc-ar` e `-gcc-ranlib`, usados pelo LTO
- `output/staging/usr/bin/sdl2-config`, que informa onde estão os cabeçalhos e a biblioteca da SDL2
- `output/staging/usr/lib/libz.so`

### Código-fonte

O FRT é uma plataforma do Godot, como `x11` ou `android`, e fica dentro da pasta `platform/` do código do Godot. O repositório `efornara/godot3`, na branch `frt`, é o Godot 3.6.3 com os pequenos ajustes de que o FRT precisa. Em uma pasta fora deste repositório, por exemplo `~/dev/godot-build`, execute:

```sh
mkdir -p ~/dev/godot-build
cd ~/dev/godot-build
git clone --depth 1 -b frt https://github.com/efornara/godot3
git clone --depth 1 https://github.com/efornara/frt godot3/platform/frt
```

### Janela sem buffer de profundidade

Antes de compilar, altere o FRT para criar a janela sem buffer de profundidade e sem stencil. Essa mudança faz o Godot ganhar cerca de 8 ms por quadro em 1080p, como explicado em [Buffer de profundidade da janela](#buffer-de-profundidade-da-janela). Fazê-la antes da primeira compilação evita compilar tudo de novo depois.

No arquivo `godot3/platform/frt/sdl2_adapter.h`, dentro da função `init_window`, adicione as duas últimas linhas abaixo logo depois de `SDL_GL_CONTEXT_PROFILE_MASK` e antes do `SDL_CreateWindow`:

```cpp
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		// Godot only clears the color of the window framebuffer. On lima, a
		// depth/stencil buffer that is never cleared is reloaded from memory
		// on every frame, so do not request one (SDL defaults to 16-bit depth).
		// Godot renders 2D and 3D into its own render targets, which have
		// their own depth buffers; only 3D with render_direct_to_screen needs
		// depth on the window.
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
```

### Compilação

Na pasta `godot3`, compile primeiro o modelo de depuração (`release_debug`), que mostra os erros e os `print()` do jogo no console. Ajuste `BR` se o Buildroot estiver em outro lugar:

```sh
cd ~/dev/godot-build/godot3
BR="$HOME/dev/banana-pi-zero-graphics/buildroot/output"
PATH="$BR/host/bin:$BR/staging/usr/bin:$PATH" \
scons platform=frt tools=no target=release_debug arch=arm32 \
      triple=arm-buildroot-linux-gnueabihf \
      CCFLAGS="-mcpu=cortex-a7" \
      warnings=no production=yes LINKFLAGS=-s -j$(nproc)
```

As opções são:

- `platform=frt`: compila a plataforma FRT, em vez da `x11`.
- `tools=no`: compila o modelo de exportação, sem o editor.
- `target=release_debug`: modelo de depuração. Para a versão final do jogo, use `target=release`.
- `arch=arm32`: só muda o nome do arquivo gerado. Quem decide a arquitetura é o compilador.
- `triple=arm-buildroot-linux-gnueabihf`: faz o FRT usar o `arm-buildroot-linux-gnueabihf-g++` e as ferramentas do Buildroot. A toolchain do Buildroot foi configurada para o Cortex-A7 (`BR2_cortex_a7`) e gera código ARMv7. O `CCFLAGS="-mcpu=cortex-a7"` só reforça isso.
- `PATH` com `staging/usr/bin`: o FRT executa `sdl2-config --cflags --libs` para encontrar a SDL2. Com esse caminho no início do `PATH`, ele usa o `sdl2-config` do Buildroot, e o programa fica ligado à mesma SDL2, glibc e libstdc++ do cartão SD.
- `production=yes`: liga as otimizações de versão final, incluindo o LTO. O LTO otimiza o programa inteiro na etapa final de ligação, que por isso é demorada.
- `warnings=no`: desliga os avisos do compilador, que são muitos com o GCC 15.

A primeira compilação leva de 10 a 30 minutos, conforme o PC. Durante a compilação, a mensagem `Error: X11 libraries not found. Aborting.` pode aparecer. Ela é inofensiva: é só o Godot verificando se poderia compilar a plataforma `x11`, que não é usada.

O resultado fica em `bin/`. Com `production=yes`, o nome termina em `.lto`:

| Opção | Arquivo gerado |
|---|---|
| `target=release_debug` | `bin/godot.frt.opt.debug.arm32.lto` |
| `target=release` | `bin/godot.frt.opt.arm32.lto` |

Se não encontrar o arquivo, `ls -t bin/` mostra o mais recente primeiro. Confirme que o binário é ARMv7:

```sh
readelf -A bin/godot.frt.opt.debug.arm32.lto | grep Tag_CPU_arch
```

A saída deve mostrar `Tag_CPU_arch: v7`. Se mostrar `v8`, o binário não roda na placa.

O binário depende apenas de bibliotecas que já estão no cartão SD (`libSDL2-2.0.so.0`, `libz.so.1`, `libm.so.6` e `libc.so.6`), o que pode ser conferido com:

```sh
readelf -d bin/godot.frt.opt.debug.arm32.lto | grep NEEDED
```

## 3. Configurar o projeto

No editor, abra o projeto e ajuste em **Project → Project Settings**:

- **Rendering → Quality → Driver → Driver Name**: `GLES2`.
- **Display → Window**: a resolução, conforme a seção [Resolução](#resolução). Para começar, use 960×540 ampliado, que roda a 60 FPS com folga.

Nas imagens, selecione cada arquivo no painel *FileSystem* e, na aba **Import**, confira se **Compress → Mode** está em `Lossless`. Esse é o padrão para imagens 2D. A compressão de vídeo padrão do desktop (S3TC) não é suportada pela Mali-400.

O Godot grava os dados do jogo, como saves e configurações (`user://`), em `$HOME/.local/share/godot/app_userdata/<nome do projeto>/`. Como o kernel inicia o `init` com `HOME=/`, esses arquivos ficam em `/.local/share/` no cartão SD e continuam lá quando o jogo é atualizado.

## 4. Exportar o jogo

### Preset de exportação

Em **Project → Export…**, clique em **Add…** e escolha **Linux/X11**. O nome do preset fala em X11, mas é só o formato de exportação: o modelo do FRT substitui o executável do X11. Na aba **Options**:

- **Custom Template → Debug**: o caminho de `godot.frt.opt.debug.arm32.lto`.
- **Custom Template → Release**: o caminho de `godot.frt.opt.arm32.lto`, depois de compilá-lo com `target=release`.
- **Binary Format → Architecture**: `arm32`.
- **Binary Format → Embed Pck**: desligado, para que o jogo fique num `.pck` separado.

Com os modelos personalizados preenchidos, o editor não precisa dos modelos de exportação oficiais, e o aviso de modelos ausentes desaparece.

### Exportação completa

Clique em **Export Project** e dê ao arquivo o nome `start`. Para os primeiros testes, deixe **Export With Debug** marcado, para usar o modelo de depuração. O editor gera dois arquivos:

- `start.arm32`: o modelo do FRT. O Godot 3.6 acrescenta a arquitetura ao nome.
- `start.pck`: o jogo.

Ao executar, o Godot tira a extensão do nome do executável e procura `<nome>.pck` na mesma pasta. Copie os dois arquivos para o cartão, renomeando o executável para `start`, que é o caminho executado pelo `init`. Com a partição raiz do cartão montada em `/media/usuario/main`:

```sh
sudo cp start.arm32 /media/usuario/main/bin/start
sudo cp start.pck   /media/usuario/main/bin/start.pck
sudo chmod +x /media/usuario/main/bin/start
```

Assim o `init` executa `/bin/start` sem argumentos, e o Godot carrega `/bin/start.pck` sozinho.

### Atualizando só o jogo

Enquanto o modelo do FRT não mudar, não é preciso copiar o executável de novo. Use **Export PCK/Zip**, dê ao arquivo o nome `start.pck` e copie apenas esse arquivo para `/bin/start.pck`. Na versão final, desmarque **Export With Debug** e troque `/bin/start` pelo modelo de `release`.

O executável só precisa ser trocado quando:

- o FRT for recompilado, por exemplo para trocar o modelo de depuração pelo de `release`;
- o editor for atualizado para uma versão mais nova que a do FRT;
- o jogo usar código nativo (GDNative), que precisaria ser compilado para ARMv7 com a mesma toolchain.

## 5. Desempenho

### Limites da GPU em 1080p

As medidas abaixo foram feitas na placa com um programa de teste em SDL2, com a GPU a 576 MHz. Ele desenhava várias camadas de tela cheia, cada uma uma textura de 256×256 ampliada, numa textura do tamanho da tela que nunca era exibida, e media com `glFinish` o tempo que a GPU levava para terminar cada quadro:

| Medida | Valor |
|---|---|
| Rasterização | ~4 ms por camada de tela cheia em 1080p, cerca de 520 milhões de pixels por segundo |
| Gravação do quadro na RAM | ~10 ms por quadro 1080p, mesmo sem nada desenhado |

A Mali-400 desenha a tela em blocos de 16×16 pixels, guardados numa memória interna da GPU. Cada bloco só vai para a RAM depois de todas as camadas serem desenhadas, e enquanto um bloco é gravado o próximo já está sendo desenhado. Por isso o tempo de um quadro é o maior entre os dois custos:

```
quadro 1080p ≈ maior entre (10 ms , 4 ms × camadas de tela cheia)
```

Algumas consequências:

- Em 1080p, cabem cerca de 4 camadas de tela cheia nos 16,7 ms de um quadro a 60 FPS. Em 720p, que tem 2,25 vezes menos pixels, cabem cerca de 9.
- Transparência praticamente não custa nada a mais, pois a mistura de cores acontece na memória interna da GPU.
- O que custa caro é ler e gravar a tela inteira na RAM: texturas intermediárias, cópias de tela e buffers recarregados a cada quadro.

### Buffer de profundidade da janela

Por padrão, a SDL2 cria a janela com um buffer de profundidade de 16 bits (`SDL_video.c`, `depth_size = 16`). O Godot nunca limpa esse buffer na janela, pois limpa a tela só com `glClear(GL_COLOR_BUFFER_BIT)`. A SDL2 também não o limpa no seu renderer 2D.

No driver `lima` da Mesa, todo buffer nasce marcado para ser recarregado da RAM, e a marcação só sai quando o buffer é limpo. Um buffer de profundidade que nunca é limpo é recarregado a cada quadro (`lima_job.c`, função `lima_fb_zsbuf_needs_reload`). Em 1080p, isso é uma leitura extra da tela inteira, que custa cerca de 8 ms por quadro.

A alteração em `sdl2_adapter.h`, feita na seção [Janela sem buffer de profundidade](#janela-sem-buffer-de-profundidade), remove esse custo. Medido com o mesmo programa de teste, desenhando na janela, em 1080p:

| Desenho na tela | Com buffer de profundidade | Sem buffer de profundidade |
|---|---|---|
| Só limpar a tela | 17,6 ms (54 FPS) | 10,1 ms (60 FPS) |
| 4 camadas de tela cheia | 24,0 ms (41 FPS) | 15,7 ms (60 FPS) |

A alteração não afeta jogos 2D, nem jogos 3D no modo padrão, pois nesses casos o Godot desenha numa textura própria, com seu próprio buffer de profundidade. A exceção é o 3D com `render_direct_to_screen`, que desenha direto na janela e precisa da profundidade dela.

O mesmo vale para programas próprios com a SDL2: se o programa não usa profundidade, chame `SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0)` e `SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0)` antes de `SDL_CreateWindow`.

### `render_direct_to_screen`

No GLES2, o Godot 3 desenha cada quadro numa textura intermediária e depois copia essa textura para a tela (`rasterizer_storage_gles2.cpp`). Em 1080p, isso significa gravar a tela inteira na RAM, ler tudo de volta e gravar de novo. A propriedade `render_direct_to_screen` do `Viewport`, que só existe no GLES2, faz o Godot desenhar direto na tela.

Para usá-la, desenhe na resolução nativa da tela, sem ampliação. Em **Display → Window**:

- **Size → Width / Height**: a resolução do monitor, por exemplo `1920` / `1080`.
- **Size → Test Width / Test Height**: `0`.
- **Stretch → Mode**: `disabled`.

E, no `_ready()` de um script da cena principal:

```gdscript
func _ready():
	get_viewport().render_direct_to_screen = true
```

Com o `stretch` em modo `viewport`, a textura intermediária é necessária para ampliar a imagem, então essa propriedade só faz sentido na resolução nativa.

Resultados de um projeto 2D simples (um sprite e uma label) em 1080p nativo:

| Configuração | FPS |
|---|---|
| Godot padrão | 19 |
| Com `render_direct_to_screen` | 27 a 30 |
| Com `render_direct_to_screen` e FRT sem buffer de profundidade | 60 |

### Resolução

Quando o jogo é mais pesado que algumas camadas de tela cheia, desenhe numa resolução menor e amplie. Em **Display → Window**:

| Opção | Valor | Função |
|---|---|---|
| Size → Width / Height | `960` / `540` | Resolução em que o jogo é desenhado |
| Size → Test Width / Test Height | `1280` / `720` | Tamanho da janela |
| Stretch → Mode | `viewport` | Desenha em 960×540 e amplia para a janela |
| Stretch → Aspect | `keep` | Mantém a proporção |

O *Test Width / Test Height* não é só para testes: o Godot 3 também usa esse valor no jogo exportado (`main/main.cpp`). Ele é necessário porque, sem gerenciador de janelas, o driver KMS/DRM da SDL2 **muda o modo de vídeo do HDMI** para o modo do monitor mais próximo do tamanho da janela (`KMSDRM_GetModeToSet`, em `SDL_kmsdrmvideo.c`). A SDL2 escolhe o menor modo com largura e altura maiores ou iguais às da janela. Monitores raramente têm um modo 960×540, e o menor que cabe costuma ser o 1024×768, que é 4:3. A imagem 16:9 fica então com faixas pretas em cima e embaixo. Com a janela em 1280×720, a SDL2 escolhe o modo 1280×720, que é 16:9, e o monitor amplia para a tela inteira.

Com Test Width / Test Height em `1920` / `1080`, o HDMI fica no modo nativo e só o Godot amplia a imagem. Fica um pouco mais nítido, mas a cópia final passa a ser em 1080p.

Muitos monitores ficam um ou dois segundos com a tela preta a cada troca de modo, quando o jogo abre e quando fecha. Para evitar as duas trocas, o kernel pode iniciar já no modo do jogo, acrescentando `video=HDMI-A-1:1280x720@60` aos `bootargs` no `boot/boot.cmd`.

### Vsync e o limite de 60 FPS

No driver KMS/DRM da SDL2, cada troca de quadro espera a anterior ser exibida no vblank (`SDL_kmsdrmopengles.c`). Desligar o vsync só pede uma troca assíncrona (`DRM_MODE_PAGE_FLIP_ASYNC`), que o `sun4i-drm` não oferece. Por isso o FPS nunca passa da taxa do monitor, com ou sem vsync. Um contador que mostra 60 FPS significa que o quadro cabe em 16,7 ms, mas não mostra quanto sobra. Para medir a folga, aumente a carga, por exemplo com mais camadas de tela cheia, até o FPS cair.

Mantenha **Display → Window → Vsync → Use Vsync** ligado no jogo final. Ele não limita mais nada e evita o *tearing*, a divisão horizontal da imagem durante movimentos.

### Outras recomendações

- Evite muitas camadas de tela cheia sobrepostas, como fundos com parallax em várias camadas.
- Evite `Light2D`, shaders de tela inteira, `BackBufferCopy` e efeitos de `WorldEnvironment`, como o glow.
- Prefira `CPUParticles2D` a `Particles2D`.
- Os fragment shaders da Mali-400 só têm precisão `mediump`. Shaders próprios que fazem contas com valores grandes perdem precisão.
- O GDScript é interpretado e roda bem mais devagar que C no Cortex-A7. Jogos com muita lógica por quadro, como centenas de inimigos ou pathfinding, podem esbarrar na CPU antes da GPU.

## Problemas comuns

| Sintoma | Causa provável |
|---|---|
| `/bin/start finalizou pelo sinal 4`, sem mensagem | Binário ARMv8, como os prontos do FRT. Compile o FRT com a toolchain do Buildroot. |
| O Godot inicia, mas avisa que não encontrou os dados do projeto | O `.pck` não tem o mesmo nome do executável, ou não está na mesma pasta. Use `/bin/start` e `/bin/start.pck`. |
| Faixas pretas em cima e embaixo | O HDMI mudou para um modo 4:3. Ajuste *Test Width / Test Height* para um modo 16:9, como 1280×720. |
| FPS preso em 30 com vsync ligado | O quadro leva mais de 16,7 ms e espera o vblank seguinte. Desligue o vsync temporariamente para ver o FPS real. |
| FPS nunca passa de 60, mesmo sem vsync | Comportamento normal do KMS/DRM. Veja [Vsync e o limite de 60 FPS](#vsync-e-o-limite-de-60-fps). |
| Desempenho baixo em 1080p nativo | Use o FRT sem buffer de profundidade e `render_direct_to_screen`, ou desenhe em resolução menor e amplie. |
