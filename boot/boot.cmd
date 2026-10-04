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

# Capacidade relativa igual nos quatro Cortex-A7 (1024 nao e frequencia).
fdt resize 0x100 || exit
setenv dt_cpu_index
for dt_cpu_index in 0 1 2 3; do
    fdt set /cpus/cpu@${dt_cpu_index} capacity-dmips-mhz <0x400> || exit
done

# A GPU usa a alimentacao fixa de 1,2 V em todos os pontos de operacao.
fdt resize 0x100 || exit
fdt get value gpu_supply /vcc1v2 phandle || exit
fdt set /soc/gpu@1c40000 mali-supply <${gpu_supply}> || exit
setenv gpu_opp_hz
for gpu_opp_hz in 120000000 312000000 432000000 576000000; do
    fdt set /opp-table-gpu/opp-${gpu_opp_hz} opp-microvolt <0x124f80> || exit
done
setenv gpu_supply

echo "Iniciando boot do Kernel..."
bootz ${kernel_addr_r} - ${fdt_addr_r}
