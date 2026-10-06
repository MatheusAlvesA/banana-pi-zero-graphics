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

# Audio HDMI: a I2S2 envia o audio ao controlador HDMI, e a placa de som
# simple-audio-card liga as duas pontas. O codec do HDMI exige frame clock
# invertido e slots fixos de 32 bits, configurados pelas propriedades de TDM.
# Os nos do HDMI e da I2S2 nao tem phandle no DTB original; se nao tiverem,
# recebem valores livres (os phandles existentes vao ate 0x35).
fdt resize 0x400 || exit
if fdt get value hdmi_dai /soc/hdmi@1ee0000 phandle; then
    echo "HDMI ja tem phandle"
else
    setenv hdmi_dai 0x1000
    fdt set /soc/hdmi@1ee0000 phandle <${hdmi_dai}> || exit
fi
if fdt get value i2s2_dai /soc/i2s@1c22800 phandle; then
    echo "I2S2 ja tem phandle"
else
    setenv i2s2_dai 0x1001
    fdt set /soc/i2s@1c22800 phandle <${i2s2_dai}> || exit
fi
fdt set /soc/hdmi@1ee0000 "#sound-dai-cells" <0> || exit
fdt set /soc/i2s@1c22800 status okay || exit
fdt mknode / hdmi-sound || exit
fdt set /hdmi-sound compatible simple-audio-card || exit
fdt set /hdmi-sound simple-audio-card,name sun8i-h3-hdmi || exit
fdt set /hdmi-sound simple-audio-card,format i2s || exit
fdt set /hdmi-sound simple-audio-card,mclk-fs <128> || exit
fdt set /hdmi-sound simple-audio-card,frame-inversion || exit
# A I2S2 so tem DMA de envio (tx). Sem playback-only, a placa tambem cria o
# fluxo de captura, nao encontra o DMA de recepcao e falha com -EINVAL.
fdt set /hdmi-sound playback-only || exit
fdt mknode /hdmi-sound simple-audio-card,codec || exit
fdt set /hdmi-sound/simple-audio-card,codec sound-dai <${hdmi_dai}> || exit
fdt mknode /hdmi-sound simple-audio-card,cpu || exit
fdt set /hdmi-sound/simple-audio-card,cpu sound-dai <${i2s2_dai}> || exit
fdt set /hdmi-sound/simple-audio-card,cpu dai-tdm-slot-num <2> || exit
fdt set /hdmi-sound/simple-audio-card,cpu dai-tdm-slot-width <32> || exit
setenv hdmi_dai
setenv i2s2_dai

echo "Iniciando boot do Kernel..."
bootz ${kernel_addr_r} - ${fdt_addr_r}
