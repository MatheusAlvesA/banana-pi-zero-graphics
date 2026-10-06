#include <stdio.h>
#include <SDL2/SDL.h>

int main(void)
{
    int status = 1;
    SDL_DisplayMode mode;
    SDL_RendererInfo info;

    /* Sem X11 ou Wayland, o unico driver de video compilado e o KMSDRM. Ele
     * procura em /dev/dri o node com um conector conectado (o do sun4i-drm),
     * assume o DRM master e cria GBM e EGL, como o start_gl.c faz a mao. */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Iniciar SDL: %s\n", SDL_GetError());
        goto done;
    }
    printf("Driver de video: %s\n", SDL_GetCurrentVideoDriver());
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0)
        printf("Tela detectada: %dx%d @ %d Hz\n",
               mode.w, mode.h, mode.refresh_rate);

    /* No KMSDRM, a janela ocupa a tela inteira, no modo atual da saida. */
    SDL_Window *window = SDL_CreateWindow("start", 0, 0, 0, 0,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        fprintf(stderr, "Criar janela: %s\n", SDL_GetError());
        goto done;
    }
    /* O renderer 2D do SDL desenha pela GPU com OpenGL ES 2.0.
     * PRESENTVSYNC faz cada quadro esperar o vblank da tela. */
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED |
                                                SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "Criar renderer: %s\n", SDL_GetError());
        goto done;
    }
    if (SDL_GetRendererInfo(renderer, &info) == 0)
        printf("Renderer: %s\n", info.name);
    fflush(stdout);

    /* Laco de quadros, como num jogo: tratar eventos, desenhar e apresentar.
     * O SDL converte SIGINT e SIGTERM em SDL_QUIT. O teclado chega pelo
     * evdev, nos dispositivos que o udev identificou. */
    Uint8 red = 255, green = 0;
    Uint64 end = SDL_GetTicks64() + 10000;
    while (SDL_GetTicks64() < end) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                end = 0;
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.sym == SDLK_SPACE) {
                red = 0;
                green = 255;
            }
        }
        SDL_SetRenderDrawColor(renderer, red, green, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }
    status = 0;

done:
    /* SDL_Quit destroi a janela e o renderer, libera o DRM master e
     * restaura o modo anterior da tela (o console do kernel). */
    SDL_Quit();
    return status;
}
