#include <stdio.h>
#include <SDL2/SDL.h>

#define TONE_HZ 440.0f
#define TONE_VOLUME 0.2f

/* Chamada pela thread de audio do SDL sempre que o dispositivo precisa de
 * mais amostras. Gera um seno de 440 Hz (a nota la), igual nos dois canais.
 * A fase continua entre chamadas para a onda nao ter saltos audiveis. */
static void tone_callback(void *userdata, Uint8 *stream, int len)
{
    SDL_AudioSpec *spec = userdata;
    Sint16 *samples = (Sint16 *)stream;
    int frames = len / (int)(sizeof(Sint16) * spec->channels);
    static float phase;

    for (int i = 0; i < frames; i++) {
        Sint16 value = (Sint16)(SDL_sinf(phase) * TONE_VOLUME * 32767.0f);
        for (int ch = 0; ch < spec->channels; ch++)
            *samples++ = value;
        phase += 2.0f * (float)M_PI * TONE_HZ / (float)spec->freq;
        if (phase >= 2.0f * (float)M_PI)
            phase -= 2.0f * (float)M_PI;
    }
}

/* O audio e opcional: sem placa de som em /dev/snd (kernel sem a
 * simple-audio-card ou device tree sem o audio HDMI), o programa avisa e
 * segue so com o video. */
static void start_tone(SDL_AudioSpec *have)
{
    SDL_AudioSpec want;
    SDL_AudioDeviceID device;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "Iniciar audio: %s\n", SDL_GetError());
        return;
    }
    printf("Driver de audio: %s\n", SDL_GetCurrentAudioDriver());

    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = tone_callback;
    want.userdata = have;
    /* Sem permitir mudancas, o SDL converte para o formato pedido se o
     * dispositivo usar outro, e o callback sempre recebe S16 estereo. */
    device = SDL_OpenAudioDevice(NULL, 0, &want, have, 0);
    if (device == 0) {
        fprintf(stderr, "Abrir dispositivo de audio: %s\n", SDL_GetError());
        return;
    }
    printf("Audio: %d Hz, %d canais\n", have->freq, have->channels);
    /* O dispositivo abre pausado; despausar inicia o callback. */
    SDL_PauseAudioDevice(device, 0);
}

int main(void)
{
    int status = 1;
    SDL_DisplayMode mode;
    SDL_RendererInfo info;
    SDL_AudioSpec audio_spec;

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
    start_tone(&audio_spec);
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
    /* SDL_Quit fecha o dispositivo de audio, destroi a janela e o renderer,
     * libera o DRM master e restaura o modo anterior da tela (o console do
     * kernel). */
    SDL_Quit();
    return status;
}
