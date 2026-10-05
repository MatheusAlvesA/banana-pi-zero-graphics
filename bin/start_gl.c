#define _FILE_OFFSET_BITS 64
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

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

/* Abre o primeiro node primario que tenha um conector conectado utilizavel.
 * Na Banana Pi, e o node do sun4i-drm; o node do lima nao tem conectores. */
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
        int fd = open(path, O_RDWR | O_CLOEXEC);
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
 * SET_MASTER retorna 0; se outro processo e master, retorna EBUSY. */
static int become_master(int fd, const char *path)
{
    if (drmSetMaster(fd) == 0)
        return 0;
    int err = errno;
    fprintf(stderr, "Assumir DRM master em %s: %s\n", path, strerror(err));
    if (err == EBUSY)
        fputs("Outro processo ja e DRM master deste device.\n", stderr);
    else if (err == EACCES || err == EPERM)
        fputs("Sem CAP_SYS_ADMIN e sem heranca de master: rode como root.\n", stderr);
    errno = err;
    return -1;
}

/* O EGL devolve varias configs compativeis com os atributos pedidos, inclusive
 * com canal alfa. A superficie GBM e XRGB8888, entao escolhemos a config cujo
 * formato nativo (EGL_NATIVE_VISUAL_ID) e exatamente esse. */
static int choose_config(EGLDisplay dpy, EGLConfig *config)
{
    static const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint count = 0;
    if (!eglChooseConfig(dpy, attrs, NULL, 0, &count) || count < 1) {
        fprintf(stderr, "Nenhuma config EGL compativel (erro 0x%x)\n",
                eglGetError());
        return -1;
    }
    EGLConfig *configs = calloc((size_t)count, sizeof(*configs));
    if (!configs) {
        perror("calloc");
        return -1;
    }
    int found = -1;
    if (eglChooseConfig(dpy, attrs, configs, count, &count)) {
        for (EGLint i = 0; i < count && found == -1; i++) {
            EGLint id;
            if (eglGetConfigAttrib(dpy, configs[i], EGL_NATIVE_VISUAL_ID, &id) &&
                (uint32_t)id == GBM_FORMAT_XRGB8888) {
                *config = configs[i];
                found = 0;
            }
        }
    }
    free(configs);
    if (found == -1)
        fputs("Nenhuma config EGL com formato XRGB8888\n", stderr);
    return found;
}

int main(void)
{
    int status = 1;
    int have_master = 0;
    drmModeRes *res = NULL;
    uint32_t conn_id = 0, crtc_id = 0;
    drmModeModeInfo mode = {0};
    uint32_t fb_id = 0;
    struct gbm_device *gbm = NULL;
    struct gbm_surface *gbm_surf = NULL;
    struct gbm_bo *bo = NULL;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLConfig config;
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

    /* A GBM aloca buffers no device de exibicao. Com o driver kmsro da Mesa,
     * esses buffers podem ser lidos pela tela (SCANOUT) e, ao mesmo tempo,
     * usados pela GPU do lima como destino da renderizacao (RENDERING). */
    gbm = gbm_create_device(fd);
    if (!gbm) {
        perror("Criar device GBM");
        goto done;
    }
    gbm_surf = gbm_surface_create(gbm, mode.hdisplay, mode.vdisplay,
                                  GBM_FORMAT_XRGB8888,
                                  GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surf) {
        perror("Criar superficie GBM");
        goto done;
    }

    /* Sem X11 ou Wayland, o display EGL e criado diretamente sobre a GBM. */
    dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    if (dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "Obter display EGL (erro 0x%x)\n", eglGetError());
        goto done;
    }
    EGLint major, minor;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "Inicializar EGL (erro 0x%x)\n", eglGetError());
        dpy = EGL_NO_DISPLAY;
        goto done;
    }
    printf("EGL %d.%d (%s)\n", major, minor, eglQueryString(dpy, EGL_VENDOR));
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "Selecionar OpenGL ES (erro 0x%x)\n", eglGetError());
        goto done;
    }
    if (choose_config(dpy, &config) == -1)
        goto done;

    /* A Mali-400 suporta apenas OpenGL ES 2.0. */
    static const EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "Criar contexto OpenGL ES (erro 0x%x)\n", eglGetError());
        goto done;
    }
    surf = eglCreateWindowSurface(dpy, config,
                                  (EGLNativeWindowType)gbm_surf, NULL);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "Criar superficie EGL (erro 0x%x)\n", eglGetError());
        goto done;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "Ativar contexto EGL (erro 0x%x)\n", eglGetError());
        goto done;
    }
    printf("GPU: %s\nOpenGL ES: %s\n",
           (const char *)glGetString(GL_RENDERER),
           (const char *)glGetString(GL_VERSION));
    fflush(stdout);

    /* Desenhar com a GPU: limpar toda a superficie de vermelho. */
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
        fprintf(stderr, "Desenhar com OpenGL ES (erro 0x%x)\n", gl_err);
        goto done;
    }
    /* Com GBM, o swap apenas finaliza o quadro; ainda cabe a nos exibi-lo. */
    if (!eglSwapBuffers(dpy, surf)) {
        fprintf(stderr, "Finalizar quadro (erro 0x%x)\n", eglGetError());
        goto done;
    }
    /* Reservar o buffer recem-desenhado, para a GBM nao reutiliza-lo. */
    bo = gbm_surface_lock_front_buffer(gbm_surf);
    if (!bo) {
        fputs("Obter buffer desenhado da superficie GBM\n", stderr);
        goto done;
    }
    /* Registrar o buffer como framebuffer, como no start.c.
     * Depth 24 e bpp 32 descrevem XRGB8888: 0x00RRGGBB, sem canal alfa. */
    if (drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32,
                     gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32,
                     &fb_id) != 0) {
        perror("Registrar framebuffer (ADDFB)");
        goto done;
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
    /* Desfazer em ordem inversa: framebuffer, buffer GBM, EGL, GBM, master
     * e fd. Os identificadores nulos permitem limpar tambem uma falha parcial. */
    /* RMFB tambem desativa o CRTC que estiver usando este framebuffer. */
    if (fb_id && drmModeRmFB(fd, fb_id) != 0) {
        perror("Remover framebuffer");
        status = 1;
    }
    if (bo)
        gbm_surface_release_buffer(gbm_surf, bo);
    if (dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surf != EGL_NO_SURFACE)
            eglDestroySurface(dpy, surf);
        if (ctx != EGL_NO_CONTEXT)
            eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
    }
    if (gbm_surf)
        gbm_surface_destroy(gbm_surf);
    if (gbm)
        gbm_device_destroy(gbm);
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
