# LED de diagnóstico sem DDR — Banana Pi M2 Zero

Este diagnóstico é independente do sistema gráfico do projeto. Destina-se à
M2 Zero com Allwinner H2+/H3 e LED vermelho em **PL10, ativo baixo**, conforme
o DTB incluído no repositório. Não é um binário Linux nem um programa chamado
pelo U-Boot. Não se aplica automaticamente a outras Banana Pi ou ao H5.

## Como funciona

Ao ligar, a Boot ROM (BROM), gravada dentro do SoC, procura uma imagem no
cartão SD. No offset **8192 bytes (8 KiB, setor 16)**, ela reconhece o cabeçalho
`eGON.BT0`, valida o checksum, carrega a imagem na SRAM interna e transfere
a execução para ela. A primeira instrução salta sobre o cabeçalho de 96 bytes.

O programa roda na SRAM A1 em `0x00000000`. Habilita o clock
do R_PIO, configura PL10 como saída e alterna o LED indefinidamente. Usa
apenas registradores da CPU, constantes na SRAM e registradores do PRCM/R_PIO.
Não usa pilha, libc, heap, sistema de arquivos, interrupções, inicialização
de DDR, acesso ao controlador de DDR ou acesso à memória externa. Não altera
os pinos PL8/PL9 de alimentação da placa. O intervalo é um laço de espera;
não representa uma medida precisa de tempo e depende do clock deixado pela ROM.

É possível dispensar bootloaders externos; **não é possível dispensar a Boot
ROM** neste fluxo. A CPU não executa instruções diretamente do cartão SD.
Também não é execução sem nenhuma RAM: a SRAM integrada precisa funcionar.

## Compilar e verificar

Na raiz do projeto:

```sh
make -C baremetal
make -C baremetal check
make -C baremetal disassemble
```

Requer Python 3, GNU Make e binutils ARM com prefixo `arm-linux-gnueabihf-`.
Também pode usar a toolchain Buildroot já presente:

```sh
make -C baremetal CROSS_COMPILE="$PWD/buildroot/output/host/bin/arm-buildroot-linux-gnueabihf-"
```

O arquivo a gravar é **`baremetal/blink.bin`**, já gerado neste ambiente,
com exatamente 8192 bytes. `blink.raw` e `blink.elf` não devem ser gravados.
`image.py` preenche tamanho, padding e soma de palavras little-endian de
32 bits, usando `0x5f0a6c39` temporariamente no campo de checksum.

## Gravar no cartão

Use um cartão de teste e identifique seu dispositivo com `lsblk`.
Desmonte suas partições antes de gravar. O comando abaixo **substitui a primeira
etapa do boot existente**; também sobrescreve parte da tabela GPT se houver.
O intervalo escrito é `[8192, 16384)`. Não pode haver partição nesse intervalo.
Para este teste, prefira cartão sem GPT e sem outro firmware de boot; não é
necessário criar partições ou formatar um sistema de arquivos.

Substitua `/dev/sdX` pelo dispositivo inteiro do cartão, não por `/dev/sdX1`:

```sh
sudo dd if=baremetal/blink.bin of=/dev/sdX bs=1024 seek=8 conv=notrunc,fsync status=progress
```

Para conferir os bytes efetivamente gravados:

```sh
sudo dd if=/dev/sdX of=/tmp/banana-blink-readback.bin bs=1024 skip=8 count=8 status=none
cmp baremetal/blink.bin /tmp/banana-blink-readback.bin
python3 baremetal/image.py /tmp/banana-blink-readback.bin
```

Coloque o cartão na placa desligada e ligue a alimentação. O LED vermelho
deve alternar entre aceso e apagado continuamente. Este programa permanece
nesse laço e não inicia Linux. Para voltar ao sistema anterior, regrave seu
firmware original ou use o cartão original.

## O que o resultado permite concluir

Se o LED piscar, houve execução de instruções pela CPU, leitura suficiente
do SD para carregar a imagem, uso da SRAM interna e acesso ao GPIO do LED.
Isso não é um teste completo do chip, dos quatro núcleos, do cartão ou da DDR.
O programa não testa a DDR e, portanto, não distingue DDR boa de DDR defeituosa.

Uma DDR ausente ou com falha de armazenamento não é uma dependência deste
código. Entretanto, DDR em curto, alimentação derrubada, dano ao SoC ou
problemas elétricos na placa podem impedir o boot inteiro. Software não
contorna essas falhas. A ausência de piscadas também pode significar falha
de alimentação, clock, SRAM, SD, imagem, GPIO ou LED; não identifica sozinha
qual componente falhou. LED constantemente aceso não comprova execução.


## Referências técnicas

- [Device tree oficial: LED PL10 ativo baixo](https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/allwinner/sun8i-h2-plus-bananapi-m2-zero.dts).
- [U-Boot: Boot ROM e offset de 8 KiB no SD](https://docs.u-boot.org/en/latest/board/allwinner/sunxi.html).
- [U-Boot: SRAM e endereços do PRCM](https://github.com/u-boot/u-boot/blob/master/arch/arm/include/asm/arch-sunxi/cpu_sun4i.h).
- [Linux: clock/reset do R_PIO](https://github.com/torvalds/linux/blob/master/drivers/clk/sunxi-ng/ccu-sun8i-r.c).
- [Formato eGON](https://github.com/u-boot/u-boot/blob/master/include/sunxi_image.h) e [geração do checksum](https://github.com/u-boot/u-boot/blob/master/tools/sunxi_egon.c).
