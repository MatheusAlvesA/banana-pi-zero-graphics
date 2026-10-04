#define _FILE_OFFSET_BITS 64
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define MAX_CARDS 16

/* A libdrm consulta o kernel e aloca as listas de recursos por conta propria.
 * Cada objeto retornado tem uma funcao drmModeFree* correspondente. */
static int select_output(int fd, const drmModeRes *res,
                         uint32_t *conn_id, uint32_t *crtc_id,
                         drmModeModeInfo *mode)
{
    /* Conector representa a saida fisica; so interessa uma com monitor e modos. */
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn)
            continue;
        if (conn->connection != DRM_MODE_CONNECTED || !conn->count_modes ||
            !conn->count_encoders) {
            drmModeFreeConnector(conn);
            continue;
        }
        /* Encoder liga o conector a um CRTC, que controla a leitura do
         * framebuffer e os tempos do sinal de video. */
        for (int e = 0; e < conn->count_encoders && !*crtc_id; e++) {
            drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[e]);
            if (!enc)
                continue;
            /* possible_crtcs e uma mascara de INDICES em res->crtcs, nao IDs.
             * Mantemos a escolha original: preferir o CRTC atual do encoder;
             * se ele nao for compativel, usar o ultimo compativel da lista. */
            for (int c = 0; c < res->count_crtcs && c < 32; c++) {
                if (enc->possible_crtcs & (UINT32_C(1) << c)) {
                    *crtc_id = res->crtcs[c];
                    if (res->crtcs[c] == enc->crtc_id)
                        break;
                }
            }
            drmModeFreeEncoder(enc);
        }
        if (*crtc_id) {
            *conn_id = conn->connector_id;
            /* Copiar o modo permite liberar o conector sem perder seus dados.
             * Usar o modo preferido do monitor, ou o primeiro como alternativa. */
            *mode = conn->modes[0];
            for (int m = 0; m < conn->count_modes; m++) {
                if (conn->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                    *mode = conn->modes[m];
                    break;
                }
            }
        }
        drmModeFreeConnector(conn);
        if (*crtc_id)
            return 0;
    }
    errno = ENODEV;
    return -1;
}

/* Os minors nao comecam necessariamente em zero: o framebuffer de boot
 * (simpledrm/efifb) pode ficar com card0 e liberar o node ao sair, e os
 * minors nao sao reaproveitados no mesmo boot. Por isso enumeramos. */
static int list_cards(unsigned int *nums, int max)
{
    DIR *dir = opendir("/dev/dri");
    if (!dir)
        return -1;
    int n = 0;
    struct dirent *ent;
    while (n < max && (ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "card", 4) != 0)
            continue;
        const char *digits = ent->d_name + 4;
        char *end;
        errno = 0;
        unsigned long v = strtoul(digits, &end, 10);
        /* Ignora renderD* e nomes sem numero puro. */
        if (errno || end == digits || *end || v > UINT32_MAX)
            continue;
        nums[n++] = (unsigned int)v;
    }
    closedir(dir);
    /* Ordem numerica, para tentar card2 antes de card10. */
    for (int i = 1; i < n; i++) {
        unsigned int v = nums[i];
        int j = i - 1;
        while (j >= 0 && nums[j] > v) {
            nums[j + 1] = nums[j];
            j--;
        }
        nums[j + 1] = v;
    }
    return n;
}

/* Abre o primeiro node primario que tenha um conector conectado utilizavel. */
static int open_kms_device(char *path, size_t pathsz,
                           drmModeRes **res, uint32_t *conn_id,
                           uint32_t *crtc_id, drmModeModeInfo *mode)
{
    unsigned int nums[MAX_CARDS];
    int n = list_cards(nums, MAX_CARDS);
    if (n == -1) {
        perror("Abrir /dev/dri");
        return -1;
    }
    if (n == 0) {
        fputs("Nenhum node /dev/dri/card* encontrado (KMS ausente?)\n", stderr);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        snprintf(path, pathsz, "/dev/dri/card%u", nums[i]);
        int fd = open(path, O_RDWR);
        if (fd == -1) {
            fprintf(stderr, "%s: abrir: %s\n", path, strerror(errno));
            continue;
        }
        *res = NULL;
        *conn_id = *crtc_id = 0;
        /* Abrir o node continua necessario: a libdrm opera sobre este fd. */
        *res = drmModeGetResources(fd);
        if (!*res)
            fprintf(stderr, "%s: ler recursos DRM: %s\n", path, strerror(errno));
        else if (select_output(fd, *res, conn_id, crtc_id, mode) == -1)
            fprintf(stderr, "%s: sem conector conectado com CRTC compativel: %s\n",
                    path, strerror(errno));
        else
            return fd;
        drmModeFreeResources(*res);
        *res = NULL;
        close(fd);
    }
    fputs("Nenhum device DRM utilizavel\n", stderr);
    errno = ENODEV;
    return -1;
}

/* Os ioctls de modeset sao marcados DRM_MASTER no kernel: a operacao devolve
 * EACCES para quem nao e o master atual, e root nao contorna esse teste.
 * Se ninguem tinha o device aberto, o open() acima ja nos tornou master e
 * SET_MASTER retorna 0; se o compositor e master, retorna EBUSY. */
