# ENV Config
setenv verbosity "4"
setenv rootfstype "ext4"

echo "Iniciando Boot Script!"

# Preserve o dispositivo selecionado pelo boot automatico do U-Boot.
if test -z "${devtype}"; then
    setenv devtype mmc
fi
if test -z "${devnum}"; then
    setenv devnum ${mmc_bootdev}
fi
if test -z "${devnum}"; then
    setenv devnum 0
fi

# Neste projeto, boot e raiz ficam na primeira particao do mesmo cartao.
# A numeracao MMC do Linux nao precisa coincidir com a do U-Boot.
setenv partuuid
if part uuid ${devtype} ${devnum}:1 partuuid; then
    setenv rootdev "PARTUUID=${partuuid}"
else
    echo "ERRO: nao foi possivel identificar a particao raiz"
    exit
fi
echo "Particao raiz: ${rootdev}"

setenv consoleargs "earlycon console=ttyS0,115200 console=tty1"
setenv bootargs "root=${rootdev} rootwait rootfstype=${rootfstype} ${consoleargs} consoleblank=0 loglevel=${verbosity} ubootpart=${partuuid} ubootsource=${devtype} cma=128M"

echo "Carregando Kernel em memória..."
load ${devtype} ${devnum} ${kernel_addr_r} ${prefix}zImage

echo "Carregando Device tree"
load ${devtype} ${devnum} ${fdt_addr_r} ${prefix}dtb/${fdtfile}
fdt addr ${fdt_addr_r}

# Os ajustes do device tree deste projeto (GPU, CPUs e audio HDMI) ficam no
# overlay dtb/overlay/bananapi-m2-zero-graphics.dtbo. O DTB base precisa ter
# sido compilado com simbolos (DTC_FLAGS=-@) para que o overlay seja aplicado.
setenv overlayfile bananapi-m2-zero-graphics.dtbo
echo "Aplicando overlay ${overlayfile}"
fdt resize 0x2000 || exit
load ${devtype} ${devnum} ${fdtoverlay_addr_r} ${prefix}dtb/overlay/${overlayfile} || exit
if fdt apply ${fdtoverlay_addr_r}; then
    echo "Overlay aplicado"
else
    echo "ERRO: falha ao aplicar o overlay ${overlayfile}"
    exit
fi
setenv overlayfile

echo "Iniciando boot do Kernel..."
bootz ${kernel_addr_r} - ${fdt_addr_r}