static int become_master(int fd, const char *path)
{
    if (drmSetMaster(fd) == 0)
        return 0;
    int err = errno;
    fprintf(stderr, "Assumir DRM master em %s: %s\n", path, strerror(err));
    if (err == EBUSY)
        fputs("Outro processo ja e DRM master (Xorg, compositor Wayland, plymouth).\n"
              "Rode de um VT livre (Ctrl+Alt+F3) ou pare o display manager:\n"
              "  sudo systemctl stop gdm\n", stderr);
    else if (err == EACCES || err == EPERM)
        fputs("Sem CAP_SYS_ADMIN e sem heranca de master: rode como root.\n", stderr);
    errno = err;
    return -1;
}

int main(void)
{
    int status = 1;
    int have_master = 0;
    drmModeRes *res = NULL;
    uint32_t conn_id = 0, crtc_id = 0;
    drmModeModeInfo mode = {0};
    uint32_t handle = 0, pitch = 0, fb_id = 0;
    uint64_t size = 0, offset = 0;
    unsigned char *map = MAP_FAILED;
    char path[64] = "";
    int fd = open_kms_device(path, sizeof(path), &res,
                             &conn_id, &crtc_id, &mode);
    if (fd == -1)
        goto done;
    printf("Usando %s\n", path);
    printf("Tela detectada: %ux%u (%.*s)\n", mode.hdisplay, mode.vdisplay,
           (int)sizeof(mode.name), mode.name);
    fflush(stdout);

    /* Falha cedo e com diagnostico, em vez de EACCES no SETCRTC. */
    if (become_master(fd, path) == -1)
        goto done;
    have_master = 1;

    /* Dumb buffers sao memoria de pixels simples, gravavel pela CPU.
     * O driver retorna pitch (bytes por linha) e size (tamanho da alocacao),
     * que podem incluir alinhamento alem da largura visivel. */
    /* Estas funcoes de dumb buffer retornam -errno em caso de falha. */
    int ret = drmModeCreateDumbBuffer(fd, mode.hdisplay, mode.vdisplay, 32, 0,
                                      &handle, &pitch, &size);
    if (ret != 0) {
        errno = -ret;
        perror("Criar dumb buffer");
        goto done;
    }
    /* Validar os limites antes de mapear ou calcular enderecos de pixels. */
    if (!size || size > SIZE_MAX ||
        pitch < (uint64_t)mode.hdisplay * 4 ||
        (uint64_t)pitch * mode.vdisplay > size) {
        fputs("Dimensoes do dumb buffer invalidas\n", stderr);
        goto done;
    }
    /* Registrar um framebuffer associa dimensoes/formato ao buffer.
     * Depth 24 e bpp 32 descrevem XRGB8888: 0x00RRGGBB, sem canal alfa. */
    if (drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32,
                     pitch, handle, &fb_id) != 0) {
        perror("Registrar framebuffer (ADDFB)");
        goto done;
    }
    /* Obter o offset especial para mmap; ele nao e um ponteiro de memoria.
     * mmap permanece uma chamada POSIX, usando o offset fornecido pela libdrm. */
    ret = drmModeMapDumbBuffer(fd, handle, &offset);
    if (ret != 0) {
        errno = -ret;
        perror("Obter offset do dumb buffer");
        goto done;
    }
    if (offset > INT64_MAX) {
        fputs("Offset do dumb buffer invalido\n", stderr);
        goto done;
    }
    map = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, (off_t)offset);
    if (map == MAP_FAILED) {
        perror("mmap");
        goto done;
    }
    /* Preencher antes do modeset, respeitando o padding entre linhas. */
    for (uint32_t y = 0; y < mode.vdisplay; y++) {
        uint32_t *row = (uint32_t *)(map + (size_t)y * pitch);
        for (uint32_t x = 0; x < mode.hdisplay; x++)
            row[x] = 0x00FF0000;
    }
    /* Conectar o framebuffer ao CRTC e este ao conector escolhido.
     * x=y=0 posiciona a origem da imagem; o modo define resolucao e timings. */
    if (drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode) != 0) {
        /* Perder o VT faz o kernel revogar o master: EACCES aqui tambem. */
        perror("Aplicar SETCRTC (requer DRM master)");
        goto done;
    }
    /* Manter a imagem por 10 segundos, retomando sleep se houver sinal. */
    unsigned int remaining = 10;
    while (remaining)
        remaining = sleep(remaining);
    status = 0;

done:
    /* Desfazer em ordem: mapeamento, framebuffer, buffer, master e fd.
     * Os identificadores zerados permitem limpar tambem uma falha parcial. */
    if (map != MAP_FAILED)
        munmap(map, (size_t)size);
    /* RMFB tambem desativa o CRTC que estiver usando este framebuffer. */
    if (fb_id && drmModeRmFB(fd, fb_id) != 0) {
        perror("Remover framebuffer");
        status = 1;
    }
    if (handle) {
        int destroy_ret = drmModeDestroyDumbBuffer(fd, handle);
        if (destroy_ret != 0) {
            errno = -destroy_ret;
            perror("Destruir dumb buffer");
            status = 1;
        }
    }
    if (have_master && drmDropMaster(fd) != 0)
        perror("Liberar DRM master");
    drmModeFreeResources(res);
    if (fd != -1)
        close(fd);
    /* Como programa normal, retorna; PID 1 nunca pode encerrar. */
    if (getpid() == 1)
        for (;;)
            sleep(100);
    return status;
}
